#include <nitid/settings.hpp>

namespace nitid {

std::string_view to_string(Upscaler u) noexcept
{
    switch (u) {
        case Upscaler::Off: return "off";
        case Upscaler::VfxSuperRes: return "VFX VSR";
        case Upscaler::Fsr1: return "FSR 1";
        case Upscaler::Nis: return "NIS";
        case Upscaler::Dlss: return "DLSS";
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
        case FrameGen::DlssFrameGen: return "DLSS frame gen";
    }
    return "?";
}

std::string_view to_string(Cleanup c) noexcept
{
    switch (c) {
        case Cleanup::Off: return "off";
        case Cleanup::Denoise: return "denoise";
        case Cleanup::Deblur: return "deblur";
    }
    return "?";
}

std::string_view to_string(LiveMode m) noexcept
{
    switch (m) {
        case LiveMode::LowLatency: return "low latency";
        case LiveMode::Smooth: return "smooth";
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

std::string_view to_string(DlssPreset p) noexcept
{
    switch (p) {
        case DlssPreset::Auto: return "auto";
        case DlssPreset::J: return "J";
        case DlssPreset::K: return "K";
        case DlssPreset::L: return "L";
        case DlssPreset::M: return "M";
    }
    return "?";
}

std::string_view to_string(DlssFrameGenMode m) noexcept
{
    switch (m) {
        case DlssFrameGenMode::Full: return "full";
        case DlssFrameGenMode::Lite: return "lite";
    }
    return "?";
}

std::string frame_gen_name(const Settings& s)
{
    switch (s.frame_gen) {
        case FrameGen::Off: return "off";
        case FrameGen::VfxFrameGen: return "VFX " + std::string(to_string(s.frame_gen_quality));
        case FrameGen::DlssFrameGen: return s.dlss_frame_gen_mode == DlssFrameGenMode::Lite ? "DLSS lite" : "DLSS";
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
        case Upscaler::Dlss:
            return s.dlss_preset == DlssPreset::Auto ? "DLSS" : "DLSS preset " + std::string(to_string(s.dlss_preset));
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
    if (name.starts_with("dlss")) {
        auto rest = name.substr(4);
        DlssPreset p = DlssPreset::Auto;
        if (!rest.empty()) {
            if (rest.size() != 2 || rest[0] != '-') return false;
            const char c = rest[1];
            if (c == 'j') p = DlssPreset::J;
            else if (c == 'k') p = DlssPreset::K;
            else if (c == 'l') p = DlssPreset::L;
            else if (c == 'm') p = DlssPreset::M;
            else return false;
        }
        s.upscaler = Upscaler::Dlss;
        s.dlss_preset = p;
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
    if (name == "dlss" || name == "dlss-lite") {
        s.frame_gen = FrameGen::DlssFrameGen;
        s.dlss_frame_gen_mode = name == "dlss" ? DlssFrameGenMode::Full : DlssFrameGenMode::Lite;
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

bool parse_cleanup(std::string_view name, Settings& s) noexcept
{
    auto dash = name.find('-');
    std::string_view kind = name.substr(0, dash);
    if (kind == "off" && dash == std::string_view::npos) {
        s.cleanup = Cleanup::Off;
        return true;
    }
    Cleanup c;
    if (kind == "denoise") c = Cleanup::Denoise;
    else if (kind == "deblur") c = Cleanup::Deblur;
    else return false;
    SuperResQuality q = s.cleanup_strength;
    if (dash != std::string_view::npos) {
        std::string_view st = name.substr(dash + 1);
        bool ok = false;
        for (auto v : {SuperResQuality::Low, SuperResQuality::Medium, SuperResQuality::High, SuperResQuality::Ultra})
            if (st == to_string(v)) q = v, ok = true;
        if (!ok) return false;
    }
    s.cleanup = c;
    s.cleanup_strength = q;
    return true;
}

} // namespace nitid
