// S3: Can DLSS Frame Generation (DLSS-G) generate frames outside a game, in our own Vulkan app?
//
// Veyra's approach, written from scratch for Vulkan: plain video frames as the "backbuffer",
// a stationary synthetic camera (fixed projection, identity clip-to-prev-clip), flat depth and
// ZERO motion vectors (S4/F1 add real estimated motion). The app owns presentation, so we just
// read the generated frames back and score them.
//
// Test: take every `step`-th real frame as a "rendered" frame, ask DLSS-G for step-1 generated
// frames in between (multiFrameCount = step-1), and compare each generated frame with the real
// frame that was dropped (luma PSNR). Baselines: repeat previous rendered frame, linear blend.
//
// Input: raw RGBA8 frames (ffmpeg -pix_fmt rgba -f rawvideo).
// Usage: s3-dlssg-standalone <frames.rgba> <w> <h> <num_frames> <step 2..6> <out_dir> [save_every=0] [lite=0|1]
//   lite=1 sets the create-time parameter "DLSSG.LiteCandidateBlendingEnabled" (found in the 310.9.16
//   runtime; a faster "performance mode", per Veyra's Windows notes).

#include "ngx_session.hpp"
#include "ppm.hpp"
#include "vk_image.hpp"

#include <nvsdk_ngx_helpers_dlssg_vk.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace spike;

static double luma_psnr(const uint8_t* a, const uint8_t* b, size_t px)
{
    double se = 0;
    for (size_t i = 0; i < px; ++i) {
        double ya = 0.299 * a[i * 4] + 0.587 * a[i * 4 + 1] + 0.114 * a[i * 4 + 2];
        double yb = 0.299 * b[i * 4] + 0.587 * b[i * 4 + 1] + 0.114 * b[i * 4 + 2];
        se += (ya - yb) * (ya - yb);
    }
    double mse = se / double(px);
    return mse <= 1e-10 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

static void synthetic_camera(NVSDK_NGX_DLSSG_Opt_Eval_Params& o, uint32_t w, uint32_t h)
{
    // Stationary pinhole camera: all motion lives in the motion-vector buffer.
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
    const float fov = 1.5707963f;  // 90 degrees vertical: tan(fov/2) = 1
    // Row-major, post-multiplication (row vector * matrix), D3D-style depth [0,1].
    const float sx = 1.0f / aspect, sy = 1.0f, a = f / (f - n), b = -n * f / (f - n);
    o.cameraViewToClip[0][0] = sx;
    o.cameraViewToClip[1][1] = sy;
    o.cameraViewToClip[2][2] = a;
    o.cameraViewToClip[2][3] = 1.0f;
    o.cameraViewToClip[3][2] = b;
    o.cameraViewToClip[3][3] = 0.0f;
    // Inverse of the above.
    o.clipToCameraView[0][0] = 1.0f / sx;
    o.clipToCameraView[1][1] = 1.0f / sy;
    o.clipToCameraView[2][2] = 0.0f;
    o.clipToCameraView[2][3] = 1.0f / b;
    o.clipToCameraView[3][2] = 1.0f;
    o.clipToCameraView[3][3] = -a / b;
    o.cameraNear = n;
    o.cameraFar = f;
    o.cameraFOV = fov;
    o.cameraAspectRatio = aspect;
    o.cameraPos[0] = o.cameraPos[1] = o.cameraPos[2] = 0.0f;
    o.cameraUp[0] = 0; o.cameraUp[1] = 1; o.cameraUp[2] = 0;
    o.cameraRight[0] = 1; o.cameraRight[1] = 0; o.cameraRight[2] = 0;
    o.cameraFwd[0] = 0; o.cameraFwd[1] = 0; o.cameraFwd[2] = 1;
    o.jitterOffset[0] = o.jitterOffset[1] = 0.0f;
    o.cameraPinholeOffset[0] = o.cameraPinholeOffset[1] = 0.0f;
    o.mvecScale[0] = 1.0f / float(w);  // MVs in pixels -> normalised
    o.mvecScale[1] = 1.0f / float(h);
    o.colorBuffersHDR = false;
    o.depthInverted = false;
    o.cameraMotionIncluded = true;
    o.automodeOverrideReset = false;
    o.notRenderingGameFrames = false;
    o.orthoProjection = false;
    o.motionVectorsInvalidValue = 3.402823466e38f;  // zero is valid (no motion)
    o.motionVectorsDilated = false;
    o.menuDetectionEnabled = false;
    o.mvecsSubrectSize = {w, h};
    o.depthSubrectSize = {w, h};
    o.backbufferSubrectSize = {w, h};
    o.outputInterpSubrectSize = {w, h};
}

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::fprintf(stderr, "usage: %s frames.rgba w h n step out_dir [save_every]\n", argv[0]);
        return 2;
    }
    std::string path = argv[1], outdir = argv[6];
    uint32_t w = std::stoul(argv[2]), h = std::stoul(argv[3]);
    size_t nframes = std::stoul(argv[4]);
    uint32_t step = std::stoul(argv[5]);
    int save_every = argc > 7 ? std::stoi(argv[7]) : 0;
    unsigned lite = argc > 8 ? unsigned(std::stoul(argv[8])) : 0u;
    const size_t fbytes = size_t(w) * h * 4;

    std::vector<uint8_t> frames(fbytes * nframes);
    {
        std::ifstream f(path, std::ios::binary);
        f.read(reinterpret_cast<char*>(frames.data()), std::streamsize(frames.size()));
        if (size_t(f.gcount()) != frames.size()) {
            std::fprintf(stderr, "short read: %zd of %zu\n", f.gcount(), frames.size());
            return 1;
        }
    }
    auto frame = [&](size_t i) { return frames.data() + i * fbytes; };

    NgxSession s;
    s.init({NVSDK_NGX_Feature_FrameGeneration}, std::string(NITID_ROOT) + "/results/s3-dlssg-standalone/ngx");
    auto& vk = s.vk;

    int mfc_max = 0;
    {
        NVSDK_NGX_Parameter* caps = nullptr;
        NVSDK_NGX_VULKAN_GetCapabilityParameters(&caps);
        if (caps) {
            caps->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &mfc_max);
            NVSDK_NGX_VULKAN_DestroyParameters(caps);
        }
    }
    const uint32_t count = step - 1;
    std::printf("DLSS-G %ux%u, step %u -> multiFrameCount %u (max %d)\n", w, h, step, count, mfc_max);
    if (count < 1 || int(count) > mfc_max) {
        std::printf("unsupported multiplier\n");
        return 1;
    }

    Image back = make_image(vk, w, h, VK_FORMAT_R8G8B8A8_UNORM);
    Image interp = make_image(vk, w, h, VK_FORMAT_R8G8B8A8_UNORM);
    Image mvec = make_image(vk, w, h, VK_FORMAT_R16G16_SFLOAT);
    Image depth = make_image(vk, w, h, VK_FORMAT_R32_SFLOAT);
    Buffer up = make_host_buffer(vk, fbytes), down = make_host_buffer(vk, fbytes);
    Timer timer;
    timer.init(vk);

    NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                                  NVSDK_NGX_DLSSG_ResourceFlags_Depth);
    NVSDK_NGX_Parameter_SetUI(s.params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags,
                              NVSDK_NGX_DLSSG_ResourceFlags_HUDLess | NVSDK_NGX_DLSSG_ResourceFlags_UI |
                                  NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                                  NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField);
    NVSDK_NGX_Parameter_SetUI(s.params, "DLSSG.LiteCandidateBlendingEnabled", lite);
    std::printf("DLSSG.LiteCandidateBlendingEnabled = %u\n", lite);
    NVSDK_NGX_DLSSG_Create_Params cp{};
    cp.Width = w;
    cp.Height = h;
    cp.NativeBackbufferFormat = VK_FORMAT_R8G8B8A8_UNORM;
    cp.RenderWidth = w;
    cp.RenderHeight = h;
    cp.DynamicResolutionScaling = false;

    NVSDK_NGX_Handle* hdl = nullptr;
    {
        auto cb = vk.begin();
        cmd_clear(cb, mvec, 0, 0);
        cmd_clear(cb, depth, 0.5f);
        cmd_clear(cb, interp, 0, 0, 0, 1);
        auto r = NGX_VK_CREATE_DLSSG(cb, 1, 1, &hdl, s.params, &cp);
        std::printf("create DLSS-G: %s\n", ngx_result(r).c_str());
        vk.submit_wait(cb);
        if (NVSDK_NGX_FAILED(r) || !hdl) {
            int ir = -1;
            s.params->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &ir);
            std::printf("FeatureInitResult: %d\n", ir);
            return 1;
        }
    }

    std::vector<double> ps_gen, ps_rep, ps_blend, ms;
    std::vector<std::vector<double>> ps_by_index(count);
    std::vector<uint8_t> blend(fbytes);
    unsigned long long frame_id = 0;
    size_t groups = 0;
    for (size_t r = 0; r + step < nframes + step && r < nframes; r += step) {
        // r = index of the current rendered frame; previous rendered frame = r - step.
        std::memcpy(up.map, frame(r), fbytes);
        bool reset = r == 0;
        for (uint32_t k = 1; k <= count; ++k) {
            auto cb = vk.begin();
            if (k == 1) cmd_upload(cb, up, back);
            to_layout(cb, interp, VK_IMAGE_LAYOUT_GENERAL);
            auto rb = ngx_res(back, false), rm = ngx_res(mvec, false), rd = ngx_res(depth, false),
                 ro = ngx_res(interp, true);
            NVSDK_NGX_VK_DLSSG_Eval_Params ep{};
            ep.pBackbuffer = &rb;
            ep.pDepth = &rd;
            ep.pMVecs = &rm;
            ep.pOutputInterpFrame = &ro;
            NVSDK_NGX_DLSSG_Opt_Eval_Params op{};
            synthetic_camera(op, w, h);
            op.multiFrameCount = count;
            op.multiFrameIndex = k;
            op.reset = reset;
            NVSDK_NGX_Parameter_SetULL(s.params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, frame_id);
            timer.begin(cb);
            auto res = NGX_VK_EVALUATE_DLSSG(cb, hdl, s.params, &ep, &op);
            timer.end(cb);
            if (NVSDK_NGX_FAILED(res)) {
                std::printf("evaluate (frame %zu, index %u) failed: %s\n", r, k, ngx_result(res).c_str());
                return 1;
            }
            cmd_download(cb, interp, down);
            vk.submit_wait(cb);
            if (r == 0) continue;  // first frame: nothing to interpolate from
            ms.push_back(timer.ms(vk));
            size_t prev = r - step, real = prev + k;
            const uint8_t* g = static_cast<uint8_t*>(down.map);
            const float t = float(k) / float(step);
            for (size_t i = 0; i < fbytes; ++i)
                blend[i] = uint8_t(std::lround((1 - t) * frame(prev)[i] + t * frame(r)[i]));
            double pg = luma_psnr(g, frame(real), size_t(w) * h);
            ps_gen.push_back(pg);
            ps_by_index[k - 1].push_back(pg);
            ps_rep.push_back(luma_psnr(frame(prev), frame(real), size_t(w) * h));
            ps_blend.push_back(luma_psnr(blend.data(), frame(real), size_t(w) * h));
            if (save_every > 0 && groups % size_t(save_every) == 0) {
                char p[512];
                std::snprintf(p, sizeof p, "%s/gen_real%03zu_step%u.ppm", outdir.c_str(), real, step);
                write_ppm_rgba(p, g, w, h);
                std::snprintf(p, sizeof p, "%s/real%03zu.ppm", outdir.c_str(), real);
                write_ppm_rgba(p, frame(real), w, h);
                std::snprintf(p, sizeof p, "%s/blend_real%03zu_step%u.ppm", outdir.c_str(), real, step);
                write_ppm_rgba(p, blend.data(), w, h);
            }
        }
        ++frame_id;
        ++groups;
    }

    auto mean = [](const std::vector<double>& v) {
        double s = 0;
        for (double x : v) s += x;
        return v.empty() ? 0.0 : s / double(v.size());
    };
    auto changed = [&]() {  // sanity: generated frames are not just copies of an input
        size_t n = 0;
        for (size_t i = 0; i < ps_gen.size(); ++i) n += std::abs(ps_gen[i] - ps_rep[i]) > 0.01;
        return n;
    };
    std::printf("RESULT step=%u gen=%zu mean_psnr_dlssg=%.2f repeat=%.2f blend=%.2f mean_ms=%.3f differs_from_repeat=%zu\n",
                step, ps_gen.size(), mean(ps_gen), mean(ps_rep), mean(ps_blend), mean(ms), changed());
    for (uint32_t k = 0; k < count; ++k)
        std::printf("RESULT_INDEX step=%u index=%u mean_psnr=%.2f\n", step, k + 1, mean(ps_by_index[k]));

    // Timing pass: the quality pass submits and waits after every evaluation (GPU idles between,
    // clocks may drop). Here B groups go into one command buffer with no read-back, each
    // evaluation timestamped, so the GPU stays busy like in a real-time pipeline.
    {
        const uint32_t B = 8;
        std::vector<Buffer> stage(B);
        for (auto& b : stage) b = make_host_buffer(vk, fbytes);
        VkQueryPool qp;
        VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qci.queryCount = 2 * B * count;
        VK_CHECK(vkCreateQueryPool(vk.device, &qci, nullptr, &qp));
        std::vector<double> evals;
        double wall_ms = 0;
        size_t wall_gen = 0;
        unsigned long long fid = 1000;  // new frame-id range; reset on first eval
        bool first = true;
        for (size_t base = 0; base + size_t(B) * step <= nframes; base += size_t(B) * step) {
            for (uint32_t j = 0; j < B; ++j) std::memcpy(stage[j].map, frame(base + j * step), fbytes);
            auto cb = vk.begin();
            vkCmdResetQueryPool(cb, qp, 0, 2 * B * count);
            for (uint32_t j = 0; j < B; ++j) {
                cmd_upload(cb, stage[j], back);
                for (uint32_t k = 1; k <= count; ++k) {
                    to_layout(cb, interp, VK_IMAGE_LAYOUT_GENERAL);
                    auto rb = ngx_res(back, false), rm = ngx_res(mvec, false), rd = ngx_res(depth, false),
                         ro = ngx_res(interp, true);
                    NVSDK_NGX_VK_DLSSG_Eval_Params ep{};
                    ep.pBackbuffer = &rb;
                    ep.pDepth = &rd;
                    ep.pMVecs = &rm;
                    ep.pOutputInterpFrame = &ro;
                    NVSDK_NGX_DLSSG_Opt_Eval_Params op{};
                    synthetic_camera(op, w, h);
                    op.multiFrameCount = count;
                    op.multiFrameIndex = k;
                    op.reset = first;
                    NVSDK_NGX_Parameter_SetULL(s.params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, fid);
                    uint32_t q = 2 * (j * count + (k - 1));
                    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, q);
                    NGX_VK_EVALUATE_DLSSG(cb, hdl, s.params, &ep, &op);
                    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, q + 1);
                }
                first = false;
                ++fid;
            }
            auto t0 = std::chrono::steady_clock::now();
            vk.submit_wait(cb);
            auto t1 = std::chrono::steady_clock::now();
            if (base > 0) {  // skip the first batch (warm-up)
                wall_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
                wall_gen += size_t(B) * count;
                std::vector<uint64_t> ts(2 * B * count);
                VK_CHECK(vkGetQueryPoolResults(vk.device, qp, 0, uint32_t(ts.size()), ts.size() * 8, ts.data(), 8,
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
                for (size_t i = 0; i < ts.size(); i += 2)
                    evals.push_back(double(ts[i + 1] - ts[i]) * vk.props.limits.timestampPeriod / 1e6);
            }
        }
        std::sort(evals.begin(), evals.end());
        if (!evals.empty())
            std::printf("TIMING step=%u batched_evals=%zu gpu_ms_median=%.3f gpu_ms_min=%.3f gpu_ms_p90=%.3f "
                        "wall_ms_per_generated_frame=%.3f (includes upload of the real frame)\n",
                        step, evals.size(), evals[evals.size() / 2], evals.front(), evals[evals.size() * 9 / 10],
                        wall_ms / double(wall_gen));
        vkDestroyQueryPool(vk.device, qp, nullptr);
        for (auto& b : stage) destroy(vk, b);
    }

    NVSDK_NGX_VULKAN_ReleaseFeature(hdl);
    destroy(vk, back);
    destroy(vk, interp);
    destroy(vk, mvec);
    destroy(vk, depth);
    destroy(vk, up);
    destroy(vk, down);
    timer.destroy(vk);
    s.shutdown();
    return 0;
}
