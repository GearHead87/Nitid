#pragma once

#include <nitid/export.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace nitid {

// How the video is scaled to the output size.
enum class Upscaler : std::uint8_t {
    Off,          // no AI upscaling; the frame is scaled to the window with a plain bilinear blit
    VfxSuperRes,  // NVIDIA VFX Video Super Resolution (AI; RTX GPUs with the VFX runtime)
    Fsr1,         // AMD FidelityFX FSR 1 (EASU + RCAS sharpening): spatial, any NVIDIA GPU
    Nis,          // NVIDIA Image Scaling: spatial, up to 2x (larger windows: the rest is a plain blit)
};

// VFX Video Super Resolution quality levels (values match the SDK's QualityLevel).
enum class SuperResQuality : std::uint8_t { Bicubic = 0, Low = 1, Medium = 2, High = 3, Ultra = 4 };

// Same-size clean-up with VFX VSR's restoration models, used when no upscaling is needed (the window is not larger
// than the video). Upscaling modes already clean up compression artifacts while they upscale.
enum class Cleanup : std::uint8_t {
    Off,
    Denoise,  // removes noise and compression artifacts (VSR modes 8-11)
    Deblur,   // sharpens soft video (VSR modes 12-15)
};

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
    // FSR 1 / NIS sharpening, 0 (none) to 1 (strongest).
    float sharpness = 0.5f;
    // Clean-up when the video is shown at its own size (needs the VFX runtime). Strength: low..ultra.
    Cleanup cleanup = Cleanup::Off;
    SuperResQuality cleanup_strength = SuperResQuality::Medium;  // Low..Ultra (Bicubic is treated as Low)

    FrameGen frame_gen = FrameGen::VfxFrameGen;
    FrameGenQuality frame_gen_quality = FrameGenQuality::Medium;
    // Output frames per source frame. 0 = one new frame for every display refresh (window output only;
    // headless output treats 0 as 2).
    int frame_gen_multiplier = 0;

    // Nitid's own scene-cut detector. On a cut, frame generation holds the old shot instead of morphing
    // between two unrelated pictures. (The SDK's automatic detection is always off: see spike V3.)
    bool scene_cut_detection = true;

    // Side by side: the original (plainly scaled) on the left, Nitid's picture on the right, split at
    // Engine::set_compare_split() (window output only).
    bool compare = false;

    bool operator==(const Settings&) const = default;
};

NITID_EXPORT std::string_view to_string(Upscaler) noexcept;
NITID_EXPORT std::string_view to_string(SuperResQuality) noexcept;
NITID_EXPORT std::string_view to_string(FrameGen) noexcept;
NITID_EXPORT std::string_view to_string(Cleanup) noexcept;
NITID_EXPORT std::string_view to_string(FrameGenQuality) noexcept;

// Short description of the chosen upscaler, e.g. "VSR high", "FSR 1", "NIS", "off".
NITID_EXPORT std::string upscaling_name(const Settings& s);

// Parse the names apps use on the command line and in presets.
//   upscaling:        "off", "fsr", "nis", or VSR quality "bicubic", "low", "medium", "high", "ultra"
//   frame generation: "off", "low", "medium", "high"
// Sets both the on/off choice and the quality in `s`. Returns false if the name is unknown.
NITID_EXPORT bool parse_upscaling(std::string_view name, Settings& s) noexcept;
NITID_EXPORT bool parse_frame_gen(std::string_view name, Settings& s) noexcept;
//   clean-up:         "off", "denoise", "deblur", optionally with a strength: "denoise-high", "deblur-low"
NITID_EXPORT bool parse_cleanup(std::string_view name, Settings& s) noexcept;

} // namespace nitid
