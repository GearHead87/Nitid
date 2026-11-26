#pragma once

// Nitid's own CUDA kernels (gpu/kernels/*.cu), embedded as PTX and loaded with the driver API.
#include <cuda.h>

#include <cstddef>
#include <cstdint>

namespace nitid::gpu {

// Colour matrix for YUV -> RGB.
enum class ColorMatrix { Bt601, Bt709, Bt2020 };

class Kernels {
public:
    // Loads the module into the current context. available() is false if the PTX was not built in
    // or failed to load (callers then use the CPU path).
    Kernels();
    ~Kernels();
    Kernels(const Kernels&) = delete;
    Kernels& operator=(const Kernels&) = delete;

    bool available() const { return yuv_to_rgba_ != nullptr; }

    // Semi-planar 4:2:0 (NV12 = 1 byte per sample, P010/P016 = 2) -> RGBA8 full range.
    void yuv420sp_to_rgba(CUdeviceptr y, size_t y_pitch, CUdeviceptr uv, size_t uv_pitch, CUdeviceptr rgba,
                          size_t rgba_pitch, int width, int height, int bytes_per_sample, ColorMatrix m, bool full_range,
                          CUstream stream);

    // Luma thumbnail (each `block` x `block` area averaged), full range, tw = ceil(w / block).
    void luma_thumbnail(CUdeviceptr y, size_t y_pitch, int width, int height, int bytes_per_sample, int block,
                        bool full_range, CUdeviceptr out, int tw, int th, CUstream stream);

    // Captured frames: BGRx / RGBx (4 bytes per pixel, row pitch `pitch`) -> RGBA8 (alpha 255), tightly packed.
    void rgbx_to_rgba(CUdeviceptr src, size_t pitch, CUdeviceptr rgba, int width, int height, bool bgr, CUstream stream);

private:
    CUmodule module_ = nullptr;
    CUfunction rgbx_ = nullptr;
    CUfunction yuv_to_rgba_ = nullptr;
    CUfunction luma_thumb_ = nullptr;
};

} // namespace nitid::gpu
