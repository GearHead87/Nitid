#include "app.hpp"

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
    std::println("Keys:");
    for (const auto& b : key_bindings())
        if (!b.keys.empty()) std::println("  {:<8} {}", b.keys, b.help);
}

} // namespace

App::App(AppOptions options) : opts_(std::move(options)), settings_(opts_.settings) {}

App::~App()
{
    engine_.reset();  // before the window goes away
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

int App::run()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

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

    // Window: requested size, or the video's shape at 80% of the screen.
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect usable{0, 0, 1280, 720};
    SDL_GetDisplayUsableBounds(display, &usable);
    int w = opts_.width, h = opts_.height;
    if (w <= 0 || h <= 0) {
        w = int(usable.w * 0.8), h = int(usable.h * 0.8);
    }
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

    source_ = engine_->open_file(opts_.file, opts_.open);
    if (opts_.width <= 0 || opts_.height <= 0) {
        // Re-shape the window to the video's aspect ratio within 80% of the screen.
        double s = std::min(usable.w * 0.8 / source_.width, usable.h * 0.8 / source_.height);
        SDL_SetWindowSize(window_, int(source_.width * s), int(source_.height * s));
    }
    if (opts_.fullscreen) handle(Action::ToggleFullscreen);
    SDL_SyncWindow(window_);
    sync_display();
    print_help();

    auto t_start = clock_type::now();
    auto t_title = t_start, t_stats = t_start;
    bool ended = false;
    bool first_frame = true;
    const bool captures_requested = !opts_.capture_at.empty();
    int frames_after_capture = 0;
    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: running_ = false; break;
                case SDL_EVENT_KEY_DOWN:
                    if (!ev.key.repeat)
                        if (auto a = action_for_key(ev.key.key)) handle(*a);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED: sync_display(); break;
                default: break;
            }
        }
        if (!running_) break;
        if (!opts_.capture_at.empty() && !first_frame && engine_->stats().position_s >= opts_.capture_at.front()) {
            engine_->capture_next_window_frame(std::format("{}_{:.2f}s.ppm", opts_.capture_prefix, opts_.capture_at.front()));
            opts_.capture_at.erase(opts_.capture_at.begin());
        }
        if (!engine_->render_frame()) {
            ended = true;
            break;
        }
        if (first_frame) {
            first_frame = false;
            std::println("First frame on screen {:.2f} s after opening (includes loading the AI models)", seconds_since(t_start));
        }
        if (captures_requested && opts_.capture_at.empty() && ++frames_after_capture > 2) break;  // test run done
        if (seconds_since(t_title) > 0.5) {
            update_title();
            t_title = clock_type::now();
        }
        if (opts_.print_stats && seconds_since(t_stats) >= 1.0) {
            std::println("{}", stats_line(settings_, engine_->stats()));
            t_stats = clock_type::now();
        }
        if (opts_.quit_after_s > 0 && seconds_since(t_start) >= opts_.quit_after_s) break;
    }
    std::println("{}{}", ended ? "End of video.\n" : "", stats_report(source_, settings_, engine_->stats()));
    return 0;
}

void App::update_title()
{
    SDL_SetWindowTitle(window_, window_title(source_, settings_, engine_->stats(), engine_->paused()).c_str());
}

void App::handle(Action a)
{
    using namespace nitid;
    switch (a) {
        case Action::Quit: running_ = false; return;
        case Action::TogglePause: engine_->set_paused(!engine_->paused()); break;
        case Action::ToggleFullscreen:
            fullscreen_ = !fullscreen_;
            SDL_SetWindowFullscreen(window_, fullscreen_);
            break;
        case Action::ToggleUpscaler:
            settings_.upscaler = settings_.upscaler == Upscaler::Off ? Upscaler::VfxSuperRes : Upscaler::Off;
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
        case Action::ResetStats: engine_->reset_stats(); break;
        case Action::PrintStats: std::println("{}", stats_line(settings_, engine_->stats())); break;
        case Action::ShowHelp: print_help(); break;
    }
    engine_->set_settings(settings_);
    update_title();
}

} // namespace nitid_app
