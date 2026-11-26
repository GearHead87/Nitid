#pragma once

// Live video from PipeWire: screen capture (through the desktop portal) or any PipeWire video node.
//
// Frames arrive on PipeWire's thread. DMA-BUF frames (the compositor's GPU memory, spike S6) are imported into
// Vulkan and copied into a buffer CUDA can read; shared-memory frames are uploaded. Either way a CUDA kernel turns
// BGRx/RGBx into RGBA8 in a pooled GPU buffer, and only the newest frame is kept for the player (older untaken
// ones are dropped: a live picture should never lag behind).
#include "gpu/cuda_context.hpp"
#include "gpu/kernels.hpp"
#include "gpu/vulkan_context.hpp"
#include "input/frame_source.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nitid::input {

class PipeWireCapture final : public FrameSource {
public:
    // Where to connect. fd >= 0: a PipeWire remote opened by the portal (node = its stream); else the user's
    // PipeWire with `node` (an object id) or `target` (a node name / serial), for tests and tools.
    struct Target {
        int fd = -1;
        uint32_t node = 0xffffffffu;  // PW_ID_ANY
        std::string target;
        std::string name;             // shown as the source name
    };
    // Runs on a setup thread (the portal may show a permission dialog and wait for the user). Returns false and
    // sets `error` on failure; must return promptly once `stop` is set.
    // `status` reports progress ("waiting for permission") to the player.
    using Connect = std::function<bool(Target& out, std::string& error, const std::atomic<bool>& stop,
                                       const std::function<void(std::string)>& status)>;

    PipeWireCapture(Connect connect, gpu::VulkanContext& vk, gpu::CudaContext& cuda, gpu::Kernels& kernels);
    ~PipeWireCapture() override;

    const SourceInfo& info() const override;
    std::optional<DecodedFrame> next() override;      // waits up to 1 s for a new frame
    std::optional<DecodedFrame> try_next() override;
    void recycle(DecodedFrame&& frame) override;
    void seek(double) override {}
    bool live() const override { return true; }
    uint64_t dropped() const override;

    // "waiting for permission", "capturing 2560x1440 BGRx (DMA-BUF)", "failed: ...".
    std::string status() const;
    bool failed() const;

    struct Impl;  // PipeWire / Vulkan state (callbacks need it)

private:
    std::unique_ptr<Impl> impl_;
    mutable SourceInfo info_;
};

} // namespace nitid::input
