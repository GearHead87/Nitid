#pragma once

// Spatial (single-frame, non-AI) upscalers on RGBA8 GPU frames: AMD FSR 1 (EASU + RCAS sharpening) and NVIDIA Image
// Scaling. Both run as Nitid's own CUDA kernels (gpu/kernels/scale.cu), so they work on any NVIDIA GPU Nitid runs
// on, without the VFX runtime. Cost: well under a millisecond at 4K.
#include "gpu/cuda_context.hpp"

#include <cstdint>

namespace nitid::gpu {

enum class SpatialKind { Fsr1, Nis };

class SpatialScaler {
public:
    // Loads the kernels into the current CUDA context. available() is false without them (built without nvcc).
    SpatialScaler();
    ~SpatialScaler();
    SpatialScaler(const SpatialScaler&) = delete;
    SpatialScaler& operator=(const SpatialScaler&) = delete;

    bool available() const { return easu_ != nullptr; }

    // NIS supports 1x to 2x per axis; larger ratios must be capped by the caller.
    static constexpr double kNisMaxScale = 2.0;

    // Sizes and sharpness (0 = none, 1 = strongest). Cheap when nothing changed.
    void configure(SpatialKind kind, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h, float sharpness);
    // src: src_w x src_h RGBA8, dst: dst_w x dst_h RGBA8 (tightly packed). Queued on `stream`.
    void run(CUdeviceptr src, CUdeviceptr dst, CUstream stream);

private:
    CUmodule module_ = nullptr;
    CUfunction easu_ = nullptr, rcas_ = nullptr, nis_ = nullptr;
    SpatialKind kind_ = SpatialKind::Fsr1;
    uint32_t sw_ = 0, sh_ = 0, dw_ = 0, dh_ = 0;
    float sharpness_ = -1;
    CudaBuffer temp_;   // FSR: EASU output, input of RCAS
    CudaBuffer coefs_;  // NIS: scaler + USM filter tables, [64][6] floats each
    struct NisParams {
        float kDetectRatio, kDetectThres, kMinContrastRatio, kRatioNorm, kContrastBoost, kEps;
        float kSharpStartY, kSharpScaleY, kSharpStrengthMin, kSharpStrengthScale, kSharpLimitMin, kSharpLimitScale;
        float kScaleX, kScaleY;
        int src_w, src_h, dst_w, dst_h;
    } nis_params_{};
};

} // namespace nitid::gpu
