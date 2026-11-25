#include "input/scene_cut.hpp"

#include <cmath>

namespace nitid::input {

bool SceneCutDetector::push(const uint8_t* rgba, uint32_t w, uint32_t h)
{
    std::array<uint32_t, kBins> count{};
    uint32_t n = 0;
    for (uint32_t y = 0; y < h; y += 4) {
        const uint8_t* row = rgba + size_t(y) * w * 4;
        for (uint32_t x = 0; x < w; x += 4) {
            const uint8_t* p = row + size_t(x) * 4;
            uint32_t luma = (77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8;
            ++count[luma * kBins / 256];
            ++n;
        }
    }
    std::array<float, kBins> hist{};
    for (int i = 0; i < kBins; ++i) hist[i] = n ? float(count[i]) / float(n) : 0.f;
    bool cut = false;
    if (has_prev_) {
        float d = 0;
        for (int i = 0; i < kBins; ++i) d += std::fabs(hist[i] - prev_[i]);
        score_ = 0.5f * d;
        cut = score_ > threshold;
    } else {
        score_ = 0;
    }
    prev_ = hist;
    has_prev_ = true;
    return cut;
}

} // namespace nitid::input
