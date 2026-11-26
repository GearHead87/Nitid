#pragma once

#include <cstdint>
#include <string>

namespace nitid {

// What the engine knows about the open input.
struct SourceInfo {
    std::string name;         // file name (or device name for live inputs, later)
    std::string codec;        // e.g. "h264", "hevc", "av1"
    std::string decoder;      // e.g. "NVDEC (cuda)" or "software"
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double fps = 0;           // frame rate the engine plays at (after source_step)
    double duration_s = 0;    // 0 if unknown
    std::string audio;        // e.g. "aac 48000 Hz 2 ch -> PipeWire 1.6.11", or why there is no sound
    bool has_audio = false;   // sound is playing (window output only)
    bool live = false;        // screen capture / PipeWire: no duration, no seeking
};

struct OpenOptions {
    // Use only every Nth decoded frame (fps / N). Used by tests: drop frames, let frame generation
    // rebuild them, compare with the real ones.
    int source_step = 1;
    bool loop = false;        // start again at the end instead of stopping
    bool hw_decode = true;    // try NVDEC first, fall back to software decoding
    bool audio = true;        // play the sound (window output only; headless output never plays sound)
};

// Screen capture through the desktop's portal (it asks the user which screen or window to share).
struct ScreenCaptureOptions {
    bool monitors = true;   // offer whole screens
    bool windows = true;    // offer single windows
    bool cursor = true;     // include the mouse pointer
    bool remember = true;   // the desktop remembers the choice: no dialog next time (restore token, kept in
                            // $XDG_STATE_HOME/nitid/screencast-token)
};

} // namespace nitid
