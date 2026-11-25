#pragma once

// Scene-cut detection on RGBA8 frames: compares luma histograms of consecutive frames. Camera pans keep
// the histogram mostly the same; a cut to another shot changes it. Cheap: samples every 4th pixel.
#include <array>
#include <cstdint>

namespace nitid::input {

class SceneCutDetector {
public:
    // Returns true if `rgba` starts a new shot compared with the previous frame pushed.
    bool push(const uint8_t* rgba, uint32_t width, uint32_t height);
    float last_score() const { return score_; }  // 0 = same histogram, 1 = completely different
    void reset() { has_prev_ = false; }

    float threshold = 0.40f;

private:
    static constexpr int kBins = 64;
    std::array<float, kBins> prev_{};
    bool has_prev_ = false;
    float score_ = 0;
};

} // namespace nitid::input
