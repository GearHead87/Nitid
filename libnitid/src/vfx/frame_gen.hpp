#pragma once

// VFX Video Frame Generation: makes the frame at any time between two RGBA8 GPU frames (spikes V3, S9).
// The SDK's automatic shot-change detection is always off (it misfires on fast motion, spike V3);
// the caller passes its own scene-cut decision.
#include "gpu/cuda_context.hpp"
#include "vfx/vfx_api.hpp"

#include <memory>

namespace nitid::vfx {

class FrameGen {
public:
    FrameGen(std::shared_ptr<const Api> api, CUstream stream);
    ~FrameGen();
    FrameGen(const FrameGen&) = delete;
    FrameGen& operator=(const FrameGen&) = delete;

    void configure(unsigned width, unsigned height, unsigned mode);  // reloads only when changed
    // timestep in (0, 1): 0 = previous, 1 = current. Complete when it returns; result in output().
    void run(CUdeviceptr previous, CUdeviceptr current, float timestep, bool shot_change);
    CUdeviceptr output() const { return out_.ptr(); }
    bool configured() const { return fx_ != nullptr; }
    void reset();

private:
    std::shared_ptr<const Api> api_;
    CUstream stream_;
    NvVFX_Handle fx_ = nullptr;
    gpu::CudaBuffer out_;
    NvCVImage out_img_{}, prev_img_{}, cur_img_{};  // the SDK keeps pointers to these
    unsigned w_ = 0, h_ = 0, mode_ = ~0u;
};

} // namespace nitid::vfx
