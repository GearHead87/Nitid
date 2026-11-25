#pragma once

// Interface for everything that delivers video frames: files now; screen capture (PipeWire) and capture
// cards (V4L2) later. Frames arrive as RGBA8 in CPU memory for 0.1; GPU-resident frames come later.
#include <nitid/source.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace nitid::input {

struct DecodedFrame {
    std::vector<uint8_t> rgba;  // width * height * 4
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
    // Gives a frame's pixel buffer back for reuse (avoids reallocating every frame).
    virtual void recycle(std::vector<uint8_t>&& buffer) = 0;
    // Start again from the beginning.
    virtual void restart() = 0;
};

} // namespace nitid::input
