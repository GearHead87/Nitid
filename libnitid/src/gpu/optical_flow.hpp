#pragma once

// Motion vectors for DLSS from the GPU's optical flow engine (VK_NV_optical_flow; spikes S4 and F1).
//
// Per frame: a CUDA kernel makes 8-bit luma into a shared buffer; on the pipeline thread's queue it is copied into one
// of two luma images (current / previous take turns); the optical flow queue measures the flow from the current frame
// to the previous one on a 4x4 grid; the grid is copied back into a shared buffer and a CUDA kernel expands it to one
// R16G16 half-float vector per pixel (bilinear). Consumers copy that into motion_image() inside their own submission
// (record_upload). Vectors are in pixels, current -> previous: DLSS's convention (F1).
#include "gpu/interop.hpp"
#include "gpu/kernels.hpp"
#include "gpu/vk_resources.hpp"

#include <memory>

namespace nitid::gpu {

class OpticalFlow {
public:
    // Needs vk.optical_flow, vk.work_queue and the motion kernels (available()).
    OpticalFlow(VulkanContext& vk, CudaContext& cuda, Kernels& kernels);
    ~OpticalFlow();
    OpticalFlow(const OpticalFlow&) = delete;
    OpticalFlow& operator=(const OpticalFlow&) = delete;

    static bool available(const VulkanContext& vk, const Kernels& k);

    void configure(uint32_t w, uint32_t h);  // frame size; no-op if unchanged
    void reset();                            // forget the previous frame (seek, scene cut)

    // Measures motion from `rgba` (w x h RGBA8, CUDA, finished) to the previous frame pushed. Returns false (motion
    // = zero) when there is no previous frame. Blocks until the vectors are in the motion buffer.
    bool push(CUdeviceptr rgba);

    // Records the copy of the latest vectors into motion_image() (then in GENERAL layout).
    void record_upload(VkCommandBuffer cb);
    Image& motion_image() { return motion_; }
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }

private:
    void release();

    VulkanContext& vk_;
    CudaContext& cuda_;
    Kernels& kernels_;
    uint32_t w_ = 0, h_ = 0, fw_ = 0, fh_ = 0;
    static constexpr int kGrid = 4;
    VkOpticalFlowSessionNV session_ = VK_NULL_HANDLE;
    Image luma_[2], flow_, motion_;
    int cur_ = 0;
    bool have_prev_ = false;
    bool hints_ = false;  // the previous flow is a valid hint for this one
    std::unique_ptr<SharedBuffer> luma_buf_, flow_buf_, motion_buf_;
    std::unique_ptr<CommandRunner> copy_in_, flow_run_, copy_out_;
    std::unique_ptr<Semaphore> luma_ready_, flow_ready_;
};

} // namespace nitid::gpu
