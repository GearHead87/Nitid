#pragma once

// Finds and loads Nitid's DLSS module (libnitid-dlss.so) and, once the Vulkan device exists, starts NVIDIA's DLSS
// (NGX) on it. Both steps are optional: without the module or the DLSS runtime libraries, DLSS is reported as
// unavailable and everything else works as before.
//
// The module comes with Nitid (next to libnitid). The DLSS runtime libraries (libnvidia-ngx-dlss.so.*,
// libnvidia-ngx-dlssg.so.*) are NVIDIA's; nitid-setup-dlss downloads them. Searched, in order:
// ~/.local/share/nitid/dlss, <libdir>/nitid/dlss, and the SDK folder of a developer build. $NITID_DLSS_DIR replaces
// that list (only that folder), and $NITID_DLSS_MODULE names the module file to load instead of the one next to
// libnitid.
#include "dlss/plugin_api.h"
#include "gpu/vulkan_context.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nitid::dlss {

class Runtime {
public:
    // nullptr (with *why) if the module is missing or unusable.
    static std::unique_ptr<Runtime> load(std::string* why);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    std::vector<std::string> instance_extensions() const;
    std::vector<std::string> device_extensions() const;

    // After the device exists (with device_extensions() enabled). False (with *why) if DLSS can't start.
    bool start(gpu::VulkanContext& vk, std::string* why);
    bool started() const { return started_; }

    const NitidDlssApi& api() const { return *api_; }
    const NitidDlssCaps& caps() const { return caps_; }
    std::string sdk_version() const { return api_->sdk_version ? api_->sdk_version : ""; }
    // Folders searched for NVIDIA's DLSS runtime libraries.
    static std::vector<std::string> runtime_dirs();

private:
    Runtime() = default;
    void* lib_ = nullptr;
    const NitidDlssApi* api_ = nullptr;
    NitidDlssCaps caps_{};
    bool started_ = false;
};

} // namespace nitid::dlss
