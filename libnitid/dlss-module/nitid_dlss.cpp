// libnitid-dlss.so: Nitid's DLSS module. The only code that uses NVIDIA's DLSS SDK (NGX); libnitid loads it at
// run time through the C table in src/dlss/plugin_api.h (see there for why it is separate).
//
// What it does was proven in the Phase 0 spikes: native Vulkan NGX on Linux (S1), DLSS Super Resolution on our own
// images (S2; jitter is zero for video), DLSS Frame Generation outside a game with a stationary synthetic camera
// (S3) and motion vectors in pixels, current -> previous, mvecScale 1 (F1).

// NGX headers need the Vulkan types (and size_t) first.
#include <cstddef>
#include <vulkan/vulkan.h>

#include <nvsdk_ngx_helpers_dlssg_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>

#include "dlss/plugin_api.h"

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

// Project ID for NGX "custom engine" applications (any stable GUID; this one is Nitid's).
constexpr const char* kProjectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";

// What NGX asked for in spike S1 (same for DLSS SR and DLSS-G on driver 620).
const char* const kInstanceExtensions[] = {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
const char* const kDeviceExtensions[] = {"VK_NVX_binary_import", "VK_NVX_image_view_handle",
                                         VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                         VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};

struct State {
    std::mutex m;
    bool ready = false;
    VkDevice device = VK_NULL_HANDLE;
    void (*log)(int, const char*) = nullptr;
    // NGX keeps pointers to the search paths: keep them alive.
    std::vector<std::wstring> paths;
    std::vector<const wchar_t*> path_ptrs;
    std::wstring data_dir;
    NVSDK_NGX_FeatureCommonInfo info{};
} g;

void say(int level, const char* fmt, ...)
{
    if (!g.log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g.log(level, buf);
}

void NVSDK_CONV ngx_log(const char* msg, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
{
    if (!g.log || !msg) return;
    std::string s = msg;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    g.log(0, ("NGX: " + s).c_str());
}

std::wstring widen(const char* s)
{
    std::wstring w;
    for (; s && *s; ++s) w.push_back(wchar_t(static_cast<unsigned char>(*s)));
    return w;
}

void set_error(char* error, size_t size, const char* what, NVSDK_NGX_Result r)
{
    if (!error || !size) return;
    const char* why = r == NVSDK_NGX_Result_FAIL_FeatureNotSupported   ? " (not supported on this GPU)"
                      : r == NVSDK_NGX_Result_FAIL_FeatureNotFound     ? " (DLSS runtime library not found)"
                      : r == NVSDK_NGX_Result_FAIL_OutOfDate           ? " (driver too old)"
                      : r == NVSDK_NGX_Result_FAIL_InvalidParameter    ? " (invalid parameter)"
                      : r == NVSDK_NGX_Result_FAIL_UnsupportedInputFormat ? " (unsupported format)"
                      : r == NVSDK_NGX_Result_FAIL_OutOfGPUMemory      ? " (out of GPU memory)"
                                                                        : "";
    std::snprintf(error, size, "%s failed: 0x%08X%s", what, unsigned(r), why);
}

NVSDK_NGX_Resource_VK resource(const NitidDlssImage& im, bool rw)
{
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view, im.image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, im.format,
                                                  im.width, im.height, rw);
}

struct Feature {
    NVSDK_NGX_Handle* handle = nullptr;
    NVSDK_NGX_Parameter* params = nullptr;
    bool frame_gen = false;
    uint32_t w = 0, h = 0, mv_w = 0, mv_h = 0;
};

// Stationary pinhole camera: for video, all motion lives in the motion vectors (Veyra's idea, written from scratch
// in spike S3). Row-major, row-vector convention, D3D-style depth [0,1].
void stationary_camera(NVSDK_NGX_DLSSG_Opt_Eval_Params& o, uint32_t w, uint32_t h)
{
    auto ident = [](float m[4][4]) {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) m[r][c] = r == c ? 1.0f : 0.0f;
    };
    ident(o.cameraViewToClip);
    ident(o.clipToCameraView);
    ident(o.clipToLensClip);
    ident(o.clipToPrevClip);
    ident(o.prevClipToClip);
    const float n = 0.1f, f = 1000.0f, aspect = float(w) / float(h);
    const float sx = 1.0f / aspect, sy = 1.0f, a = f / (f - n), b = -n * f / (f - n);  // 90 degrees vertical
    o.cameraViewToClip[0][0] = sx;
    o.cameraViewToClip[1][1] = sy;
    o.cameraViewToClip[2][2] = a;
    o.cameraViewToClip[2][3] = 1.0f;
    o.cameraViewToClip[3][2] = b;
    o.cameraViewToClip[3][3] = 0.0f;
    o.clipToCameraView[0][0] = 1.0f / sx;
    o.clipToCameraView[1][1] = 1.0f / sy;
    o.clipToCameraView[2][2] = 0.0f;
    o.clipToCameraView[2][3] = 1.0f / b;
    o.clipToCameraView[3][2] = 1.0f;
    o.clipToCameraView[3][3] = -a / b;
    o.cameraNear = n;
    o.cameraFar = f;
    o.cameraFOV = 1.5707963f;
    o.cameraAspectRatio = aspect;
    o.cameraPos[0] = o.cameraPos[1] = o.cameraPos[2] = 0.0f;
    o.cameraUp[0] = 0, o.cameraUp[1] = 1, o.cameraUp[2] = 0;
    o.cameraRight[0] = 1, o.cameraRight[1] = 0, o.cameraRight[2] = 0;
    o.cameraFwd[0] = 0, o.cameraFwd[1] = 0, o.cameraFwd[2] = 1;
    o.jitterOffset[0] = o.jitterOffset[1] = 0.0f;
    o.cameraPinholeOffset[0] = o.cameraPinholeOffset[1] = 0.0f;
    o.mvecScale[0] = o.mvecScale[1] = 1.0f;  // vectors are in pixels of the motion image (spike F1)
    o.colorBuffersHDR = false;
    o.depthInverted = false;
    o.cameraMotionIncluded = true;
    o.automodeOverrideReset = false;
    o.notRenderingGameFrames = false;
    o.orthoProjection = false;
    o.motionVectorsInvalidValue = 3.402823466e38f;  // zero is valid (no motion)
    o.motionVectorsDilated = false;
    o.menuDetectionEnabled = false;
}

// --- table entries ----------------------------------------------------------------------------------------------

const char* const* instance_extensions(uint32_t* count)
{
    *count = uint32_t(std::size(kInstanceExtensions));
    return kInstanceExtensions;
}

const char* const* device_extensions(uint32_t* count)
{
    *count = uint32_t(std::size(kDeviceExtensions));
    return kDeviceExtensions;
}

int init(const NitidDlssInitInfo* in, NitidDlssCaps* caps, char* error, size_t error_size)
{
    std::lock_guard l(g.m);
    *caps = {};
    if (g.ready) {
        if (error) std::snprintf(error, error_size, "already initialised");
        return 0;
    }
    g.log = in->log;
    g.device = in->device;
    g.paths.clear();
    for (uint32_t i = 0; i < in->runtime_dir_count; ++i) g.paths.push_back(widen(in->runtime_dirs[i]));
    g.path_ptrs.clear();
    for (auto& p : g.paths) g.path_ptrs.push_back(p.c_str());
    g.data_dir = widen(in->data_dir);
    g.info = {};
    g.info.PathListInfo.Path = g.path_ptrs.data();
    g.info.PathListInfo.Length = unsigned(g.path_ptrs.size());
    g.info.LoggingInfo.LoggingCallback = ngx_log;
    g.info.LoggingInfo.MinimumLoggingLevel = std::getenv("NITID_NGX_LOG") ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE
                                                                           : NVSDK_NGX_LOGGING_LEVEL_OFF;
    g.info.LoggingInfo.DisableOtherLoggingSinks = true;

    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, in->app_version ? in->app_version : "0", g.data_dir.c_str(),
        in->instance, in->phys, in->device, in->get_instance_proc, in->get_device_proc, &g.info);
    if (NVSDK_NGX_FAILED(r)) {
        set_error(error, error_size, "NGX initialisation", r);
        return 0;
    }
    NVSDK_NGX_Parameter* cap = nullptr;
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&cap);
    if (NVSDK_NGX_FAILED(r) || !cap) {
        set_error(error, error_size, "NGX capability query", r);
        NVSDK_NGX_VULKAN_Shutdown1(in->device);
        return 0;
    }
    int sr = 0, fg = 0;
    unsigned mfg = 0;
    cap->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &sr);
    cap->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &fg);
    if (NVSDK_NGX_FAILED(cap->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &mfg))) mfg = 1;
    NVSDK_NGX_VULKAN_DestroyParameters(cap);
    caps->super_res = sr;
    caps->frame_gen = fg;
    caps->max_generated = fg ? int(mfg < 1 ? 1 : mfg) : 0;
    g.ready = true;
    say(0, "DLSS: NGX ready (super resolution %s, frame generation %s, up to %dx)", sr ? "yes" : "no",
        fg ? "yes" : "no", caps->max_generated + 1);
    return 1;
}

void shutdown()
{
    std::lock_guard l(g.m);
    if (!g.ready) return;
    NVSDK_NGX_VULKAN_Shutdown1(g.device);
    g.ready = false;
}

NVSDK_NGX_PerfQuality_Value perf_quality(const NitidDlssSuperResCreate& c)
{
    // The quality mode only picks NVIDIA's default preset / tuning; the sizes are given explicitly.
    const double ratio = double(c.out_width) / double(c.in_width);
    if (ratio <= 1.01) return NVSDK_NGX_PerfQuality_Value_DLAA;
    if (ratio <= 1.55) return NVSDK_NGX_PerfQuality_Value_MaxQuality;   // Quality: 1.5x
    if (ratio <= 1.80) return NVSDK_NGX_PerfQuality_Value_Balanced;     // 1.72x
    if (ratio <= 2.05) return NVSDK_NGX_PerfQuality_Value_MaxPerf;      // Performance: 2x
    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;                // 3x
}

void* super_res_create(VkCommandBuffer cb, const NitidDlssSuperResCreate* c, char* error, size_t error_size)
{
    if (!g.ready) {
        std::snprintf(error, error_size, "DLSS is not initialised");
        return nullptr;
    }
    auto* f = new Feature;
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_AllocateParameters(&f->params);
    if (NVSDK_NGX_FAILED(r)) {
        set_error(error, error_size, "NGX AllocateParameters", r);
        delete f;
        return nullptr;
    }
    if (c->preset > 0)
        for (const char* key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
            NVSDK_NGX_Parameter_SetUI(f->params, key, unsigned(c->preset));
    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth = c->in_width;
    cp.Feature.InHeight = c->in_height;
    cp.Feature.InTargetWidth = c->out_width;
    cp.Feature.InTargetHeight = c->out_height;
    cp.Feature.InPerfQualityValue = perf_quality(*c);
    cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;  // LDR input; vectors at the input size
    r = NGX_VULKAN_CREATE_DLSS_EXT1(g.device, cb, 1, 1, &f->handle, f->params, &cp);
    if (NVSDK_NGX_FAILED(r) || !f->handle) {
        set_error(error, error_size, "DLSS Super Resolution create", r);
        NVSDK_NGX_VULKAN_DestroyParameters(f->params);
        delete f;
        return nullptr;
    }
    f->w = c->out_width, f->h = c->out_height;
    return f;
}

int super_res_evaluate(VkCommandBuffer cb, void* feature, const NitidDlssSuperResEval* e, char* error, size_t error_size)
{
    auto* f = static_cast<Feature*>(feature);
    NVSDK_NGX_Resource_VK color = resource(e->color, false), mv = resource(e->motion, false),
                          depth = resource(e->depth, false), out = resource(e->output, true);
    NVSDK_NGX_VK_DLSS_Eval_Params ep{};
    ep.Feature.pInColor = &color;
    ep.Feature.pInOutput = &out;
    ep.pInDepth = &depth;
    ep.pInMotionVectors = &mv;
    ep.InJitterOffsetX = ep.InJitterOffsetY = 0.0f;  // video frames are not jittered
    ep.InRenderSubrectDimensions = {e->color.width, e->color.height};
    ep.InReset = e->reset;
    ep.InMVScaleX = ep.InMVScaleY = 1.0f;  // pixels at the input size
    NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cb, f->handle, f->params, &ep);
    if (NVSDK_NGX_FAILED(r)) {
        set_error(error, error_size, "DLSS Super Resolution evaluate", r);
        return 0;
    }
    return 1;
}

void* frame_gen_create(VkCommandBuffer cb, const NitidDlssFrameGenCreate* c, char* error, size_t error_size)
{
    if (!g.ready) {
        std::snprintf(error, error_size, "DLSS is not initialised");
        return nullptr;
    }
    auto* f = new Feature;
    f->frame_gen = true;
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_AllocateParameters(&f->params);
    if (NVSDK_NGX_FAILED(r)) {
        set_error(error, error_size, "NGX AllocateParameters", r);
        delete f;
        return nullptr;
    }
    NVSDK_NGX_Parameter_SetUI(f->params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                                  NVSDK_NGX_DLSSG_ResourceFlags_Depth);
    NVSDK_NGX_Parameter_SetUI(f->params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_HUDLess | NVSDK_NGX_DLSSG_ResourceFlags_UI |
                                  NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                                  NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField);
    // Create-time option present in the Linux 310.9.16 library (spike S3): blend candidates instead of the full
    // network; only useful with real motion vectors (F1).
    NVSDK_NGX_Parameter_SetUI(f->params, "DLSSG.LiteCandidateBlendingEnabled", c->lite ? 1u : 0u);
    NVSDK_NGX_DLSSG_Create_Params cp{};
    cp.Width = c->width;
    cp.Height = c->height;
    cp.NativeBackbufferFormat = c->format;
    cp.RenderWidth = c->mv_width;
    cp.RenderHeight = c->mv_height;
    r = NGX_VK_CREATE_DLSSG(cb, 1, 1, &f->handle, f->params, &cp);
    if (NVSDK_NGX_FAILED(r) || !f->handle) {
        set_error(error, error_size, "DLSS Frame Generation create", r);
        NVSDK_NGX_VULKAN_DestroyParameters(f->params);
        delete f;
        return nullptr;
    }
    f->w = c->width, f->h = c->height, f->mv_w = c->mv_width, f->mv_h = c->mv_height;
    return f;
}

int frame_gen_evaluate(VkCommandBuffer cb, void* feature, const NitidDlssFrameGenEval* e, char* error, size_t error_size)
{
    auto* f = static_cast<Feature*>(feature);
    NVSDK_NGX_Resource_VK back = resource(e->backbuffer, false), mv = resource(e->motion, false),
                          depth = resource(e->depth, false), out = resource(e->output, true);
    NVSDK_NGX_VK_DLSSG_Eval_Params ep{};
    ep.pBackbuffer = &back;
    ep.pDepth = &depth;
    ep.pMVecs = &mv;
    ep.pOutputInterpFrame = &out;
    NVSDK_NGX_DLSSG_Opt_Eval_Params op{};
    stationary_camera(op, f->w, f->h);
    op.mvecsSubrectSize = {e->motion.width, e->motion.height};
    op.depthSubrectSize = {e->depth.width, e->depth.height};
    op.backbufferSubrectSize = {f->w, f->h};
    op.outputInterpSubrectSize = {f->w, f->h};
    op.multiFrameCount = e->count;
    op.multiFrameIndex = e->index;
    op.reset = e->reset;
    NVSDK_NGX_Parameter_SetULL(f->params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, e->frame_id);
    NVSDK_NGX_Result r = NGX_VK_EVALUATE_DLSSG(cb, f->handle, f->params, &ep, &op);
    if (NVSDK_NGX_FAILED(r)) {
        set_error(error, error_size, "DLSS Frame Generation evaluate", r);
        return 0;
    }
    return 1;
}

void release(void* feature)
{
    auto* f = static_cast<Feature*>(feature);
    if (!f) return;
    if (f->handle) NVSDK_NGX_VULKAN_ReleaseFeature(f->handle);
    if (f->params) NVSDK_NGX_VULKAN_DestroyParameters(f->params);
    delete f;
}

const NitidDlssApi kApi = {
    NITID_DLSS_ABI,
    NITID_DLSS_SDK_VERSION,
    instance_extensions,
    device_extensions,
    init,
    shutdown,
    super_res_create,
    super_res_evaluate,
    frame_gen_create,
    frame_gen_evaluate,
    release,
};

} // namespace

extern "C" __attribute__((visibility("default"))) const NitidDlssApi* nitid_dlss_api(void) { return &kApi; }
