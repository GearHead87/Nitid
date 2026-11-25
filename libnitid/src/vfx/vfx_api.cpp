#include "vfx/vfx_api.hpp"

#include "core/check.hpp"
#include "core/log.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#ifndef NITID_VFX_DEFAULT_DIR
#define NITID_VFX_DEFAULT_DIR ""
#endif

namespace nitid::vfx {

namespace fs = std::filesystem;

namespace {

// Same order as NVIDIA's Python loader (nvvfx/_lib_loader.py): dependencies first, all RTLD_GLOBAL so the
// effect libraries that libVideoFX opens later resolve against them.
constexpr const char* kPreload[] = {
    "libnppc.so.12",   "libnppial.so.12", "libnppicc.so.12",   "libnppidei.so.12", "libnppif.so.12",
    "libnppig.so.12",  "libnppim.so.12",  "libnppist.so.12",   "libnppitc.so.12",  "libcudnn.so.9",
    "libNVCVImage.so", "libnvngxruntime.so", "libVideoFXLocal.so", "libVideoFX.so",
};

std::vector<std::string> candidate_dirs(const std::string& explicit_dir)
{
    std::vector<std::string> dirs;
    if (!explicit_dir.empty()) dirs.push_back(explicit_dir);
    if (const char* e = std::getenv("NITID_VFX_DIR"); e && *e) dirs.emplace_back(e);
    if (*NITID_VFX_DEFAULT_DIR) dirs.emplace_back(NITID_VFX_DEFAULT_DIR);
    return dirs;
}

template <class F> bool sym(F& f, const char* name)
{
    f = reinterpret_cast<F>(dlsym(RTLD_DEFAULT, name));
    return f != nullptr;
}

} // namespace

void Api::check(NvCV_Status s, const char* what) const
{
    if (s != NVCV_SUCCESS) failf("VFX: {} failed ({} {})", what, int(s), ErrorString ? ErrorString(s) : "");
}

std::shared_ptr<const Api> load_api(const std::string& explicit_dir, std::string* error)
{
    // Loading is process-wide; do it once.
    static std::shared_ptr<const Api> cached;
    static std::string cached_error;
    static bool tried = false;
    if (tried) {
        if (error) *error = cached_error;
        return cached;
    }
    tried = true;

    auto dirs = candidate_dirs(explicit_dir);
    if (dirs.empty()) cached_error = "VFX runtime folder not set (set NITID_VFX_DIR)";
    for (const auto& d : dirs) {
        if (!fs::exists(fs::path(d) / "libVideoFX.so")) {
            cached_error = "libVideoFX.so not found in " + d;
            continue;
        }
        bool core_ok = true;
        for (const char* lib : kPreload) {
            auto p = fs::path(d) / lib;
            if (!fs::exists(p)) continue;  // optional ones differ between releases
            if (!dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL)) {
                cached_error = std::string("cannot load ") + lib + ": " + dlerror();
                core_ok = false;
                break;
            }
        }
        if (!core_ok) continue;
        // Effect libraries (libnvVFX*.so) and their NGX models (libnvidia-ngx-*.so.<version>): libVideoFX
        // opens them by name, which only works if they are already loaded.
        for (const auto& ent : fs::directory_iterator(d)) {
            std::string name = ent.path().filename().string();
            if (name.starts_with("libnvVFX") || name.starts_with("libnvidia-ngx-")) {
                if (!dlopen(ent.path().c_str(), RTLD_NOW | RTLD_GLOBAL))
                    log::warn("cannot load {}: {}", name, dlerror());
            }
        }

        auto api = std::make_shared<Api>();
        bool ok = sym(api->GetVersion, "NvVFX_GetVersion") && sym(api->CreateEffect, "NvVFX_CreateEffect") &&
                  sym(api->DestroyEffect, "NvVFX_DestroyEffect") && sym(api->SetU32, "NvVFX_SetU32") &&
                  sym(api->SetF32, "NvVFX_SetF32") && sym(api->SetImage, "NvVFX_SetImage") &&
                  sym(api->SetCudaStream, "NvVFX_SetCudaStream") && sym(api->Load, "NvVFX_Load") &&
                  sym(api->Run, "NvVFX_Run") && sym(api->ImageInit, "NvCVImage_Init") &&
                  sym(api->ErrorString, "NvCV_GetErrorStringFromCode");
        if (!ok) {
            cached_error = "VFX runtime in " + d + " is missing expected functions";
            continue;
        }
        unsigned v = 0;
        api->GetVersion(&v);
        api->version = std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 0xff) + "." +
                       std::to_string((v >> 8) & 0xff) + "." + std::to_string(v & 0xff);
        api->dir = d;
        log::info("VFX runtime {} loaded from {}", api->version, d);
        cached = api;
        cached_error.clear();
        break;
    }
    if (!cached) log::warn("AI effects unavailable: {}", cached_error);
    if (error) *error = cached_error;
    return cached;
}

NvCVImage rgba_image(const Api& api, void* gpu_ptr, unsigned width, unsigned height)
{
    NvCVImage img{};
    api.check(api.ImageInit(&img, width, height, int(width * 4), gpu_ptr, NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_GPU),
              "NvCVImage_Init");
    return img;
}

} // namespace nitid::vfx

// nvCVImage.h's C++ NvCVImage class calls NvCVImage_Alloc / NvCVImage_Dealloc from its constructor and
// destructor. libnitid doesn't link the runtime (it's dlopen'ed), so forward those two calls to it.
// These definitions have hidden visibility, so RTLD_DEFAULT finds the runtime's real functions.
extern "C" {

NvCV_Status NvCV_API NvCVImage_Alloc(NvCVImage* im, unsigned width, unsigned height, NvCVImage_PixelFormat format,
                                     NvCVImage_ComponentType type, unsigned layout, unsigned mem_space, unsigned alignment)
{
    using Fn = NvCV_Status (*)(NvCVImage*, unsigned, unsigned, NvCVImage_PixelFormat, NvCVImage_ComponentType, unsigned,
                               unsigned, unsigned);
    static Fn fn = nullptr;
    if (!fn) fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "NvCVImage_Alloc"));
    if (fn) return fn(im, width, height, format, type, layout, mem_space, alignment);
    // Runtime not loaded: leave an empty image (plain data; no constructor call, which would recurse).
    std::memset(static_cast<void*>(im), 0, sizeof(NvCVImage));
    return width || height ? NVCV_ERR_LIBRARY : NVCV_SUCCESS;
}

void NvCV_API NvCVImage_Dealloc(NvCVImage* im)
{
    using Fn = void (*)(NvCVImage*);
    static Fn fn = nullptr;
    if (!fn) fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "NvCVImage_Dealloc"));
    if (fn) fn(im);
}

} // extern "C"
