#pragma once

#include <chrono>

namespace nitid {

// Exponential moving average for stage timings (about the last 60 samples).
class RunningAverage {
public:
    void add(double v) { avg_ = n_++ == 0 ? v : avg_ + (v - avg_) / (n_ < 60 ? double(n_) : 60.0); }
    double value() const { return avg_; }
    void reset() { avg_ = 0, n_ = 0; }

private:
    double avg_ = 0;
    unsigned long long n_ = 0;
};

using SteadyClock = std::chrono::steady_clock;

inline double ms_since(SteadyClock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(SteadyClock::now() - t0).count();
}

} // namespace nitid
