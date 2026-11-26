#pragma once

// The A/V sync controller (pure, unit-tested). The sound follows the picture: from the smoothed error
// (audio minus video, seconds; + = sound early) it picks a resampling ratio, or asks for a resync.
#include <algorithm>
#include <cmath>

namespace nitid::audio {

constexpr double kResyncError = 0.08;     // smoothed error above this: jump instead of resampling
constexpr double kMaxSpeedChange = 0.01;  // resampling stays within +-1 %
constexpr double kSyncGain = 0.5;         // speed change per second of error
constexpr double kSettleError = 0.012;    // still this far off ~30 frames after a resync: resync once more

// Ratio of input samples consumed per output sample (> 1 = sound catches up), or 0 = resync needed.
inline double sync_ratio(double smoothed_error_s)
{
    if (std::fabs(smoothed_error_s) > kResyncError) return 0;
    return 1.0 - std::clamp(smoothed_error_s * kSyncGain, -kMaxSpeedChange, kMaxSpeedChange);
}

} // namespace nitid::audio
