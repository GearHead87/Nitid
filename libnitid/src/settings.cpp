#include <nitid/settings.hpp>

namespace nitid {

std::string_view to_string(Upscaler u) noexcept
{
    switch (u) {
        case Upscaler::Off: return "off";
        case Upscaler::VfxSuperRes: return "VFX VSR";
    }
    return "?";
}

std::string_view to_string(SuperResQuality q) noexcept
{
    switch (q) {
        case SuperResQuality::Bicubic: return "bicubic";
        case SuperResQuality::Low: return "low";
        case SuperResQuality::Medium: return "medium";
        case SuperResQuality::High: return "high";
        case SuperResQuality::Ultra: return "ultra";
    }
    return "?";
}

std::string_view to_string(FrameGen f) noexcept
{
    switch (f) {
        case FrameGen::Off: return "off";
        case FrameGen::VfxFrameGen: return "VFX frame gen";
    }
    return "?";
}

std::string_view to_string(FrameGenQuality q) noexcept
{
    switch (q) {
        case FrameGenQuality::Low: return "low";
        case FrameGenQuality::Medium: return "medium";
        case FrameGenQuality::High: return "high";
    }
    return "?";
}

} // namespace nitid
