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
    std::int32_t video_x = 0, video_y = 0;              // window output: where the picture is (pixels)
    std::uint32_t video_w = 0, video_h = 0;

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
    std::uint64_t late_frames = 0;        // window output: refreshes where a due frame was not ready in time
    // Frame gen one-per-refresh mode: a new frame every `frame_gen_stride` refreshes (1 = every refresh; more when
    // frame generation takes longer than a refresh). 0 = not in that mode.
    int frame_gen_stride = 0;

    double position_s = 0;                // playback position
    StageTimes avg;

    // Sound (when the source has audio and it is playing)
    bool audio_active = false;
    double av_sync_ms = 0;          // audio minus picture, smoothed (+ = sound early); kept near 0 by resampling
    double av_sync_max_ms = 0;      // largest |av_sync_ms| since reset_stats(), excluding resyncs
    double audio_speed = 1;         // resampling ratio used to follow the picture
    double audio_latency_ms = 0;    // PipeWire's delay to the speakers
    double display_latency_ms = 0;  // present() to on screen
    std::uint64_t audio_resyncs = 0;    // jumps (start, seek, pause, stalls)
    std::uint64_t audio_underruns = 0;  // audio decoding fell behind
};

} // namespace nitid
