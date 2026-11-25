#pragma once

#include <cstdint>
#include <string>

namespace nitid {

// Average time per call of each pipeline stage, in milliseconds (wall clock, GPU work included).
struct StageTimes {
    double decode_ms = 0;     // decode + colour convert, on the decoder thread (per source frame)
    double upload_ms = 0;     // CPU frame -> GPU (per source frame)
    double upscale_ms = 0;    // VFX VSR (per source frame)
    double frame_gen_ms = 0;  // VFX frame generation (per generated frame)
    double output_ms = 0;     // copy to the display / CPU buffer (per output frame)
};

// Snapshot from Engine::stats(). Counters run since open() or reset_stats().
struct Stats {
    // Sizes
    std::uint32_t source_width = 0, source_height = 0;
    std::uint32_t work_width = 0, work_height = 0;      // size after upscaling (= frame gen size)
    std::uint32_t output_width = 0, output_height = 0;  // window / headless output size

    // What is actually running (a setting can be on but inactive, e.g. VSR when no upscale is needed).
    bool upscaler_active = false;
    bool frame_gen_active = false;
    std::string upscaler_note;    // why the upscaler is inactive, if it is
    std::string frame_gen_note;   // why frame gen is inactive, if it is

    // Rates
    double source_fps = 0;
    double display_hz = 0;        // window output: refresh rate used for pacing
    double output_fps = 0;        // measured over the last second

    // Counters
    std::uint64_t output_frames = 0;      // frames presented / produced
    std::uint64_t generated_frames = 0;   // of those, made by frame generation
    std::uint64_t source_frames = 0;      // real frames shown at least once
    std::uint64_t skipped_frames = 0;     // real frames never shown because playback fell behind
    std::uint64_t missed_refreshes = 0;   // window output: displayed later than one refresh
    std::uint64_t scene_cuts = 0;

    double position_s = 0;                // playback position
    StageTimes avg;
};

} // namespace nitid
