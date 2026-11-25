#pragma once

// Scene-cut detection on RGBA8 frames, with two measures between consecutive frames:
//  - luma histogram difference (0..1): catches cuts to a shot with different brightness/colours;
//  - mean absolute difference of a 32x18 luma thumbnail (0..1): catches cuts between similar-looking
//    shots (e.g. two shots of an orange sky).
// A cut is a *sudden* change: a measure must pass its threshold AND be several times its value for the
// previous frame. Fades, flashes and camera pans change the frame steadily; frame generation handles
// those, so they must not count as cuts. Tuned on the Sintel trailer (tests/v0.1/04-cli-bench): finds all
// 7 real cuts, 1 borderline extra (a dark scene brightening at once).
#include <array>
#include <cstdint>
#include <vector>

namespace nitid::input {

class SceneCutDetector {
public:
    // Returns true if the frame starts a new shot compared with the previous frame pushed.
    // RGBA8 frame (samples every 4th pixel):
    bool push(const uint8_t* rgba, uint32_t width, uint32_t height);
    // Small full-range luma image (e.g. the GPU path's 1/4-size thumbnail), tightly packed:
    bool push_luma(const uint8_t* luma, uint32_t width, uint32_t height);
    float last_score() const { return hist_score_; }      // histogram measure for the last frame
    float last_thumb_score() const { return thumb_score_; }
    void reset() { has_prev_ = false, hist_score_ = thumb_score_ = 0; }

    float hist_threshold = 0.30f, hist_jump = 3.0f;
    float thumb_threshold = 0.15f, thumb_jump = 2.5f;

private:
    static constexpr int kBins = 64, kTw = 32, kTh = 18;
    std::array<float, kBins> prev_hist_{};
    std::array<float, kTw * kTh> prev_thumb_{};
    bool has_prev_ = false;
    float hist_score_ = 0, thumb_score_ = 0;
    std::vector<uint8_t> sampled_;
};

} // namespace nitid::input
