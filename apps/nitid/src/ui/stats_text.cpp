#include "ui/stats_text.hpp"

#include <format>

namespace nitid_app {

namespace {

std::string multiplier_text(const nitid::Settings& s)
{
    return s.frame_gen_multiplier > 0 ? std::format("{}x", s.frame_gen_multiplier) : "per refresh";
}

std::string upscaler_text(const nitid::Settings& s, const nitid::Stats& st)
{
    if (st.upscaler_active) return std::format("{} {:.2f} ms", nitid::upscaling_name(s), st.avg.upscale_ms);
    return std::format("upscaling off ({})", st.upscaler_note);
}

std::string frame_gen_text(const nitid::Settings& s, const nitid::Stats& st)
{
    if (st.frame_gen_active)
        return std::format("FG {} {} {:.2f} ms", nitid::to_string(s.frame_gen_quality), multiplier_text(s), st.avg.frame_gen_ms);
    return std::format("FG off ({})", st.frame_gen_note);
}

} // namespace

std::string window_title(const nitid::SourceInfo& src, const nitid::Settings& s, const nitid::Stats& st, bool paused)
{
    return std::format("Nitid - {}{} | {}x{} -> {}x{} | {} | {} | {:.0f} fps", paused ? "[paused] " : "", src.name,
                       st.source_width, st.source_height, st.work_width, st.work_height, upscaler_text(s, st),
                       frame_gen_text(s, st), st.output_fps);
}

std::string stats_line(const nitid::Settings& s, const nitid::Stats& st)
{
    return std::format("t={:.1f}s | {}x{}@{:.3f} -> {}x{} (window {}x{} @ {:.0f} Hz) | {} | {} | out {:.1f} fps | "
                       "frames {} (generated {}) | skipped {} | missed refreshes {} | cuts {}",
                       st.position_s, st.source_width, st.source_height, st.source_fps, st.work_width, st.work_height,
                       st.output_width, st.output_height, st.display_hz, upscaler_text(s, st), frame_gen_text(s, st),
                       st.output_fps, st.output_frames, st.generated_frames, st.skipped_frames, st.missed_refreshes,
                       st.scene_cuts);
}

std::string stats_report(const nitid::SourceInfo& src, const nitid::Settings& s, const nitid::Stats& st)
{
    std::string r;
    r += std::format("Source:   {} ({} {}x{} @ {:.3f} fps, decoder {})\n", src.name, src.codec, src.width, src.height,
                     src.fps, src.decoder);
    r += std::format("Work:     {}x{}   Output: {}x{}   Display: {:.2f} Hz\n", st.work_width, st.work_height,
                     st.output_width, st.output_height, st.display_hz);
    r += std::format("Upscale:  {}\n", upscaler_text(s, st));
    r += std::format("FrameGen: {}   scene-cut detection {}\n", frame_gen_text(s, st), s.scene_cut_detection ? "on" : "off");
    r += std::format("Played:   {:.1f} s, {} output frames ({} generated), {} real frames shown, {} skipped, "
                     "{} scene cuts\n",
                     st.position_s, st.output_frames, st.generated_frames, st.source_frames, st.skipped_frames, st.scene_cuts);
    r += std::format("Display:  {:.1f} fps measured, {} missed refreshes\n", st.output_fps, st.missed_refreshes);
    r += std::format("Avg ms:   decode+convert {:.2f} | upload {:.2f} | upscale {:.2f} | frame gen {:.2f} | output {:.2f}\n",
                     st.avg.decode_ms, st.avg.upload_ms, st.avg.upscale_ms, st.avg.frame_gen_ms, st.avg.output_ms);
    return r;
}

} // namespace nitid_app
