#pragma once

#include <nitid/export.hpp>

#include <cstdint>
#include <string_view>

namespace nitid {

// How the video is scaled to the output size.
enum class Upscaler : std::uint8_t {
    Off,          // no AI upscaling; the frame is scaled to the window with a plain bilinear blit
    VfxSuperRes,  // NVIDIA VFX Video Super Resolution
};

// VFX Video Super Resolution quality levels (values match the SDK's QualityLevel).
enum class SuperResQuality : std::uint8_t { Bicubic = 0, Low = 1, Medium = 2, High = 3, Ultra = 4 };

// How extra frames are made between real video frames.
enum class FrameGen : std::uint8_t {
    Off,
    VfxFrameGen,  // NVIDIA VFX Video Frame Generation (RTX 40/50)
};

// VFX Video Frame Generation modes (values match the SDK's Mode).
enum class FrameGenQuality : std::uint8_t { Low = 0, Medium = 1, High = 2 };

// Everything the user can change while playing. Cheap to copy; apply with Engine::set_settings().
struct Settings {
    Upscaler upscaler = Upscaler::VfxSuperRes;
    SuperResQuality super_res_quality = SuperResQuality::High;

    FrameGen frame_gen = FrameGen::VfxFrameGen;
    FrameGenQuality frame_gen_quality = FrameGenQuality::Medium;
    // Output frames per source frame. 0 = one new frame for every display refresh (window output only;
    // headless output treats 0 as 2).
    int frame_gen_multiplier = 0;

    // Nitid's own scene-cut detector. On a cut, frame generation holds the old shot instead of morphing
    // between two unrelated pictures. (The SDK's automatic detection is always off: see spike V3.)
    bool scene_cut_detection = true;

    bool operator==(const Settings&) const = default;
};

NITID_EXPORT std::string_view to_string(Upscaler) noexcept;
NITID_EXPORT std::string_view to_string(SuperResQuality) noexcept;
NITID_EXPORT std::string_view to_string(FrameGen) noexcept;
NITID_EXPORT std::string_view to_string(FrameGenQuality) noexcept;

} // namespace nitid
