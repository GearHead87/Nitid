#pragma once

// DLSS Super Resolution on video frames (through the DLSS module).
//
// The pipeline works on CUDA buffers; DLSS works on Vulkan images. Frames cross over through buffers shared between
// Vulkan and CUDA (gpu::SharedBuffer, as screen capture does): CUDA writes the input buffer, one Vulkan submission
// copies it into the colour image, runs DLSS and copies the result into the output buffer, and CUDA reads that.
//
// Video has no camera jitter and no depth: jitter is 0, depth a flat plane, and motion vectors come from optical
// flow (set_motion) or are zero. DLSS then works as a temporal upscaler (it accumulates detail from earlier frames
// along the motion); its history is reset at scene cuts and seeks.
#include "dlss/runtime.hpp"
#include "gpu/interop.hpp"
#include "gpu/vk_resources.hpp"

#include <memory>
#include <string>

namespace nitid::dlss {

class SuperRes {
public:
    SuperRes(gpu::VulkanContext& vk, gpu::CudaContext& cuda, const Runtime& rt);
    ~SuperRes();
    SuperRes(const SuperRes&) = delete;
    SuperRes& operator=(const SuperRes&) = delete;

    // Creates the DLSS feature for these sizes (in == out: DLAA). preset 0 = NVIDIA's default for the ratio.
    // Throws nitid::Error.
    void configure(uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, int preset);
    bool configured() const { return feature_ != nullptr; }
    void reset();  // frees the feature and the images

    CUdeviceptr input() const { return in_buf_->ptr(); }    // write the RGBA8 input frame here
    CUdeviceptr output() const { return out_buf_->ptr(); }  // RGBA8 result, after run()
    // Motion vectors for the next run(): R16G16_SFLOAT pixels, current -> previous frame, input size; nullptr = zero.
    void set_motion(gpu::Image* motion) { motion_ = motion; }
    // Runs DLSS on input() (CUDA work on it must be finished). reset: forget the history (cut, seek, first frame).
    void run(bool reset);

    // "DLAA", "Quality 1.50x", "Performance 2.00x", ...
    const std::string& mode() const { return mode_; }

private:
    gpu::VulkanContext& vk_;
    gpu::CudaContext& cuda_;
    const Runtime& rt_;
    gpu::CommandRunner runner_;
    void* feature_ = nullptr;
    std::unique_ptr<gpu::SharedBuffer> in_buf_, out_buf_;
    gpu::Image color_, zero_motion_, depth_, out_;
    gpu::Image* motion_ = nullptr;
    std::string mode_;
    int preset_ = 0;
};

} // namespace nitid::dlss
