// S1: Do DLSS Super Resolution and DLSS Frame Generation report as available to our own
// native Vulkan app (no game, no Proton)?
//
// Steps: ask NGX which Vulkan instance/device extensions each feature needs -> create a
// Vulkan 1.3 instance/device on the NVIDIA GPU with them -> NGX GetFeatureRequirements ->
// NVSDK_NGX_VULKAN_Init_with_ProjectID -> GetCapabilityParameters -> print availability.
// Also probes the Linux NGX VSR / TrueHDR libraries shipped in the nvidia-vfx wheel.

#include "ngx_util.hpp"
#include "vk_ctx.hpp"

#include <nvsdk_ngx_defs_dlssg.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace spike;

struct Feat {
    NVSDK_NGX_Feature id;
    const char* name;
};

static const Feat kFeatures[] = {
    {NVSDK_NGX_Feature_SuperSampling, "SuperSampling (DLSS SR)"},
    {NVSDK_NGX_Feature_FrameGeneration, "FrameGeneration (DLSS-G)"},
    {NVSDK_NGX_Feature_RayReconstruction, "RayReconstruction (DLSS RR)"},
    {NVSDK_NGX_Feature_Reserved16, "Reserved16 (VSR in Veyra)"},
};

static std::string support_bits(unsigned v)
{
    if (v == 0) return "Supported";
    std::string s;
    if (v & NVSDK_NGX_FeatureSupportResult_CheckNotPresent) s += "CheckNotPresent ";
    if (v & NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported) s += "DriverVersionUnsupported ";
    if (v & NVSDK_NGX_FeatureSupportResult_AdapterUnsupported) s += "AdapterUnsupported ";
    if (v & NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported) s += "OSVersionBelowMinimum ";
    if (v & NVSDK_NGX_FeatureSupportResult_NotImplemented) s += "NotImplemented ";
    return s;
}

int main(int argc, char** argv)
{
    bool verbose = argc > 1 && std::string(argv[1]) == "-v";

    // Feature library search paths: DLSS SDK libs + the nvidia-vfx wheel's NGX libs.
    std::string vfx_libs = std::string(NITID_ROOT) + "/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs";
    std::vector<std::string> dirs = {NITID_DLSS_LIB_DIR};
    if (fs::exists(vfx_libs)) dirs.push_back(vfx_libs);
    NgxFeatureInfo finfo(dirs, verbose);

    std::string data_dir = std::string(NITID_ROOT) + "/results/s1-ngx-caps";
    fs::create_directories(data_dir);
    std::wstring wdata(data_dir.begin(), data_dir.end());

    std::printf("## Search paths for NGX feature libraries\n\n");
    for (auto& d : dirs) std::printf("- `%s`\n", d.c_str());

    // 1. Instance extensions NGX wants.
    std::vector<std::string> inst_exts;
    std::printf("\n## Vulkan extensions NGX asks for\n\n| Feature | Instance extensions | Device extensions |\n|---|---|---|\n");
    std::vector<std::vector<std::string>> per_feat_inst(std::size(kFeatures));
    for (size_t i = 0; i < std::size(kFeatures); ++i) {
        auto disc = ngx_discovery(kFeatures[i].id, &finfo.info, wdata.c_str());
        uint32_t n = 0;
        VkExtensionProperties* props = nullptr;
        auto r = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&disc, &n, &props);
        if (NVSDK_NGX_SUCCEED(r))
            for (uint32_t k = 0; k < n; ++k) {
                inst_exts.emplace_back(props[k].extensionName);
                per_feat_inst[i].emplace_back(props[k].extensionName);
            }
        else
            per_feat_inst[i].push_back("query failed " + ngx_result(r));
    }

    VkCtx vk;
    vk.create_instance(inst_exts);
    vk.pick_nvidia();

    // 2. Device extensions + feature requirements per feature.
    std::vector<std::string> dev_exts;
    std::vector<std::vector<std::string>> per_feat_dev(std::size(kFeatures));
    std::vector<NVSDK_NGX_FeatureRequirement> reqs(std::size(kFeatures));
    std::vector<NVSDK_NGX_Result> req_res(std::size(kFeatures));
    for (size_t i = 0; i < std::size(kFeatures); ++i) {
        auto disc = ngx_discovery(kFeatures[i].id, &finfo.info, wdata.c_str());
        uint32_t n = 0;
        VkExtensionProperties* props = nullptr;
        auto r = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(vk.instance, vk.phys, &disc, &n, &props);
        if (NVSDK_NGX_SUCCEED(r))
            for (uint32_t k = 0; k < n; ++k) {
                dev_exts.emplace_back(props[k].extensionName);
                per_feat_dev[i].emplace_back(props[k].extensionName);
            }
        else
            per_feat_dev[i].push_back("query failed " + ngx_result(r));
        req_res[i] = NVSDK_NGX_VULKAN_GetFeatureRequirements(vk.instance, vk.phys, &disc, &reqs[i]);
    }
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (auto& e : v) s += (s.empty() ? "" : ", ") + ("`" + e + "`");
        return s.empty() ? std::string("–") : s;
    };
    for (size_t i = 0; i < std::size(kFeatures); ++i)
        std::printf("| %s | %s | %s |\n", kFeatures[i].name, join(per_feat_inst[i]).c_str(), join(per_feat_dev[i]).c_str());

    vk.create_device(dev_exts);
    std::printf("\n## Device\n\n- GPU: %s (vendor 0x%04X), driver %s, Vulkan %u.%u.%u\n", vk.props.deviceName,
                vk.props.vendorID, vk.driver_version().c_str(), VK_API_VERSION_MAJOR(vk.props.apiVersion),
                VK_API_VERSION_MINOR(vk.props.apiVersion), VK_API_VERSION_PATCH(vk.props.apiVersion));
    std::printf("- Instance extensions enabled: %zu, device extensions enabled: %zu\n", vk.instance_exts.size(),
                vk.device_exts.size());

    std::printf("\n## NGX feature requirements (before init)\n\n| Feature | Query result | Support | Min HW arch | Min OS |\n|---|---|---|---|---|\n");
    for (size_t i = 0; i < std::size(kFeatures); ++i)
        std::printf("| %s | %s | %s | 0x%X | %s |\n", kFeatures[i].name, ngx_result(req_res[i]).c_str(),
                    NVSDK_NGX_SUCCEED(req_res[i]) ? support_bits(reqs[i].FeatureSupported).c_str() : "–",
                    reqs[i].MinHWArchitecture, reqs[i].MinOSVersion);

    // 3. Init NGX.
    auto r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kNgxEngineVersion,
                                                  wdata.c_str(), vk.instance, vk.phys, vk.device,
                                                  vkGetInstanceProcAddr, vkGetDeviceProcAddr, &finfo.info);
    std::printf("\n## NGX init\n\n- `NVSDK_NGX_VULKAN_Init_with_ProjectID`: %s\n", ngx_result(r).c_str());
    if (NVSDK_NGX_FAILED(r)) {
        vk.destroy();
        return 1;
    }

    NVSDK_NGX_Parameter* caps = nullptr;
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&caps);
    std::printf("- `GetCapabilityParameters`: %s\n", ngx_result(r).c_str());

    std::printf("\n## Capability parameters\n\n| Parameter | Value | Get result |\n|---|---|---|\n");
    const char* ints[] = {
        NVSDK_NGX_Parameter_SuperSampling_Available,
        NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
        NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
        NVSDK_NGX_Parameter_FrameGeneration_Available,
        NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver,
        NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMajor,
        NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMinor,
        NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult,
        NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,
        "RayReconstruction.Available",
        "Reserved16.Available",
    };
    for (auto name : ints) {
        int v = -1;
        auto g = caps ? caps->Get(name, &v) : NVSDK_NGX_Result_Fail;
        std::printf("| `%s` | %d | %s |\n", name, v, ngx_result(g).c_str());
    }

    int sr = 0, fg = 0, mfg = 0;
    if (caps) {
        caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &sr);
        caps->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &fg);
        caps->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &mfg);
        NVSDK_NGX_VULKAN_DestroyParameters(caps);
    }
    std::printf("\n## Verdict\n\n- DLSS SR available: **%s**\n- DLSS-G available: **%s**\n- Max generated frames (MultiFrameCountMax): **%d**\n",
                sr ? "yes" : "no", fg ? "yes" : "no", mfg);

    NVSDK_NGX_VULKAN_Shutdown1(vk.device);
    vk.destroy();
    return (sr && fg) ? 0 : 2;
}
