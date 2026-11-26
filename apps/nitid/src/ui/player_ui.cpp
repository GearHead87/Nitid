#include "ui/player_ui.hpp"

#include "key_bindings.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

namespace nitid_app {

namespace {

constexpr double kHideAfterS = 2.5;

std::string clock_text(double s)
{
    if (s < 0 || !std::isfinite(s)) s = 0;
    int t = int(s);
    if (t >= 3600) return std::format("{}:{:02}:{:02}", t / 3600, t / 60 % 60, t % 60);
    return std::format("{}:{:02}", t / 60, t % 60);
}

// A combo over a fixed list; returns true when the choice changed.
template <class T, size_t N>
bool choice(const char* label, T& value, const std::pair<T, const char*> (&items)[N], const bool* enabled = nullptr)
{
    const char* preview = "?";
    for (auto& [v, name] : items)
        if (v == value) preview = name;
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (size_t i = 0; i < N; ++i) {
            bool ok = !enabled || enabled[i];
            if (ImGui::Selectable(items[i].second, items[i].first == value, ok ? 0 : ImGuiSelectableFlags_Disabled)) {
                changed = items[i].first != value;
                value = items[i].first;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void dim_text(const std::string& s)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}

} // namespace

UiActions PlayerUi::draw(nitid::Engine& e, nitid::Settings& s, const nitid::SourceInfo* src, bool ended, bool fullscreen,
                         double now_s, const std::string& error)
{
    UiActions a;
    if (!state_.visible) {
        controls_shown_ = false;
        return a;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const nitid::Stats st = e.stats();

    if (!src) {
        // Nothing open yet: a centred prompt.
        ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.5f, vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("##open", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted("Nitid: AI upscaling and frame generation for video");
        ImGui::Spacing();
        ImGui::TextUnformatted("Drop a video file on this window, or");
        if (ImGui::Button("Open a file...")) a.open_file = true;
        ImGui::SameLine();
        if (ImGui::Button("Capture the screen...")) a.capture_screen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows a screen or window (chosen in the desktop's dialog) enhanced in this window.\n"
                              "Put Nitid on another screen than the one it captures.");
        if (!error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", error.c_str());
        }
        ImGui::End();
    }

    // Auto-hide while playing and the mouse is still (never while paused, at the end, or over a panel).
    const bool idle = now_s - last_activity_s_ > kHideAfterS;
    controls_shown_ = !src || !idle || e.paused() || ended || ImGui::GetIO().WantCaptureMouse || !error.empty();
    if (src && controls_shown_) control_bar(e, *src, ended, fullscreen, a);
    if (src && !error.empty()) {
        ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.5f, vp->Size.y * 0.1f), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::Begin("##error", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", error.c_str());
        ImGui::End();
    }
    if (src && s.compare) compare_overlay(e, st);
    if (state_.show_settings) settings_panel(e, s, st, a);
    if (state_.show_stats && src) stats_panel(*src, st, s);
    if (state_.show_help) help_panel();
    return a;
}

void PlayerUi::control_bar(nitid::Engine& e, const nitid::SourceInfo& src, bool ended, bool fullscreen, UiActions& a)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight() + style.WindowPadding.y * 2;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::Begin("##controls", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (src.live) {
        // Live input: no position or seek bar; what is being captured instead.
        const nitid::Stats st = e.stats();
        if (ImGui::Button(e.paused() ? "Play" : "Pause", ImVec2(ImGui::CalcTextSize("Replay").x + style.FramePadding.x * 2, 0)))
            e.set_paused(!e.paused());
        ImGui::SameLine();
        ImGui::TextColored(st.capture_failed ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.35f, 0.3f, 1), "LIVE");
        ImGui::SameLine();
        ImGui::TextUnformatted(std::format("{}  |  {:.0f} fps in, {:.0f} ms to screen", st.capture_status, st.capture_fps,
                                           st.live_latency_ms).c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - (ImGui::CalcTextSize("Settings").x + ImGui::CalcTextSize("Stats").x +
                                                          ImGui::CalcTextSize("?").x + ImGui::CalcTextSize("Fullscreen").x +
                                                          style.FramePadding.x * 8 + style.ItemSpacing.x * 3));
        if (ImGui::Button("Settings")) state_.show_settings = !state_.show_settings;
        ImGui::SameLine();
        if (ImGui::Button("Stats")) state_.show_stats = !state_.show_stats;
        ImGui::SameLine();
        if (ImGui::Button("?")) state_.show_help = !state_.show_help;
        ImGui::SameLine();
        if (ImGui::Button(fullscreen ? "Window" : "Fullscreen")) a.toggle_fullscreen = true;
        ImGui::End();
        return;
    }
    const bool playing = !e.paused() && !ended;
    if (ImGui::Button(playing ? "Pause" : (ended ? "Replay" : "Play"), ImVec2(ImGui::CalcTextSize("Replay").x + style.FramePadding.x * 2, 0))) {
        if (ended) {
            e.seek(0);
            e.set_paused(false);
        } else {
            e.set_paused(!e.paused());
        }
    }
    ImGui::SameLine();
    const double pos = e.position(), dur = src.duration_s;
    ImGui::TextUnformatted(std::format("{} / {}", clock_text(pos), clock_text(dur)).c_str());
    ImGui::SameLine();

    // Right-hand group: volume, panels, fullscreen. Measured first so the seek bar takes the rest.
    const float vol_w = ImGui::GetFontSize() * 6;
    const float btn = ImGui::GetFrameHeight();
    auto text_btn_w = [&](const char* t) { return ImGui::CalcTextSize(t).x + style.FramePadding.x * 2; };
    const float right = text_btn_w("Mute") + vol_w + text_btn_w("Settings") + text_btn_w("Stats") + text_btn_w("?") +
                        text_btn_w(fullscreen ? "Window" : "Fullscreen") + style.ItemSpacing.x * 6;
    float bar_w = ImGui::GetContentRegionAvail().x - right;
    if (dur > 0 && bar_w > btn * 3) {
        float p = float(pos);
        ImGui::SetNextItemWidth(bar_w);
        if (ImGui::SliderFloat("##seek", &p, 0.0f, float(dur), "")) {
            // Seek while dragging, at most every 0.15 s (4K seeks take ~0.15 s); the release always seeks.
            if (ImGui::GetTime() - last_seek_s_ > 0.15) {
                e.seek(p);
                last_seek_s_ = ImGui::GetTime();
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) e.seek(p);
        if (ImGui::IsItemHovered()) {
            float mx = ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x;
            float t = std::clamp(mx / ImGui::GetItemRectSize().x, 0.0f, 1.0f) * float(dur);
            ImGui::SetTooltip("%s", clock_text(t).c_str());
        }
    } else {
        ImGui::Dummy(ImVec2(std::max(1.0f, bar_w), btn));
    }
    ImGui::SameLine();
    bool muted = e.muted();
    if (!src.has_audio) ImGui::BeginDisabled();
    if (ImGui::Button(muted ? "Muted" : "Mute", ImVec2(text_btn_w("Muted"), 0))) e.set_muted(!muted);
    ImGui::SameLine();
    float vol = e.volume() * 100;
    ImGui::SetNextItemWidth(vol_w);
    if (ImGui::SliderFloat("##vol", &vol, 0, 200, "%.0f %%")) e.set_volume(vol / 100);
    if (!src.has_audio) ImGui::EndDisabled();
    if (!src.has_audio && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("No sound: %s", src.audio.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Settings")) state_.show_settings = !state_.show_settings;
    ImGui::SameLine();
    if (ImGui::Button("Stats")) state_.show_stats = !state_.show_stats;
    ImGui::SameLine();
    if (ImGui::Button("?")) state_.show_help = !state_.show_help;
    ImGui::SameLine();
    if (ImGui::Button(fullscreen ? "Window" : "Fullscreen")) a.toggle_fullscreen = true;
    ImGui::End();
}

void PlayerUi::settings_panel(nitid::Engine& e, nitid::Settings& s, const nitid::Stats& st, UiActions& a)
{
    using namespace nitid;
    const auto& caps = e.capabilities();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - ImGui::GetFontSize(), ImGui::GetFontSize()), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 24, 0), ImVec2(ImGui::GetFontSize() * 24, FLT_MAX));
    if (!ImGui::Begin("Settings", &state_.show_settings, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const Settings before = s;

    ImGui::SeparatorText("Upscaling");
    static const std::pair<Upscaler, const char*> ups[] = {
        {Upscaler::VfxSuperRes, "VSR (NVIDIA AI)"}, {Upscaler::Fsr1, "FSR 1 (AMD)"}, {Upscaler::Nis, "NIS (NVIDIA)"}, {Upscaler::Off, "Off"}};
    const bool ups_ok[] = {caps.super_res, true, true, true};
    choice("Upscaler", s.upscaler, ups, ups_ok);
    if (s.upscaler == Upscaler::VfxSuperRes) {
        static const std::pair<SuperResQuality, const char*> q[] = {
            {SuperResQuality::Low, "Low"}, {SuperResQuality::Medium, "Medium"}, {SuperResQuality::High, "High"}, {SuperResQuality::Ultra, "Ultra"}};
        choice("Quality##vsr", s.super_res_quality, q);
    } else if (s.upscaler == Upscaler::Fsr1 || s.upscaler == Upscaler::Nis) {
        ImGui::SliderFloat("Sharpness", &s.sharpness, 0.0f, 1.0f, "%.2f");
    }
    if (st.upscaler_active)
        dim_text(std::format("{}x{} -> {}x{}, {:.2f} ms per frame", st.source_width, st.source_height, st.work_width,
                             st.work_height, st.avg.upscale_ms));
    else
        dim_text(std::format("Not running: {}", st.upscaler_note));

    ImGui::SeparatorText("Clean-up (video at its own size)");
    static const std::pair<Cleanup, const char*> cl[] = {{Cleanup::Off, "Off"}, {Cleanup::Denoise, "Denoise (VFX)"}, {Cleanup::Deblur, "Deblur (VFX)"}};
    const bool cl_ok[] = {true, caps.super_res, caps.super_res};
    choice("Clean-up", s.cleanup, cl, cl_ok);
    if (s.cleanup != Cleanup::Off) {
        static const std::pair<SuperResQuality, const char*> q[] = {
            {SuperResQuality::Low, "Low"}, {SuperResQuality::Medium, "Medium"}, {SuperResQuality::High, "High"}, {SuperResQuality::Ultra, "Ultra"}};
        choice("Strength", s.cleanup_strength, q);
        if (st.cleanup_active) dim_text(std::format("{:.2f} ms per frame", st.avg.upscale_ms));
        else dim_text(std::format("Not running: {}", st.cleanup_note));
    }

    ImGui::SeparatorText("Frame generation");
    bool fg = s.frame_gen == FrameGen::VfxFrameGen;
    if (!caps.frame_gen) ImGui::BeginDisabled();
    if (ImGui::Checkbox("NVIDIA VFX frame generation", &fg)) s.frame_gen = fg ? FrameGen::VfxFrameGen : FrameGen::Off;
    if (!caps.frame_gen) ImGui::EndDisabled();
    if (fg) {
        static const std::pair<FrameGenQuality, const char*> q[] = {
            {FrameGenQuality::Low, "Low"}, {FrameGenQuality::Medium, "Medium"}, {FrameGenQuality::High, "High"}};
        choice("Quality##fg", s.frame_gen_quality, q);
        std::string per_refresh = std::format("Every refresh ({:.0f} Hz)", st.display_hz);
        const std::pair<int, const char*> m[] = {{0, per_refresh.c_str()}, {2, "2x"}, {3, "3x"}, {4, "4x"}};
        choice("Frames", s.frame_gen_multiplier, m);
        ImGui::Checkbox("Scene-cut detection", &s.scene_cut_detection);
    }
    if (st.frame_gen_active)
        dim_text(std::format("{:.2f} ms per generated frame, {:.0f} fps on screen", st.avg.frame_gen_ms, st.output_fps));
    else
        dim_text(std::format("Not running: {}", caps.frame_gen ? st.frame_gen_note : "needs an RTX 40 / 50 GPU and the VFX runtime"));

    ImGui::SeparatorText("View");
    ImGui::Checkbox("Compare: original | Nitid", &s.compare);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left: the video as a normal player shows it. Right: Nitid.\nDrag on the picture to move the line. Key: X");

    ImGui::SeparatorText("Screen capture");
    static const std::pair<LiveMode, const char*> lm[] = {{LiveMode::LowLatency, "Low latency (newest frame)"},
                                                         {LiveMode::Smooth, "Smooth (frame generation)"}};
    choice("Live mode", s.live_mode, lm);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Smooth generates frames between captured ones: smoother motion, about one captured frame later. Key: L");
    if (st.live) dim_text(std::format("{} | {:.0f} ms from capture to screen", st.capture_status, st.live_latency_ms));

    ImGui::SeparatorText("Sound");
    float delay = float(e.audio_delay() * 1000);
    if (ImGui::SliderFloat("A/V offset", &delay, -500, 500, "%+.0f ms")) e.set_audio_delay(delay / 1000);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("+ plays the sound later. Keys: , and .");
    if (st.audio_active)
        dim_text(std::format("In sync within {:+.1f} ms (speaker delay {:.0f} ms)", st.av_sync_ms, st.audio_latency_ms));

    if (!(s == before)) a.settings_changed = true;
    ImGui::End();
}

void PlayerUi::stats_panel(const nitid::SourceInfo& src, const nitid::Stats& st, const nitid::Settings& s)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stats", &state_.show_stats, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%s", src.name.c_str());
    ImGui::Text("%s %ux%u @ %.3f fps, %s", src.codec.c_str(), src.width, src.height, src.fps, src.decoder.c_str());
    ImGui::Separator();
    ImGui::Text("Work size   %ux%u   window %ux%u", st.work_width, st.work_height, st.output_width, st.output_height);
    ImGui::Text("Upscaler    %s", st.upscaler_active ? nitid::upscaling_name(s).c_str() : "off");
    ImGui::Text("Frame gen   %s", st.frame_gen_active ? "on" : "off");
    ImGui::Text("Display     %.1f fps of %.0f Hz, %llu missed refreshes", st.output_fps, st.display_hz,
                (unsigned long long)st.missed_refreshes);
    ImGui::Text("Frames      %llu shown, %llu generated, %llu skipped, %llu scene cuts", (unsigned long long)st.output_frames,
                (unsigned long long)st.generated_frames, (unsigned long long)st.skipped_frames, (unsigned long long)st.scene_cuts);
    ImGui::Separator();
    if (ImGui::BeginTable("times", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto row = [](const char* what, double ms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(what);
            ImGui::TableNextColumn();
            ImGui::Text("%6.2f ms", ms);
        };
        row("Decode + colour (per video frame)", st.avg.decode_ms);
        row("Upload", st.avg.upload_ms);
        row("Upscale", st.avg.upscale_ms);
        row("Frame generation (per generated frame)", st.avg.frame_gen_ms);
        row("Copy to window", st.avg.output_ms);
        row("Present to screen", st.display_latency_ms);
        ImGui::EndTable();
    }
    ImGui::Separator();
    if (st.audio_active)
        ImGui::Text("Sound       A/V %+.1f ms (max %.1f), speed %.4f, %llu resyncs, speaker delay %.0f ms", st.av_sync_ms,
                    st.av_sync_max_ms, st.audio_speed, (unsigned long long)st.audio_resyncs, st.audio_latency_ms);
    else
        ImGui::Text("Sound       %s", src.audio.c_str());
    ImGui::End();
}

void PlayerUi::compare_overlay(nitid::Engine& e, const nitid::Stats& st)
{
    if (!st.video_w || !st.video_h) return;
    const ImGuiIO& io = ImGui::GetIO();
    // The engine reports the picture in framebuffer pixels; ImGui works in window units.
    const float sx = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.0f;
    const float sy = io.DisplayFramebufferScale.y > 0 ? io.DisplayFramebufferScale.y : 1.0f;
    const float x0 = st.video_x / sx, y0 = st.video_y / sy, w = st.video_w / sx, h = st.video_h / sy;
    // Drag anywhere on the picture (not on a panel) to move the line.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.WantCaptureMouse && io.MousePos.x >= x0 && io.MousePos.x <= x0 + w &&
        io.MousePos.y >= y0 && io.MousePos.y <= y0 + h)
        e.set_compare_split((io.MousePos.x - x0) / w);
    const float x = x0 + e.compare_split() * w;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddLine(ImVec2(x, y0), ImVec2(x, y0 + h), IM_COL32(255, 255, 255, 200), 2.0f);
    const float pad = ImGui::GetFontSize() * 0.5f;
    auto label = [&](const char* t, float lx) {
        ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddRectFilled(ImVec2(lx - pad * 0.5f, y0 + pad * 0.5f), ImVec2(lx + ts.x + pad * 0.5f, y0 + pad * 1.5f + ts.y),
                          IM_COL32(0, 0, 0, 150), 4.0f);
        dl->AddText(ImVec2(lx, y0 + pad), IM_COL32(255, 255, 255, 230), t);
    };
    label("Original", x - ImGui::CalcTextSize("Original").x - pad * 1.5f);
    label("Nitid", x + pad * 1.5f);
}

void PlayerUi::help_panel()
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keys", &state_.show_help, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        for (const auto& b : key_bindings()) {
            if (b.keys.empty()) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.keys.data(), b.keys.data() + b.keys.size());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.help.data(), b.help.data() + b.help.size());
        }
        ImGui::EndTable();
    }
    dim_text("Mouse: move to show the controls, double-click for fullscreen, drop a file to open it.");
    ImGui::End();
}

} // namespace nitid_app
