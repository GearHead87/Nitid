#include <nitid/settings.hpp>

namespace nitid {

std::string_view to_string(Upscaler u) noexcept
{
    switch (u) {
        case Upscaler::Off: return "off";
        case Upscaler::VfxSuperRes: return "VFX VSR";
        case Upscaler::Fsr1: return "FSR 1";
        case Upscaler::Nis: return "NIS";
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

std::string upscaling_name(const Settings& s)
{
    switch (s.upscaler) {
        case Upscaler::Off: return "off";
        case Upscaler::VfxSuperRes: return "VSR " + std::string(to_string(s.super_res_quality));
        case Upscaler::Fsr1:
        case Upscaler::Nis: return std::string(to_string(s.upscaler));
    }
    return "?";
}

bool parse_upscaling(std::string_view name, Settings& s) noexcept
{
    if (name == "off") {
        s.upscaler = Upscaler::Off;
        return true;
    }
    if (name == "fsr") {
        s.upscaler = Upscaler::Fsr1;
        return true;
    }
    if (name == "nis") {
        s.upscaler = Upscaler::Nis;
        return true;
    }
    for (auto q : {SuperResQuality::Bicubic, SuperResQuality::Low, SuperResQuality::Medium, SuperResQuality::High,
                   SuperResQuality::Ultra})
        if (name == to_string(q)) {
            s.upscaler = Upscaler::VfxSuperRes;
            s.super_res_quality = q;
            return true;
        }
    return false;
}

bool parse_frame_gen(std::string_view name, Settings& s) noexcept
{
    if (name == "off") {
        s.frame_gen = FrameGen::Off;
        return true;
    }
    for (auto q : {FrameGenQuality::Low, FrameGenQuality::Medium, FrameGenQuality::High})
        if (name == to_string(q)) {
            s.frame_gen = FrameGen::VfxFrameGen;
            s.frame_gen_quality = q;
            return true;
        }
    return false;
}

} // namespace nitid
