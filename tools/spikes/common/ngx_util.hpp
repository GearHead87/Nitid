// NGX helpers shared by the spikes: logging, result strings, feature info with search paths,
// and Vulkan extension discovery for a set of features.
#pragma once

// NGX headers need the Vulkan types (and size_t) declared first.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vulkan/vulkan.h>

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <vector>

namespace spike {

// Project ID for NGX "custom engine" initialisation (any GUID-like string works for development).
inline constexpr const char* kNgxProjectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
inline constexpr const char* kNgxEngineVersion = "0.1.0";

inline void NVSDK_CONV ngx_log(const char* msg, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature src)
{
    std::fprintf(stderr, "[ngx f=%d] %s", int(src), msg);
    size_t n = std::char_traits<char>::length(msg);
    if (n == 0 || msg[n - 1] != '\n')
        std::fputc('\n', stderr);
}

inline std::string ngx_result(NVSDK_NGX_Result r)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "0x%08X%s", unsigned(r), NVSDK_NGX_SUCCEED(r) ? " (ok)" : " (FAIL)");
    std::string s = buf;
    if (r == NVSDK_NGX_Result_FAIL_FeatureNotSupported) s += " FeatureNotSupported";
    if (r == NVSDK_NGX_Result_FAIL_PlatformError) s += " PlatformError";
    if (r == NVSDK_NGX_Result_FAIL_FeatureNotFound) s += " FeatureNotFound";
    if (r == NVSDK_NGX_Result_FAIL_InvalidParameter) s += " InvalidParameter";
    if (r == NVSDK_NGX_Result_FAIL_NotInitialized) s += " NotInitialized";
    if (r == NVSDK_NGX_Result_FAIL_UnsupportedInputFormat) s += " UnsupportedInputFormat";
    if (r == NVSDK_NGX_Result_FAIL_OutOfDate) s += " OutOfDate";
    return s;
}

// Holds the wide-string search paths so the pointers inside NVSDK_NGX_FeatureCommonInfo stay valid.
struct NgxFeatureInfo {
    std::vector<std::wstring> paths;
    std::vector<const wchar_t*> ptrs;
    NVSDK_NGX_FeatureCommonInfo info{};

    explicit NgxFeatureInfo(const std::vector<std::string>& dirs, bool verbose)
    {
        for (auto& d : dirs) paths.emplace_back(d.begin(), d.end());
        for (auto& p : paths) ptrs.push_back(p.c_str());
        info.PathListInfo.Path = ptrs.data();
        info.PathListInfo.Length = unsigned(ptrs.size());
        info.LoggingInfo.LoggingCallback = ngx_log;
        info.LoggingInfo.MinimumLoggingLevel = verbose ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_ON;
        info.LoggingInfo.DisableOtherLoggingSinks = true;
    }
};

inline NVSDK_NGX_FeatureDiscoveryInfo ngx_discovery(NVSDK_NGX_Feature f, const NVSDK_NGX_FeatureCommonInfo* info,
                                                    const wchar_t* data_path)
{
    NVSDK_NGX_FeatureDiscoveryInfo d{};
    d.SDKVersion = NVSDK_NGX_Version_API;
    d.FeatureID = f;
    d.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc.ProjectId = kNgxProjectId;
    d.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d.Identifier.v.ProjectDesc.EngineVersion = kNgxEngineVersion;
    d.ApplicationDataPath = data_path;
    d.FeatureInfo = info;
    return d;
}

}  // namespace spike
