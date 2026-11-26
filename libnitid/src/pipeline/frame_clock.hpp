#pragma once

// Maps output moments to source frames.
//
// Position is measured in source frames (2.25 = a quarter of the way from frame 2 to frame 3).
// Display output advances by one refresh per call (and by whole missed refreshes after a stall), so
// generated frames land exactly on the refresh grid (spike S7: FIFO pacing is steady to +-0.03 ms).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nitid::pipeline {

struct FramePick {
    int64_t frame = 0;  // real frame to show, or the earlier frame of the pair
    float phase = 0;    // 0 = exactly that frame; (0,1) = between it and the next
};

// Splits a position into frame + phase; multiplier > 0 rounds the phase down to k / multiplier.
inline FramePick pick(double position, int multiplier)
{
    double f = std::floor(position + 1e-9);
    double ph = position - f;
    if (multiplier > 0) ph = std::floor(ph * multiplier + 1e-6) / multiplier;
    if (ph < 1e-4) ph = 0;
    return {int64_t(f), float(ph)};
}

// Frame-gen pacing: how many refreshes one generated frame may take. A frame that costs more than 95 % of the time
// it has would miss the display now and then; prepare one every `stride` refreshes instead (evenly paced).
inline int frame_gen_stride(double frame_ms, double period_s, int max_stride)
{
    if (period_s <= 0 || frame_ms <= 0) return 1;
    return std::clamp(int(std::ceil(frame_ms / (0.95 * period_s * 1000))), 1, max_stride);
}

class DisplayClock {
public:
    static constexpr double kMaxCatchUpS = 0.25;

    void reset() { seek(0); }
    // Continue from media time `s`; the next tick returns exactly `s`.
    void seek(double s) { media_s_ = s, debt_s_ = 0, started_ = false; }

    // Call once per display refresh. Returns the media time (seconds) for this refresh.
    //
    // Media time moves in whole refreshes, so generated frames land on the refresh grid, and follows the
    // wall clock on average: a frame that took 1.4 refreshes adds 1, the leftover 0.4 is carried to the
    // next one (no slow motion when the GPU can't keep up every refresh). After a long stall (loading a
    // model, dragging the window) it carries on from where it was instead of jumping ahead.
    double tick(double now_s, double period_s, bool paused)
    {
        if (!started_) {
            started_ = true;
            last_now_ = now_s;
            return media_s_;
        }
        double elapsed = now_s - last_now_;
        last_now_ = now_s;
        if (paused) return media_s_;
        if (elapsed > kMaxCatchUpS) elapsed = period_s;
        debt_s_ += elapsed;
        double refreshes = std::max(1.0, std::floor(debt_s_ / period_s + 0.5));
        debt_s_ -= refreshes * period_s;
        media_s_ += refreshes * period_s;
        return media_s_;
    }

    double media_s() const { return media_s_; }

private:
    double media_s_ = 0, last_now_ = 0, debt_s_ = 0;
    bool started_ = false;
};

} // namespace nitid::pipeline
