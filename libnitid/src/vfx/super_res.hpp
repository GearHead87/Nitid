#pragma once

// VFX Video Super Resolution: RGBA8 GPU frame in, RGBA8 GPU frame of any larger size out (spikes S5, S9).
#include "gpu/cuda_context.hpp"
#include "vfx/vfx_api.hpp"

#include <memory>

namespace nitid::vfx {

class SuperRes {
public:
    SuperRes(std::shared_ptr<const Api> api, CUstream stream);
    ~SuperRes();
    SuperRes(const SuperRes&) = delete;
    SuperRes& operator=(const SuperRes&) = delete;

    // Reloads the model only when something changed (~0.2 s). `input` must stay valid while configured.
    void configure(CUdeviceptr input, unsigned in_w, unsigned in_h, unsigned out_w, unsigned out_h, unsigned quality);
    void run();  // input -> output(), complete when it returns
    CUdeviceptr output() const { return out_.ptr(); }
    bool configured() const { return fx_ != nullptr; }
    void reset();

private:
    std::shared_ptr<const Api> api_;
    CUstream stream_;
    NvVFX_Handle fx_ = nullptr;
    gpu::CudaBuffer out_;
    NvCVImage in_img_{}, out_img_{};
    CUdeviceptr in_ = 0;
    unsigned in_w_ = 0, in_h_ = 0, out_w_ = 0, out_h_ = 0, quality_ = ~0u;
};

} // namespace nitid::vfx
