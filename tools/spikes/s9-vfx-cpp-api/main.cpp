// S9: the two VFX calls Nitid 0.1 needs, from C++ (V3 only tested frame generation from Python).
//
//  1. VideoSuperRes to a non-integer output size (e.g. 1920x1080 -> 2560x1440, this monitor).
//  2. VideoFrameGeneration with RGBA8 GPU images and an explicit Timestep (FrameMultiplier = 0),
//     automatic shot-change detection off. Generates t = 0.5 from frames (a, c); the real middle
//     frame b is the reference (the run script measures PSNR).
//
// Usage: s9-vfx-cpp-api <a.ppm> <b_unused_or_any.ppm> <c.ppm> <out_dir> <sr_w> <sr_h> <vfg_mode 0..2>

#include "ppm.hpp"

#include <cuda.h>
#include <nvVideoEffects.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace spike;
using clk = std::chrono::steady_clock;

#define CU_CHECK(x)                                                                         \
    do {                                                                                    \
        CUresult r_ = (x);                                                                  \
        if (r_ != CUDA_SUCCESS) {                                                           \
            std::fprintf(stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #x, int(r_)); \
            std::exit(1);                                                                   \
        }                                                                                   \
    } while (0)

static bool cv_ok(NvCV_Status s, const char* what)
{
    if (s != NVCV_SUCCESS) {
        std::printf("  %s -> %d %s\n", what, int(s), NvCV_GetErrorStringFromCode(s));
        return false;
    }
    return true;
}
#define CV(x) cv_ok((x), #x)

struct GpuImg {
    CUdeviceptr ptr = 0;
    NvCVImage img{};
    uint32_t w = 0, h = 0;
};

static GpuImg gpu_rgba(uint32_t w, uint32_t h)
{
    GpuImg g;
    g.w = w;
    g.h = h;
    CU_CHECK(cuMemAlloc(&g.ptr, size_t(w) * h * 4));
    NvCVImage_Init(&g.img, w, h, int(w * 4), reinterpret_cast<void*>(g.ptr), NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_GPU);
    return g;
}

static void save(const GpuImg& g, const std::string& path)
{
    std::vector<uint8_t> px(size_t(g.w) * g.h * 4);
    CU_CHECK(cuMemcpyDtoH(px.data(), g.ptr, px.size()));
    write_ppm_rgba(path, px.data(), g.w, g.h);
}

static double median_ms(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main(int argc, char** argv)
{
    if (argc < 8) {
        std::fprintf(stderr, "usage: %s a.ppm b.ppm c.ppm out_dir sr_w sr_h vfg_mode\n", argv[0]);
        return 2;
    }
    uint32_t w, h, w2, h2;
    auto a = read_ppm_rgba(argv[1], w, h);
    auto c = read_ppm_rgba(argv[3], w2, h2);
    std::string out = argv[4];
    uint32_t sw = std::stoul(argv[5]), sh = std::stoul(argv[6]), mode = std::stoul(argv[7]);

    CU_CHECK(cuInit(0));
    CUdevice dev;
    CU_CHECK(cuDeviceGet(&dev, 0));
    CUcontext ctx;
    CU_CHECK(cuDevicePrimaryCtxRetain(&ctx, dev));
    CU_CHECK(cuCtxSetCurrent(ctx));
    CUstream st;
    // Blocking stream: the VFX SDK does part of its work on the legacy default stream; a non-blocking
    // stream neither waits for it (timings too low) nor orders results (VSR returned the previous frame).
    CU_CHECK(cuStreamCreate(&st, CU_STREAM_DEFAULT));

    GpuImg ga = gpu_rgba(w, h), gc = gpu_rgba(w, h);
    CU_CHECK(cuMemcpyHtoD(ga.ptr, a.data(), a.size()));
    CU_CHECK(cuMemcpyHtoD(gc.ptr, c.data(), c.size()));

    // 1. VSR to sw x sh.
    std::printf("## VSR %ux%u -> %ux%u (HIGH)\n", w, h, sw, sh);
    {
        GpuImg o = gpu_rgba(sw, sh);
        NvVFX_Handle fx = nullptr;
        bool ok = CV(NvVFX_CreateEffect("VideoSuperRes", &fx)) && CV(NvVFX_SetU32(fx, "ImageEncodingMode", 0)) &&
                  CV(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE, &ga.img)) && CV(NvVFX_SetImage(fx, NVVFX_OUTPUT_IMAGE, &o.img)) &&
                  CV(NvVFX_SetU32(fx, "QualityLevel", 3)) && CV(NvVFX_SetF32(fx, "Strength", 1.0f)) &&
                  CV(NvVFX_SetCudaStream(fx, NVVFX_CUDA_STREAM, st)) && CV(NvVFX_Load(fx));
        if (ok) {
            std::vector<double> t;
            for (int i = 0; i < 60 && ok; ++i) {
                auto t0 = clk::now();
                ok = CV(NvVFX_Run(fx, 0));
                CU_CHECK(cuStreamSynchronize(st));
                if (i >= 10) t.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
            }
            if (ok) {
                save(o, out + "/vsr.ppm");
                std::printf("RESULT vsr ok median_ms=%.3f\n", median_ms(t));
            }
        }
        if (!ok) std::printf("RESULT vsr FAILED\n");
        if (fx) NvVFX_DestroyEffect(fx);
        cuMemFree(o.ptr);
    }

    // 2. VFG with RGBA8, Timestep 0.5, auto shot-change off.
    std::printf("## VFG %ux%u mode %u, RGBA8, Timestep 0.5\n", w, h, mode);
    {
        GpuImg o = gpu_rgba(w, h);
        NvVFX_Handle fx = nullptr;
        bool ok = CV(NvVFX_CreateEffect("VideoFrameGeneration", &fx)) &&
                  CV(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE_0, &ga.img)) && CV(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE_1, &gc.img)) &&
                  CV(NvVFX_SetU32(fx, "InputWidth", w)) && CV(NvVFX_SetU32(fx, "InputHeight", h)) &&
                  CV(NvVFX_SetU32(fx, "Mode", mode)) && CV(NvVFX_SetU32(fx, "AutomaticShotChangeDetectionEnabled", 0)) &&
                  CV(NvVFX_SetCudaStream(fx, NVVFX_CUDA_STREAM, st)) && CV(NvVFX_Load(fx)) &&
                  CV(NvVFX_SetImage(fx, NVVFX_OUTPUT_IMAGE, &o.img)) && CV(NvVFX_SetU32(fx, "FrameMultiplier", 0)) &&
                  CV(NvVFX_SetU32(fx, "ShotChange", 0)) && CV(NvVFX_SetF32(fx, "Timestep", 0.5f));
        if (ok) {
            std::vector<double> t;
            for (int i = 0; i < 60 && ok; ++i) {
                auto t0 = clk::now();
                ok = CV(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE_0, &ga.img)) && CV(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE_1, &gc.img)) &&
                     CV(NvVFX_SetF32(fx, "Timestep", 0.5f)) && CV(NvVFX_Run(fx, 0));
                CU_CHECK(cuStreamSynchronize(st));
                if (i >= 10) t.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
            }
            if (ok) {
                save(o, out + "/vfg_t050.ppm");
                // Two more timesteps for a 4x-style check (outputs only).
                for (float ts : {0.25f, 0.75f}) {
                    if (CV(NvVFX_SetF32(fx, "Timestep", ts)) && CV(NvVFX_Run(fx, 0))) {
                        CU_CHECK(cuStreamSynchronize(st));
                        save(o, out + "/vfg_t0" + std::to_string(int(ts * 100)) + ".ppm");
                    }
                }
                // Shot change flag: output should be a copy of one input.
                if (CV(NvVFX_SetU32(fx, "ShotChange", 1)) && CV(NvVFX_SetF32(fx, "Timestep", 0.5f)) && CV(NvVFX_Run(fx, 0))) {
                    CU_CHECK(cuStreamSynchronize(st));
                    save(o, out + "/vfg_shotchange.ppm");
                }
                std::printf("RESULT vfg ok median_ms=%.3f\n", median_ms(t));
            }
        }
        if (!ok) std::printf("RESULT vfg FAILED\n");
        if (fx) NvVFX_DestroyEffect(fx);
        cuMemFree(o.ptr);
    }
    cuMemFree(ga.ptr);
    cuMemFree(gc.ptr);
    cuStreamDestroy(st);
    cuDevicePrimaryCtxRelease(dev);
    return 0;
}
