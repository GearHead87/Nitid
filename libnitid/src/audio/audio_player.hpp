#pragma once

// Sound for file playback: FFmpeg decodes the file's audio on its own thread (48 kHz stereo float), PipeWire plays
// it. The *video* clock leads (it is locked to the display so frame generation stays smooth); the audio follows it:
//
//  - once per rendered frame the engine calls sync(media time of that frame, when it will be on screen);
//  - the player compares that with what PipeWire will be playing at that moment (its reported delay) and
//    resamples the audio very slightly (at most +-1%, normally ~0.01%) to close the gap;
//  - gaps it can't close quickly (start, seek, pause, a stalled video) are fixed at once by skipping audio or
//    inserting silence ("resync").
//
// This is how mpv's display-resample mode works. Audio is optional: files without audio, or machines without
// PipeWire, play silently.
#include <cstdint>
#include <memory>
#include <string>

namespace nitid::audio {

struct AudioStats {
    std::string description;      // e.g. "aac 48000 Hz 2 ch -> PipeWire 1.6.11"
    double sync_error_ms = 0;     // audio minus video, smoothed (+ = audio early)
    double max_sync_error_ms = 0; // largest |error| since reset_stats(), outside resyncs
    double speed = 1;             // current resampling ratio
    double latency_ms = 0;        // PipeWire's delay to the device
    uint64_t resyncs = 0;
    uint64_t underruns = 0;       // the decoder could not keep up (silence played)
};

class AudioPlayer {
public:
    // nullptr if the file has no audio stream or PipeWire is not available; *note says which.
    static std::unique_ptr<AudioPlayer> open(const std::string& path, std::string* note);
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    void seek(double seconds);
    void set_paused(bool paused);
    void set_volume(float gain);       // linear gain, 0..1 (more boosts)
    void set_muted(bool muted);
    void set_delay(double seconds);    // user offset: + plays the sound later
    // Once per rendered frame: `media_s` will be on screen at `when_ns` (CLOCK_MONOTONIC / steady_clock).
    void sync(double media_s, int64_t when_ns);

    AudioStats stats() const;
    void reset_stats();

private:
    AudioPlayer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nitid::audio
