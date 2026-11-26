#include "app.hpp"

#include "ui/overlay.hpp"
#include "ui/stats_text.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <print>

namespace nitid_app {

namespace {

using clock_type = std::chrono::steady_clock;

double seconds_since(clock_type::time_point t0)
{
    return std::chrono::duration<double>(clock_type::now() - t0).count();
}

void print_help()
{
    std::println("Keys (F1 shows them in the window):");
    for (const auto& b : key_bindings())
        if (!b.keys.empty()) std::println("  {:<12} {}", b.keys, b.help);
}

// Keys that repeat while held (seeking, volume); everything else acts once per press.
bool repeats(Action a)
{
    switch (a) {
        case Action::SeekBack:
        case Action::SeekForward:
        case Action::VolumeUp:
        case Action::VolumeDown:
        case Action::AudioEarlier:
        case Action::AudioLater: return true;
        default: return false;
    }
}

} // namespace

App::App(AppOptions options) : opts_(std::move(options)), settings_(opts_.settings) {}

App::~App()
{
    overlay_.reset();  // ImGui's Vulkan objects before the engine's device
    engine_.reset();   // before the window goes away
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void App::sync_display()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    engine_->resize_window(uint32_t(std::max(1, w)), uint32_t(std::max(1, h)));
    const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    if (dm && dm->refresh_rate > 1) engine_->set_refresh_rate(dm->refresh_rate);
}

bool App::open(const std::string& path)
{
    try {
        source_ = engine_->open_file(path, opts_.open);
    } catch (const nitid::Error& e) {
        error_ = e.what();
        has_source_ = false;
        std::println(stderr, "nitid: {}", error_);
        update_title();
        return false;
    }
    has_source_ = true;
    ended_ = false;
    error_.clear();
    std::println("Opened {} ({} {}x{} @ {:.3f} fps, {})", source_.name, source_.codec, source_.width, source_.height,
                 source_.fps, source_.decoder);
    std::println("Sound: {}", source_.audio);
    if (!shaped_ && !fullscreen_ && (opts_.width <= 0 || opts_.height <= 0)) {
        // Re-shape the window to the video's aspect ratio within 80% of the screen (first video only).
        SDL_Rect usable{0, 0, 1280, 720};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable);
        double s = std::min(usable.w * 0.8 / source_.width, usable.h * 0.8 / source_.height);
        SDL_SetWindowSize(window_, int(source_.width * s), int(source_.height * s));
        SDL_SyncWindow(window_);
        sync_display();
    }
    shaped_ = true;
    update_title();
    return true;
}

void App::open_live(const std::string& target)
{
    try {
        source_ = engine_->open_pipewire(target);
    } catch (const nitid::Error& e) {
        error_ = e.what();
        has_source_ = false;
        return;
    }
    has_source_ = true;
    ended_ = false;
    error_.clear();
    std::println("Live input: {}", source_.name);
    update_title();
}

void App::show_open_dialog()
{
    static const SDL_DialogFileFilter filters[] = {
        {"Videos", "mp4;mkv;webm;mov;m4v;avi;ts;m2ts;mts;flv;wmv;mpg;mpeg;ogv"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(
        [](void* self, const char* const* files, int) {
            // May run on another thread: hand the path to the main loop as an event.
            if (!files || !files[0]) return;
            SDL_Event e{};
            e.type = static_cast<App*>(self)->dialog_event_;
            e.user.data1 = SDL_strdup(files[0]);
            SDL_PushEvent(&e);
        },
        this, window_, filters, 2, nullptr, false);
}

void App::apply_settings()
{
    engine_->set_settings(settings_);
    update_title();
}

int App::run()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    dialog_event_ = SDL_RegisterEvents(1);

    // Engine first (no window yet): its Vulkan instance needs SDL's surface extensions.
    Uint32 n = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&n);
    nitid::EngineOptions eo;
    eo.instance_extensions.assign(exts, exts + n);
    eo.validation = opts_.validation;
    engine_ = nitid::Engine::create(eo);
    const auto& caps = engine_->capabilities();
    std::println("GPU {} (driver {}), VFX {}: upscaling {}, frame generation {}", caps.gpu_name, caps.driver_version,
                 caps.vfx_version.empty() ? "not found" : caps.vfx_version, caps.super_res ? "yes" : "no",
                 caps.frame_gen ? "yes" : "no");
    if (!caps.vfx_error.empty()) std::println("  VFX: {}", caps.vfx_error);

    // Window: requested size, or 80% of the screen (re-shaped to the video once one is open).
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect usable{0, 0, 1280, 720};
    SDL_GetDisplayUsableBounds(display, &usable);
    int w = opts_.width, h = opts_.height;
    if (w <= 0 || h <= 0) w = int(usable.w * 0.8), h = int(usable.h * 0.8);
    window_ = SDL_CreateWindow("Nitid", w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        std::println(stderr, "SDL_CreateWindow failed: {}", SDL_GetError());
        return 1;
    }
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(display);
    nitid::WindowTarget target;
    target.create_surface = [this](VkInstance instance) {
        VkSurfaceKHR s = VK_NULL_HANDLE;
        return SDL_Vulkan_CreateSurface(window_, instance, nullptr, &s) ? s : VK_NULL_HANDLE;
    };
    target.width = uint32_t(pw);
    target.height = uint32_t(ph);
    target.refresh_hz = dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60;
    engine_->attach_window(target);
    engine_->set_settings(settings_);
    engine_->set_volume(float(opts_.volume));
    engine_->set_audio_delay(opts_.audio_delay_ms / 1000.0);
    engine_->set_compare_split(float(opts_.compare_split));

    overlay_ = std::make_unique<Overlay>(window_, *engine_);
    ui_.state().visible = opts_.ui == 1 || (opts_.ui == -1 && opts_.capture_at.empty());
    for (const auto& p : opts_.test_panels) {
        if (p == "settings") ui_.state().show_settings = true;
        if (p == "stats") ui_.state().show_stats = true;
        if (p == "help") ui_.state().show_help = true;
    }

    if (opts_.fullscreen) handle(Action::ToggleFullscreen);
    if (!opts_.pipewire.empty()) open_live(opts_.pipewire);
    else if (!opts_.file.empty() && open(opts_.file) && opts_.start_s > 0) engine_->seek(opts_.start_s);
    SDL_SyncWindow(window_);
    sync_display();
    print_help();

    const auto t_start = clock_type::now();
    auto t_title = t_start, t_stats = t_start;
    bool first_frame = true;
    const bool captures_requested = !opts_.capture_at.empty();
    const bool exit_at_end = opts_.exit_at_end || opts_.quit_after_s > 0 || captures_requested;
    int frames_after_capture = 0;
    while (running_) {
        const double now = seconds_since(t_start);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            overlay_->process_event(ev);
            if (ev.type == dialog_event_) {
                auto* path = static_cast<char*>(ev.user.data1);
                open(path);
                SDL_free(path);
                continue;
            }
            switch (ev.type) {
                case SDL_EVENT_QUIT: running_ = false; break;
                case SDL_EVENT_KEY_DOWN:
                    if (overlay_->wants_keyboard()) break;
                    if (auto a = action_for_key(ev.key.key); a && (!ev.key.repeat || repeats(*a))) handle(*a);
                    break;
                case SDL_EVENT_MOUSE_MOTION: ui_.note_mouse_activity(now); break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    ui_.note_mouse_activity(now);
                    if (ev.button.button == SDL_BUTTON_LEFT && ev.button.clicks == 2 && !overlay_->wants_mouse())
                        handle(Action::ToggleFullscreen);
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (ev.drop.data) open(ev.drop.data);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED: sync_display(); break;
                default: break;
            }
        }
        if (!running_) break;

        // On-screen controls for this frame.
        overlay_->begin_frame();
        UiActions ua = ui_.draw(*engine_, settings_, has_source_ ? &source_ : nullptr, ended_, fullscreen_, now, error_);
        overlay_->end_frame();
        if (ua.settings_changed) apply_settings();
        if (ua.open_file) show_open_dialog();
        if (ua.toggle_fullscreen) handle(Action::ToggleFullscreen);
        if (fullscreen_ && !ui_.controls_shown()) SDL_HideCursor();
        else SDL_ShowCursor();

        // Test hooks.
        if (!opts_.capture_at.empty() && !first_frame && engine_->stats().position_s >= opts_.capture_at.front()) {
            engine_->capture_next_window_frame(std::format("{}_{:.2f}s.ppm", opts_.capture_prefix, opts_.capture_at.front()));
            opts_.capture_at.erase(opts_.capture_at.begin());
        }
        if (!opts_.test_seeks.empty() && now >= opts_.test_seeks.front().first) {
            std::println("test seek: {:.2f} s -> {:.2f} s", engine_->position(), opts_.test_seeks.front().second);
            engine_->seek(opts_.test_seeks.front().second);
            opts_.test_seeks.erase(opts_.test_seeks.begin());
        }
        if (!opts_.test_keys.empty() && now >= opts_.test_keys.front().first) {
            const std::string& name = opts_.test_keys.front().second;
            if (auto a = action_for_key(SDL_GetKeyFromName(name.c_str()))) {
                std::println("test key {} at {:.2f} s", name, engine_->position());
                handle(*a);
            } else {
                std::println(stderr, "test key {}: no action", name);
            }
            opts_.test_keys.erase(opts_.test_keys.begin());
        }
        if (!opts_.test_pauses.empty()) {
            auto& [at, len] = opts_.test_pauses.front();
            if (!engine_->paused() && now >= at) {
                std::println("test pause at {:.2f} s for {:.2f} s", engine_->position(), len);
                engine_->set_paused(true);
            } else if (engine_->paused() && now >= at + len) {
                engine_->set_paused(false);
                opts_.test_pauses.erase(opts_.test_pauses.begin());
            }
        }

        const bool playing = engine_->render_frame();
        if (has_source_) {
            if (!playing && !ended_) {
                std::println("End of video.");
                if (exit_at_end) break;
            }
            ended_ = !playing;
        }
        if (first_frame && has_source_) {
            first_frame = false;
            std::println("First frame on screen {:.2f} s after start (includes loading the AI models)", seconds_since(t_start));
        }
        if (captures_requested && opts_.capture_at.empty() && ++frames_after_capture > 2) break;  // test run done
        if (seconds_since(t_title) > 0.5) {
            update_title();
            t_title = clock_type::now();
        }
        if (opts_.print_stats && has_source_ && seconds_since(t_stats) >= 1.0) {
            std::println("{}", stats_line(settings_, engine_->stats()));
            t_stats = clock_type::now();
        }
        if (opts_.quit_after_s > 0 && seconds_since(t_start) >= opts_.quit_after_s) break;
    }
    if (has_source_) std::println("{}", stats_report(source_, settings_, engine_->stats()));
    return 0;
}

void App::update_title()
{
    if (!has_source_) {
        SDL_SetWindowTitle(window_, "Nitid");
        return;
    }
    SDL_SetWindowTitle(window_, window_title(source_, settings_, engine_->stats(), engine_->paused()).c_str());
}

void App::handle(Action a)
{
    using namespace nitid;
    switch (a) {
        case Action::Quit: running_ = false; return;
        case Action::OpenFile: show_open_dialog(); return;
        case Action::ToggleUi: ui_.state().visible = !ui_.state().visible; return;
        case Action::ToggleStatsPanel: ui_.state().show_stats = !ui_.state().show_stats; return;
        case Action::ToggleSettingsPanel: ui_.state().show_settings = !ui_.state().show_settings; return;
        case Action::ShowHelp: ui_.state().show_help = !ui_.state().show_help; return;
        case Action::ToggleFullscreen:
            fullscreen_ = !fullscreen_;
            SDL_SetWindowFullscreen(window_, fullscreen_);
            return;
        default: break;
    }
    if (!has_source_) return;  // the rest needs a video
    switch (a) {
        case Action::TogglePause:
            if (ended_) {
                engine_->seek(0);
                engine_->set_paused(false);
            } else {
                engine_->set_paused(!engine_->paused());
            }
            break;
        case Action::SeekBack: engine_->seek(engine_->position() - 5); break;
        case Action::SeekForward: engine_->seek(engine_->position() + 5); break;
        case Action::SeekBackLong: engine_->seek(engine_->position() - 60); break;
        case Action::SeekForwardLong: engine_->seek(engine_->position() + 60); break;
        case Action::SeekStart: engine_->seek(0); break;
        case Action::VolumeUp: engine_->set_volume(std::min(2.0f, engine_->volume() + 0.05f)); break;
        case Action::VolumeDown: engine_->set_volume(std::max(0.0f, engine_->volume() - 0.05f)); break;
        case Action::ToggleMute: engine_->set_muted(!engine_->muted()); break;
        case Action::AudioEarlier: engine_->set_audio_delay(engine_->audio_delay() - 0.010); break;
        case Action::AudioLater: engine_->set_audio_delay(engine_->audio_delay() + 0.010); break;
        case Action::CycleUpscaler:
            settings_.upscaler = settings_.upscaler == Upscaler::VfxSuperRes ? Upscaler::Fsr1
                                 : settings_.upscaler == Upscaler::Fsr1      ? Upscaler::Nis
                                 : settings_.upscaler == Upscaler::Nis       ? Upscaler::Off
                                                                             : Upscaler::VfxSuperRes;
            break;
        case Action::CycleUpscalerQuality: {
            int q = int(settings_.super_res_quality) + 1;
            settings_.super_res_quality = SuperResQuality(q > int(SuperResQuality::Ultra) ? int(SuperResQuality::Low) : q);
            break;
        }
        case Action::ToggleFrameGen:
            settings_.frame_gen = settings_.frame_gen == FrameGen::Off ? FrameGen::VfxFrameGen : FrameGen::Off;
            break;
        case Action::CycleFrameGenQuality:
            settings_.frame_gen_quality = FrameGenQuality((int(settings_.frame_gen_quality) + 1) % 3);
            break;
        case Action::MultiplierMatchDisplay: settings_.frame_gen_multiplier = 0; break;
        case Action::Multiplier2: settings_.frame_gen_multiplier = 2; break;
        case Action::Multiplier3: settings_.frame_gen_multiplier = 3; break;
        case Action::Multiplier4: settings_.frame_gen_multiplier = 4; break;
        case Action::ToggleSceneCuts: settings_.scene_cut_detection = !settings_.scene_cut_detection; break;
        case Action::ToggleCompare: settings_.compare = !settings_.compare; break;
        case Action::ResetStats: engine_->reset_stats(); break;
        case Action::PrintStats: std::println("{}", stats_line(settings_, engine_->stats())); break;
        default: break;
    }
    apply_settings();
}

} // namespace nitid_app
