#pragma once

// Interface for everything that delivers video frames: files now; screen capture (PipeWire) and capture
// cards (V4L2) later. A frame is RGBA8, either already on the GPU (gpu_rgba) or in CPU memory (rgba).
#include "gpu/cuda_context.hpp"

#include <nitid/source.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace nitid::input {

struct DecodedFrame {
    gpu::CudaBuffer gpu_rgba;   // width * height * 4 on the GPU (GPU colour conversion), or empty
    std::vector<uint8_t> rgba;  // width * height * 4 in CPU memory (CPU path), or empty
    uint32_t width = 0, height = 0;
    int64_t index = 0;          // playback frame number (after source_step), from 0
    double time_s = 0;          // index / fps
    bool scene_cut = false;     // this frame starts a new shot
    double decode_ms = 0;       // decode + colour convert time
};

class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual const SourceInfo& info() const = 0;
    // Blocks until the next frame is ready. nullopt = end of stream (or error, already logged).
    virtual std::optional<DecodedFrame> next() = 0;
    // Gives a frame's pixel buffers back for reuse (avoids reallocating every frame).
    virtual void recycle(DecodedFrame&& frame) = 0;
    // Continue from the frame at `seconds` (frame-exact: frames before it are decoded but not delivered).
    // The next frame's index is round(seconds * fps).
    virtual void seek(double seconds) = 0;
    // Start again from the beginning.
    void restart() { seek(0); }
};

} // namespace nitid::input
