#include "dlss/runtime.hpp"

#include "core/log.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>

namespace nitid::dlss {

namespace fs = std::filesystem;

namespace {

// Folder of libnitid.so itself (the module and a system-wide runtime live next to it).
fs::path library_dir()
{
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&library_dir), &info) || !info.dli_fname) return {};
    return fs::path(info.dli_fname).parent_path();
}

void module_log(int level, const char* msg)
{
    if (level >= 2) log::error("{}", msg);
    else if (level == 1) log::warn("{}", msg);
    else log::info("{}", msg);
}

bool has_runtime(const fs::path& d)
{
    std::error_code ec;
    if (!fs::is_directory(d, ec)) return false;
    for (auto& e : fs::directory_iterator(d, ec))
        if (e.path().filename().string().starts_with("libnvidia-ngx-dlss")) return true;
    return false;
}

} // namespace

std::vector<std::string> Runtime::runtime_dirs()
{
    std::vector<std::string> dirs;
    auto add = [&](const fs::path& p) {
        if (!p.empty() && has_runtime(p)) dirs.push_back(p.string());
    };
    if (const char* e = std::getenv("NITID_DLSS_DIR"); e && *e) {  // explicit choice: only there
        add(e);
        return dirs;
    }
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) add(fs::path(x) / "nitid/dlss");
    else if (const char* h = std::getenv("HOME"); h && *h) add(fs::path(h) / ".local/share/nitid/dlss");
    if (auto lib = library_dir(); !lib.empty()) add(lib / "nitid/dlss");
    if (*NITID_DLSS_DEFAULT_DIR) add(NITID_DLSS_DEFAULT_DIR);  // developer builds: the SDK's own copies
    return dirs;
}

std::unique_ptr<Runtime> Runtime::load(std::string* why)
{
    std::vector<fs::path> candidates;
    if (const char* e = std::getenv("NITID_DLSS_MODULE"); e && *e) candidates.emplace_back(e);  // explicit: only it
    else if (auto lib = library_dir(); !lib.empty()) candidates.push_back(lib / "libnitid-dlss.so");
    void* h = nullptr;
    std::string err = "the DLSS module (libnitid-dlss.so) is not part of this build";
    for (auto& c : candidates) {
        std::error_code ec;
        if (!fs::exists(c, ec)) {
            err = c.string() + " not found";
            continue;
        }
        h = dlopen(c.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (h) break;
        const char* e = dlerror();
        err = std::string("can't load ") + c.string() + ": " + (e ? e : "?");
    }
    if (!h) {
        if (why) *why = err;
        return nullptr;
    }
    auto get = reinterpret_cast<NitidDlssGetApi>(dlsym(h, NITID_DLSS_ENTRY));
    const NitidDlssApi* api = get ? get() : nullptr;
    if (!api || api->abi != NITID_DLSS_ABI) {
        if (why) *why = "the DLSS module does not match this libnitid (rebuild or reinstall Nitid)";
        dlclose(h);
        return nullptr;
    }
    std::unique_ptr<Runtime> r(new Runtime());
    r->lib_ = h;
    r->api_ = api;
    return r;
}

Runtime::~Runtime()
{
    if (started_) api_->shutdown();
    if (lib_) dlclose(lib_);
}

std::vector<std::string> Runtime::instance_extensions() const
{
    uint32_t n = 0;
    const char* const* e = api_->instance_extensions(&n);
    return {e, e + n};
}

std::vector<std::string> Runtime::device_extensions() const
{
    uint32_t n = 0;
    const char* const* e = api_->device_extensions(&n);
    return {e, e + n};
}

bool Runtime::start(gpu::VulkanContext& vk, std::string* why)
{
    if (started_) return true;
    if (!vk.optional_extensions) {
        if (why) *why = "the GPU / driver lacks Vulkan extensions DLSS needs";
        return false;
    }
    if (!vk.work_queue) {
        if (why) *why = "not enough Vulkan queues";
        return false;
    }
    auto dirs = runtime_dirs();
    if (dirs.empty()) {
        if (why) *why = "NVIDIA's DLSS runtime is not installed (run nitid-setup-dlss)";
        return false;
    }
    std::vector<const char*> ptrs;
    for (auto& d : dirs) ptrs.push_back(d.c_str());
    fs::path data;
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) data = fs::path(x) / "nitid/ngx";
    else if (const char* h = std::getenv("HOME"); h && *h) data = fs::path(h) / ".cache/nitid/ngx";
    else data = fs::temp_directory_path() / "nitid-ngx";
    std::error_code ec;
    fs::create_directories(data, ec);

    NitidDlssInitInfo info{};
    info.instance = vk.instance;
    info.phys = vk.phys;
    info.device = vk.device;
    info.get_instance_proc = vkGetInstanceProcAddr;
    info.get_device_proc = vkGetDeviceProcAddr;
    info.runtime_dirs = ptrs.data();
    info.runtime_dir_count = uint32_t(ptrs.size());
    const std::string data_s = data.string();
    info.data_dir = data_s.c_str();
    info.app_version = NITID_VERSION_STRING;
    info.log = module_log;
    char err[512] = {};
    if (!api_->init(&info, &caps_, err, sizeof err)) {
        if (why) *why = err;
        return false;
    }
    started_ = true;
    log::info("DLSS {} from {}", sdk_version(), dirs.front());
    return true;
}

} // namespace nitid::dlss
