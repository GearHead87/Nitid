#include "gpu/kernels.hpp"

#include "core/log.hpp"
#include "gpu/cuda_context.hpp"

#if NITID_HAVE_KERNELS
#include "nitid_kernels_ptx.h"  // generated: const char nitid_kernels_ptx[]
#endif

namespace nitid::gpu {

namespace {

struct YuvToRgb {
    float ry_cr, gy_cb, gy_cr, by_cb;
    float y_off, y_scale, c_off, c_scale;
};

YuvToRgb coefficients(ColorMatrix m, bool full_range)
{
    float kr = 0.2126f, kb = 0.0722f;  // BT.709
    if (m == ColorMatrix::Bt601) kr = 0.299f, kb = 0.114f;
    if (m == ColorMatrix::Bt2020) kr = 0.2627f, kb = 0.0593f;
    const float kg = 1.0f - kr - kb;
    YuvToRgb c{};
    c.ry_cr = 2.0f * (1.0f - kr);
    c.by_cb = 2.0f * (1.0f - kb);
    c.gy_cb = -2.0f * kb * (1.0f - kb) / kg;
    c.gy_cr = -2.0f * kr * (1.0f - kr) / kg;
    if (full_range) {
        c.y_off = 0, c.y_scale = 1, c.c_off = 0.5f, c.c_scale = 1;
    } else {
        c.y_off = 16.0f / 255.0f, c.y_scale = 255.0f / 219.0f, c.c_off = 128.0f / 255.0f, c.c_scale = 255.0f / 224.0f;
    }
    return c;
}

unsigned div_up(int a, int b) { return unsigned((a + b - 1) / b); }

} // namespace

Kernels::Kernels()
{
#if NITID_HAVE_KERNELS
    CUresult r = cuModuleLoadData(&module_, nitid_kernels_ptx);
    if (r != CUDA_SUCCESS) {
        log::warn("GPU colour conversion unavailable (PTX load failed: {}); using the CPU", int(r));
        module_ = nullptr;
        return;
    }
    if (cuModuleGetFunction(&yuv_to_rgba_, module_, "yuv420sp_to_rgba") != CUDA_SUCCESS ||
        cuModuleGetFunction(&luma_thumb_, module_, "luma_thumbnail") != CUDA_SUCCESS) {
        yuv_to_rgba_ = luma_thumb_ = nullptr;
        log::warn("GPU colour conversion unavailable (kernels missing); using the CPU");
    }
#else
    log::info("built without nvcc: colour conversion runs on the CPU");
#endif
}

Kernels::~Kernels()
{
    if (module_) cuModuleUnload(module_);
}

void Kernels::yuv420sp_to_rgba(CUdeviceptr y, size_t y_pitch, CUdeviceptr uv, size_t uv_pitch, CUdeviceptr rgba,
                               size_t rgba_pitch, int w, int h, int bytes, ColorMatrix m, bool full_range, CUstream stream)
{
    YuvToRgb c = coefficients(m, full_range);
    void* args[] = {&y, &y_pitch, &uv, &uv_pitch, &rgba, &rgba_pitch, &w, &h, &bytes, &c};
    NITID_CU(cuLaunchKernel(yuv_to_rgba_, div_up(w, 32), div_up(h, 8), 1, 32, 8, 1, 0, stream, args, nullptr));
}

void Kernels::luma_thumbnail(CUdeviceptr y, size_t y_pitch, int w, int h, int bytes, int block, bool full_range,
                             CUdeviceptr out, int tw, int th, CUstream stream)
{
    float y_off = full_range ? 0.0f : 16.0f / 255.0f, y_scale = full_range ? 1.0f : 255.0f / 219.0f;
    void* args[] = {&y, &y_pitch, &w, &h, &bytes, &block, &y_off, &y_scale, &out, &tw, &th};
    NITID_CU(cuLaunchKernel(luma_thumb_, div_up(tw, 16), div_up(th, 16), 1, 16, 16, 1, 0, stream, args, nullptr));
}

} // namespace nitid::gpu
