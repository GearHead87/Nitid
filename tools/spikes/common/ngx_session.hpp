// Vulkan + NGX initialisation shared by the DLSS spikes (S2, S3, F1).
#pragma once

#include "ngx_util.hpp"
#include "vk_ctx.hpp"
#include "vk_image.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace spike {

struct NgxSession {
    VkCtx vk;
    std::vector<std::string> dirs;
    std::unique_ptr<NgxFeatureInfo> finfo;
    std::wstring data_path;
    NVSDK_NGX_Parameter* params = nullptr;

    // features: which NGX features to collect Vulkan extension requirements for.
    // extra_dev_exts / features_chain / extra_queue: e.g. VK_NV_optical_flow + its feature + OF queue.
    void init(const std::vector<NVSDK_NGX_Feature>& features, const std::string& results_dir, bool verbose = false,
              std::vector<std::string> extra_dev_exts = {}, const void* features_chain = nullptr,
              VkQueueFlags extra_queue = 0)
    {
        dirs = {NITID_DLSS_LIB_DIR};
        finfo = std::make_unique<NgxFeatureInfo>(dirs, verbose);
        std::filesystem::create_directories(results_dir);
        data_path.assign(results_dir.begin(), results_dir.end());

        std::vector<std::string> inst, dev;
        for (auto f : features) {
            auto d = ngx_discovery(f, &finfo->info, data_path.c_str());
            uint32_t n = 0;
            VkExtensionProperties* p = nullptr;
            if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &n, &p)))
                for (uint32_t k = 0; k < n; ++k) inst.emplace_back(p[k].extensionName);
        }
        vk.create_instance(inst);
        vk.pick_nvidia();
        for (auto f : features) {
            auto d = ngx_discovery(f, &finfo->info, data_path.c_str());
            uint32_t n = 0;
            VkExtensionProperties* p = nullptr;
            if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(vk.instance, vk.phys, &d, &n, &p)))
                for (uint32_t k = 0; k < n; ++k) dev.emplace_back(p[k].extensionName);
        }
        dev.insert(dev.end(), extra_dev_exts.begin(), extra_dev_exts.end());
        vk.create_device(dev, features_chain, extra_queue);
        auto r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kNgxEngineVersion,
                                                      data_path.c_str(), vk.instance, vk.phys, vk.device,
                                                      vkGetInstanceProcAddr, vkGetDeviceProcAddr, &finfo->info);
        if (NVSDK_NGX_FAILED(r)) {
            std::fprintf(stderr, "NGX init failed: %s\n", ngx_result(r).c_str());
            std::exit(1);
        }
        r = NVSDK_NGX_VULKAN_AllocateParameters(&params);
        if (NVSDK_NGX_FAILED(r)) {
            std::fprintf(stderr, "AllocateParameters failed: %s\n", ngx_result(r).c_str());
            std::exit(1);
        }
    }

    void shutdown()
    {
        if (params) NVSDK_NGX_VULKAN_DestroyParameters(params);
        NVSDK_NGX_VULKAN_Shutdown1(vk.device);
        vk.destroy();
    }
};

inline NVSDK_NGX_Resource_VK ngx_res(Image& im, bool rw)
{
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view, im.img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, im.fmt,
                                                  im.w, im.h, rw);
}

}  // namespace spike
