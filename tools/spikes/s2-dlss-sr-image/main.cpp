// S2: Can our own Vulkan app upscale with DLSS Super Resolution?
//
// Input: a folder from make_jittered.py (frame_XX.ppm low-res renders with Halton jitter,
// jitter.txt, reference.ppm). Static scene, so motion vectors are zero and depth is flat.
// Runs DLSS SR on every frame in order (it accumulates detail over frames), times each
// evaluation with GPU timestamps, and writes the output after frame 1 and after the last frame.
//
// Usage: s2-dlss-sr-image <in_dir> <out_prefix> <out_w> <out_h> <preset K|M|L|J> <mode perf|balanced|quality|dlaa> [jitter_sign=+1|-1]

#include "ngx_session.hpp"
#include "ppm.hpp"
#include "vk_image.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace spike;

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::fprintf(stderr, "usage: %s in_dir out_prefix out_w out_h preset mode [jitter_sign]\n", argv[0]);
        return 2;
    }
    std::string in = argv[1], outp = argv[2];
    uint32_t ow = std::stoul(argv[3]), oh = std::stoul(argv[4]);
    char preset_c = argv[5][0];
    std::string mode = argv[6];
    float jsign = argc > 7 ? std::stof(argv[7]) : 1.0f;

    std::vector<std::pair<float, float>> jit;
    {
        std::ifstream jf(in + "/jitter.txt");
        float x, y;
        while (jf >> x >> y) jit.emplace_back(x, y);
    }
    uint32_t iw, ih;
    auto first = read_ppm_rgba(in + "/frame_00.ppm", iw, ih);

    NgxSession s;
    s.init({NVSDK_NGX_Feature_SuperSampling}, std::string(NITID_ROOT) + "/results/s2-dlss-sr-image/ngx");
    auto& vk = s.vk;

    Image color = make_image(vk, iw, ih, VK_FORMAT_R8G8B8A8_UNORM);
    Image mvec = make_image(vk, iw, ih, VK_FORMAT_R16G16_SFLOAT);
    Image depth = make_image(vk, iw, ih, VK_FORMAT_R32_SFLOAT);
    Image out = make_image(vk, ow, oh, VK_FORMAT_R8G8B8A8_UNORM);
    Buffer up = make_host_buffer(vk, VkDeviceSize(iw) * ih * 4);
    Buffer down = make_host_buffer(vk, VkDeviceSize(ow) * oh * 4);
    Timer timer;
    timer.init(vk);

    NVSDK_NGX_PerfQuality_Value pq = mode == "perf" ? NVSDK_NGX_PerfQuality_Value_MaxPerf
                                   : mode == "balanced" ? NVSDK_NGX_PerfQuality_Value_Balanced
                                   : mode == "dlaa" ? NVSDK_NGX_PerfQuality_Value_DLAA
                                                    : NVSDK_NGX_PerfQuality_Value_MaxQuality;
    NVSDK_NGX_DLSS_Hint_Render_Preset preset = preset_c == 'M' ? NVSDK_NGX_DLSS_Hint_Render_Preset_M
                                             : preset_c == 'L' ? NVSDK_NGX_DLSS_Hint_Render_Preset_L
                                             : preset_c == 'J' ? NVSDK_NGX_DLSS_Hint_Render_Preset_J
                                                               : NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    for (const char* key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
        NVSDK_NGX_Parameter_SetUI(s.params, key, preset);

    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth = iw;
    cp.Feature.InHeight = ih;
    cp.Feature.InTargetWidth = ow;
    cp.Feature.InTargetHeight = oh;
    cp.Feature.InPerfQualityValue = pq;
    cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;  // LDR input, MVs at render res

    NVSDK_NGX_Handle* h = nullptr;
    {
        auto cb = vk.begin();
        cmd_clear(cb, mvec, 0, 0);
        cmd_clear(cb, depth, 0.5f);
        cmd_clear(cb, out, 0, 0, 0, 1);
        auto r = NGX_VULKAN_CREATE_DLSS_EXT1(vk.device, cb, 1, 1, &h, s.params, &cp);
        std::printf("create DLSS SR %ux%u -> %ux%u preset %c mode %s: %s\n", iw, ih, ow, oh, preset_c, mode.c_str(),
                    ngx_result(r).c_str());
        vk.submit_wait(cb);
        if (NVSDK_NGX_FAILED(r)) return 1;
    }

    std::vector<double> times;
    for (size_t k = 0; k < jit.size(); ++k) {
        char name[64];
        std::snprintf(name, sizeof name, "/frame_%02zu.ppm", k);
        uint32_t w2, h2;
        auto px = read_ppm_rgba(in + name, w2, h2);
        std::memcpy(up.map, px.data(), px.size());
        auto cb = vk.begin();
        cmd_upload(cb, up, color);
        to_layout(cb, out, VK_IMAGE_LAYOUT_GENERAL);
        auto rc = ngx_res(color, false), rm = ngx_res(mvec, false), rd = ngx_res(depth, false), ro = ngx_res(out, true);
        NVSDK_NGX_VK_DLSS_Eval_Params ep{};
        ep.Feature.pInColor = &rc;
        ep.Feature.pInOutput = &ro;
        ep.pInDepth = &rd;
        ep.pInMotionVectors = &rm;
        ep.InJitterOffsetX = jsign * jit[k].first;
        ep.InJitterOffsetY = jsign * jit[k].second;
        ep.InRenderSubrectDimensions = {iw, ih};
        ep.InReset = k == 0;
        ep.InMVScaleX = ep.InMVScaleY = 1.0f;
        timer.begin(cb);
        auto r = NGX_VULKAN_EVALUATE_DLSS_EXT(cb, h, s.params, &ep);
        timer.end(cb);
        if (NVSDK_NGX_FAILED(r)) {
            std::printf("evaluate frame %zu failed: %s\n", k, ngx_result(r).c_str());
            return 1;
        }
        if (k == 0 || k + 1 == jit.size()) cmd_download(cb, out, down);
        vk.submit_wait(cb);
        times.push_back(timer.ms(vk));
        if (k == 0 || k + 1 == jit.size()) {
            std::string p = outp + (k == 0 ? "_frame01.ppm" : "_final.ppm");
            write_ppm_rgba(p, static_cast<uint8_t*>(down.map), ow, oh);
        }
    }
    double sum = 0;
    for (size_t k = 4; k < times.size(); ++k) sum += times[k];
    std::printf("frames: %zu, first eval: %.2f ms, mean eval (frames 5..%zu): %.3f ms\n", times.size(), times[0],
                times.size(), sum / double(times.size() - 4));

    NVSDK_NGX_VULKAN_ReleaseFeature(h);
    destroy(vk, color);
    destroy(vk, mvec);
    destroy(vk, depth);
    destroy(vk, out);
    destroy(vk, up);
    destroy(vk, down);
    timer.destroy(vk);
    s.shutdown();
    return 0;
}
