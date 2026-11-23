// F1: Veyra's approach end to end: hardware optical flow (S4) -> motion vectors -> DLSS-G (S3).
//
// Same test as S3 (rebuild dropped real frames and score them), but DLSS-G now gets real motion
// vectors: VK_NV_optical_flow with input = current rendered frame, reference = previous rendered
// frame, i.e. "current -> previous, in pixels", which is DLSS's motion-vector convention.
// The flow (grid 1x1 or 4x4) is expanded to a full-resolution R16G16_SFLOAT image.
//
// Usage: f1-flow-dlssg <frames.rgba> <w> <h> <n> <step> <out_dir> <mv: zero|nvof1|nvof4> [sign=+1|-1] [lite=0|1] [save_every=0] [mvscale=px|norm]
//   mvscale px: mvecScale = 1 (vectors already in pixels, per the DLSS-FG guide)
//   mvscale norm: mvecScale = 1/width, 1/height (per the header comment "normalize ... to [-1,1]")

#include "dlssg_util.hpp"
#include "ngx_session.hpp"
#include "nvof.hpp"
#include "ppm.hpp"
#include "vk_image.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace spike;

static uint16_t to_half(float f)
{
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000, mant = x & 0x7fffff;
    int exp = int((x >> 23) & 0xff) - 127 + 15;
    if (exp <= 0) return uint16_t(sign);
    if (exp >= 31) return uint16_t(sign | 0x7c00);
    return uint16_t(sign | (uint32_t(exp) << 10) | (mant >> 13));
}

int main(int argc, char** argv)
{
    if (argc < 8) {
        std::fprintf(stderr, "usage: %s frames.rgba w h n step out_dir zero|nvof1|nvof4 [sign] [lite] [save_every]\n", argv[0]);
        return 2;
    }
    std::string path = argv[1], outdir = argv[6], mvmode = argv[7];
    uint32_t w = std::stoul(argv[2]), h = std::stoul(argv[3]);
    size_t nframes = std::stoul(argv[4]);
    uint32_t step = std::stoul(argv[5]);
    float sign = argc > 8 ? std::stof(argv[8]) : 1.0f;
    unsigned lite = argc > 9 ? unsigned(std::stoul(argv[9])) : 0u;
    int save_every = argc > 10 ? std::stoi(argv[10]) : 0;
    std::string mvscale = argc > 11 ? argv[11] : "px";
    const size_t fbytes = size_t(w) * h * 4, px = size_t(w) * h;

    std::vector<uint8_t> frames(fbytes * nframes);
    {
        std::ifstream f(path, std::ios::binary);
        f.read(reinterpret_cast<char*>(frames.data()), std::streamsize(frames.size()));
        if (size_t(f.gcount()) != frames.size()) {
            std::fprintf(stderr, "short read\n");
            return 1;
        }
    }
    auto frame = [&](size_t i) { return frames.data() + i * fbytes; };
    std::vector<std::vector<uint8_t>> lum(nframes);
    for (size_t i = 0; i < nframes; ++i) lum[i] = luma(std::vector<uint8_t>(frame(i), frame(i) + fbytes), px);

    VkPhysicalDeviceOpticalFlowFeaturesNV off{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    NgxSession s;
    s.init({NVSDK_NGX_Feature_FrameGeneration}, std::string(NITID_ROOT) + "/results/f1-flow-dlssg/ngx", false,
           {VK_NV_OPTICAL_FLOW_EXTENSION_NAME}, &off, VK_QUEUE_OPTICAL_FLOW_BIT_NV);
    auto& vk = s.vk;
    nvof_load_functions(vk.device);
    OfRunner of(vk, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16_S10_5_NV);
    auto grid = mvmode == "nvof1" ? VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV : VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV;
    auto perf = mvmode == "nvof1" ? VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV : VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_NV;

    const uint32_t count = step - 1;
    std::printf("F1 %ux%u step %u (x%u) mv=%s sign=%+.0f lite=%u\n", w, h, step, step, mvmode.c_str(), sign, lite);

    Image back = make_image(vk, w, h, VK_FORMAT_R8G8B8A8_UNORM);
    Image interp = make_image(vk, w, h, VK_FORMAT_R8G8B8A8_UNORM);
    Image mvec = make_image(vk, w, h, VK_FORMAT_R16G16_SFLOAT);
    Image depth = make_image(vk, w, h, VK_FORMAT_R32_SFLOAT);
    Buffer up = make_host_buffer(vk, fbytes), down = make_host_buffer(vk, fbytes), mvup = make_host_buffer(vk, px * 4);

    NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                                  NVSDK_NGX_DLSSG_ResourceFlags_Depth);
    NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_HUDLess | NVSDK_NGX_DLSSG_ResourceFlags_UI |
                                  NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                                  NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField);
    NVSDK_NGX_Parameter_SetUI(s.params, "DLSSG.LiteCandidateBlendingEnabled", lite);
    NVSDK_NGX_DLSSG_Create_Params cp{};
    cp.Width = w;
    cp.Height = h;
    cp.NativeBackbufferFormat = VK_FORMAT_R8G8B8A8_UNORM;
    cp.RenderWidth = w;
    cp.RenderHeight = h;
    NVSDK_NGX_Handle* hdl = nullptr;
    {
        auto cb = vk.begin();
        cmd_clear(cb, mvec, 0, 0);
        cmd_clear(cb, depth, 0.5f);
        cmd_clear(cb, interp, 0, 0, 0, 1);
        auto r = NGX_VK_CREATE_DLSSG(cb, 1, 1, &hdl, s.params, &cp);
        vk.submit_wait(cb);
        if (NVSDK_NGX_FAILED(r) || !hdl) {
            std::printf("create DLSS-G failed: %s\n", ngx_result(r).c_str());
            return 1;
        }
    }

    std::vector<double> ps_gen, ps_blend, flow_ms;
    std::vector<uint8_t> blend(fbytes);
    unsigned long long fid = 0;
    size_t groups = 0;
    for (size_t r = 0; r < nframes; r += step) {
        // Motion vectors for this rendered frame: current (r) -> previous rendered frame (r - step).
        auto* mv = static_cast<uint16_t*>(mvup.map);
        if (r > 0 && mvmode != "zero") {
            std::vector<double> ms;
            Flow f = of.run(lum[r], lum[r - step], w, h, grid, perf, 1, &ms);
            flow_ms.push_back(ms[0]);
            for (uint32_t j = 0; j < h; ++j)
                for (uint32_t i = 0; i < w; ++i) {
                    size_t k = size_t(std::min(j / f.grid, f.h - 1)) * f.w + std::min(i / f.grid, f.w - 1);
                    size_t o = (size_t(j) * w + i) * 2;
                    mv[o] = to_half(sign * f.fx[k]);
                    mv[o + 1] = to_half(sign * f.fy[k]);
                }
        } else {
            std::memset(mvup.map, 0, px * 4);
        }
        std::memcpy(up.map, frame(r), fbytes);
        for (uint32_t k = 1; k <= count; ++k) {
            auto cb = vk.begin();
            if (k == 1) {
                cmd_upload(cb, up, back);
                cmd_upload(cb, mvup, mvec);
            }
            to_layout(cb, interp, VK_IMAGE_LAYOUT_GENERAL);
            auto rb = ngx_res(back, false), rm = ngx_res(mvec, false), rd = ngx_res(depth, false), ro = ngx_res(interp, true);
            NVSDK_NGX_VK_DLSSG_Eval_Params ep{};
            ep.pBackbuffer = &rb;
            ep.pDepth = &rd;
            ep.pMVecs = &rm;
            ep.pOutputInterpFrame = &ro;
            NVSDK_NGX_DLSSG_Opt_Eval_Params op{};
            synthetic_camera(op, w, h);
            if (mvscale == "px") op.mvecScale[0] = op.mvecScale[1] = 1.0f;
            op.multiFrameCount = count;
            op.multiFrameIndex = k;
            op.reset = r == 0;
            NVSDK_NGX_Parameter_SetULL(s.params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, fid);
            auto res = NGX_VK_EVALUATE_DLSSG(cb, hdl, s.params, &ep, &op);
            if (NVSDK_NGX_FAILED(res)) {
                std::printf("evaluate failed: %s\n", ngx_result(res).c_str());
                return 1;
            }
            cmd_download(cb, interp, down);
            vk.submit_wait(cb);
            if (r == 0) continue;
            size_t prev = r - step, real = prev + k;
            const float t = float(k) / float(step);
            for (size_t i = 0; i < fbytes; ++i)
                blend[i] = uint8_t(std::lround((1 - t) * frame(prev)[i] + t * frame(r)[i]));
            ps_gen.push_back(luma_psnr(static_cast<uint8_t*>(down.map), frame(real), px));
            ps_blend.push_back(luma_psnr(blend.data(), frame(real), px));
            if (save_every > 0 && groups % size_t(save_every) == 0) {
                char p[512];
                std::snprintf(p, sizeof p, "%s/gen_%s_lite%u_real%03zu_step%u.ppm", outdir.c_str(), mvmode.c_str(), lite, real, step);
                write_ppm_rgba(p, static_cast<uint8_t*>(down.map), w, h);
                std::snprintf(p, sizeof p, "%s/real%03zu.ppm", outdir.c_str(), real);
                write_ppm_rgba(p, frame(real), w, h);
            }
        }
        ++fid;
        ++groups;
    }
    auto mean = [](const std::vector<double>& v) {
        double a = 0;
        for (double x : v) a += x;
        return v.empty() ? 0.0 : a / double(v.size());
    };
    std::sort(flow_ms.begin(), flow_ms.end());
    std::printf("RESULT step=%u mv=%s mvscale=%s sign=%+.0f lite=%u gen=%zu mean_psnr_dlssg=%.2f blend=%.2f flow_ms_median=%.3f\n", step,
                mvmode.c_str(), mvscale.c_str(), sign, lite, ps_gen.size(), mean(ps_gen), mean(ps_blend),
                flow_ms.empty() ? 0.0 : flow_ms[flow_ms.size() / 2]);

    NVSDK_NGX_VULKAN_ReleaseFeature(hdl);
    destroy(vk, back);
    destroy(vk, interp);
    destroy(vk, mvec);
    destroy(vk, depth);
    destroy(vk, up);
    destroy(vk, down);
    destroy(vk, mvup);
    s.shutdown();
    return 0;
}
