#pragma once

// Size maths shared by the pipeline and the presenter. Pure functions (unit-tested).
#include <algorithm>
#include <cstdint>

namespace nitid::pipeline {

struct Size {
    uint32_t w = 0, h = 0;
    bool operator==(const Size&) const = default;
};

struct Rect {
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0;
};

// Largest size with the source's aspect ratio that fits in `box` (even width/height, at least 2x2).
inline Size fit(Size src, Size box)
{
    if (!src.w || !src.h || !box.w || !box.h) return {};
    double s = std::min(double(box.w) / src.w, double(box.h) / src.h);
    auto even = [](double v) { return std::max<uint32_t>(2, uint32_t(v) & ~1u); };
    return {std::min(box.w, even(src.w * s + 0.5)), std::min(box.h, even(src.h * s + 0.5))};
}

// `inner` centred in `outer` (letterbox / pillarbox).
inline Rect centre(Size inner, Size outer)
{
    return {int32_t((outer.w - inner.w) / 2), int32_t((outer.h - inner.h) / 2), inner.w, inner.h};
}

// Upscaler output size for a source shown in `box`: fit size, capped at max_scale x the source.
// Returns the source size itself when no upscale is needed.
inline Size upscale_target(Size src, Size box, double max_scale = 4.0)
{
    Size f = fit(src, box);
    if (f.w <= src.w || f.h <= src.h) return src;
    if (f.w > src.w * max_scale) f = fit(src, {uint32_t(src.w * max_scale), uint32_t(src.h * max_scale)});
    return f;
}

} // namespace nitid::pipeline
