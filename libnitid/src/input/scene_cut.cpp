#include "input/scene_cut.hpp"

#include <cmath>

namespace nitid::input {

bool SceneCutDetector::push(const uint8_t* rgba, uint32_t w, uint32_t h)
{
    const uint32_t sw = (w + 3) / 4, sh = (h + 3) / 4;
    sampled_.resize(size_t(sw) * sh);
    for (uint32_t y = 0; y < sh; ++y)
        for (uint32_t x = 0; x < sw; ++x) {
            const uint8_t* p = rgba + (size_t(y) * 4 * w + size_t(x) * 4) * 4;
            sampled_[size_t(y) * sw + x] = uint8_t((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
        }
    return push_luma(sampled_.data(), sw, sh);
}

bool SceneCutDetector::push_luma(const uint8_t* luma, uint32_t w, uint32_t h)
{
    std::array<uint32_t, kBins> count{};
    std::array<float, kTw * kTh> sum{};
    std::array<uint32_t, kTw * kTh> cells{};
    uint32_t n = 0;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = luma + size_t(y) * w;
        const uint32_t ty = y * kTh / h;
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t l = row[x];
            ++count[l * kBins / 256];
            const uint32_t c = ty * kTw + x * kTw / w;
            sum[c] += float(l);
            ++cells[c];
            ++n;
        }
    }
    std::array<float, kBins> hist{};
    for (int i = 0; i < kBins; ++i) hist[i] = n ? float(count[i]) / float(n) : 0.f;
    std::array<float, kTw * kTh> thumb{};
    for (int i = 0; i < kTw * kTh; ++i) thumb[i] = cells[i] ? sum[i] / float(cells[i]) : 0.f;

    bool cut = false;
    if (has_prev_) {
        float hd = 0, td = 0;
        for (int i = 0; i < kBins; ++i) hd += std::fabs(hist[i] - prev_hist_[i]);
        for (int i = 0; i < kTw * kTh; ++i) td += std::fabs(thumb[i] - prev_thumb_[i]);
        const float prev_h = hist_score_, prev_t = thumb_score_;
        hist_score_ = 0.5f * hd;
        thumb_score_ = td / (255.f * kTw * kTh);
        cut = (hist_score_ > hist_threshold && hist_score_ > hist_jump * prev_h) ||
              (thumb_score_ > thumb_threshold && thumb_score_ > thumb_jump * prev_t);
    } else {
        hist_score_ = thumb_score_ = 0;
    }
    prev_hist_ = hist;
    prev_thumb_ = thumb;
    has_prev_ = true;
    return cut;
}

} // namespace nitid::input
