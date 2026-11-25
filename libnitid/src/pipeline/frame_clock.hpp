#pragma once

// Maps output moments to source frames.
//
// Position is measured in source frames (2.25 = a quarter of the way from frame 2 to frame 3).
// Display output advances by one refresh per call (and by whole missed refreshes after a stall), so
// generated frames land exactly on the refresh grid (spike S7: FIFO pacing is steady to +-0.03 ms).
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

class DisplayClock {
public:
    static constexpr double kMaxCatchUpS = 0.25;

    void reset() { media_s_ = 0, started_ = false; }

    // Call once per display refresh. Returns the media time (seconds) for this refresh.
    double tick(double now_s, double refresh_period_s, bool paused)
    {
        if (!started_) {
            started_ = true;
            last_now_ = now_s;
            return media_s_;
        }
        double elapsed = now_s - last_now_;
        last_now_ = now_s;
        if (paused) return media_s_;
        // Normally exactly one refresh; after a short stall, the refreshes that really passed (keeps time);
        // after a long one (loading, window drag) carry on where we were instead of jumping ahead.
        double refreshes = 1.0;
        if (elapsed > 1.5 * refresh_period_s && elapsed < kMaxCatchUpS) refreshes = std::round(elapsed / refresh_period_s);
        media_s_ += refreshes * refresh_period_s;
        return media_s_;
    }

    double media_s() const { return media_s_; }

private:
    double media_s_ = 0, last_now_ = 0;
    bool started_ = false;
};

} // namespace nitid::pipeline
