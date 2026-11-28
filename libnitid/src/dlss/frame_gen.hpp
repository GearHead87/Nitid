#pragma once

// DLSS Frame Generation (DLSS-G) on video frames, through the DLSS module (spikes S3 and F1).
//
// DLSS-G makes `count` frames between the previous real frame it was given and the newest one, at 1/(count+1),
// 2/(count+1), ... of the way, and they must be generated in order for every new real frame. So for each pair of
// real frames all of them are made at once (one Vulkan submission) and kept until the pair changes; the pipeline then
// picks the one for the moment it needs.
//
// Video has no camera or depth: a stationary synthetic camera, flat depth, and motion vectors from optical flow
// ("lite" mode needs them; the full model finds motion itself and is given zero vectors, which scored better in F1).
#include "dlss/runtime.hpp"
#include "gpu/interop.hpp"
#include "gpu/optical_flow.hpp"
#include "gpu/vk_resources.hpp"

#include <memory>
#include <vector>

namespace nitid::dlss {

class FrameGen {
public:
    FrameGen(gpu::VulkanContext& vk, gpu::CudaContext& cuda, const Runtime& rt);
    ~FrameGen();
    FrameGen(const FrameGen&) = delete;
    FrameGen& operator=(const FrameGen&) = delete;

    // Frames w x h (RGBA8), motion vectors / depth mv_w x mv_h (the source size). Throws nitid::Error.
    void configure(uint32_t w, uint32_t h, uint32_t mv_w, uint32_t mv_h, bool lite);
    bool configured() const { return feature_ != nullptr; }
    bool lite() const { return lite_; }
    void reset();    // frees everything
    void restart();  // forget the frames seen so far (seek, scene cut): the next pair starts with a reset

    // Makes the `count` frames between real frames `older` and `newer` (CUDA pointers, w x h RGBA8, CUDA work on them
    // finished), unless that pair was already done with this count. `flow`: motion newer -> older (its latest
    // result), or nullptr for zero motion. Indices identify frames (newer = older + 1 for a real pair).
    void generate(CUdeviceptr older, int64_t older_index, CUdeviceptr newer, int64_t newer_index, int count,
                  gpu::OpticalFlow* flow);
    // Generated frame k (1..count) of the last generate().
    CUdeviceptr output(int k) const { return outs_[size_t(k - 1)]->ptr(); }

private:
    gpu::VulkanContext& vk_;
    gpu::CudaContext& cuda_;
    const Runtime& rt_;
    gpu::CommandRunner runner_;
    void* feature_ = nullptr;
    bool lite_ = false;
    uint32_t w_ = 0, h_ = 0;
    gpu::Image back_, out_, zero_motion_, depth_;
    std::unique_ptr<gpu::SharedBuffer> older_buf_, newer_buf_;
    std::vector<std::unique_ptr<gpu::SharedBuffer>> outs_;
    uint64_t frame_id_ = 0;
    int64_t last_fed_ = -1;   // index of the last real frame given to DLSS-G
    int64_t done_index_ = -1; // newer index of the pair whose frames are in outs_
    int done_count_ = 0;
};

} // namespace nitid::dlss
