#include <nitid/engine.hpp>
#include <nitid/error.hpp>

#include "audio/audio_player.hpp"
#include "core/log.hpp"
#include "core/running_average.hpp"
#include "dlss/runtime.hpp"
#include "gpu/cuda_context.hpp"
#include "gpu/kernels.hpp"
#include "gpu/vulkan_context.hpp"
#include "input/ffmpeg_file_source.hpp"
#include "input/pipewire_capture.hpp"
#include "input/screencast_portal.hpp"
#include "output/swapchain_presenter.hpp"
#include "pipeline/frame_clock.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/render_ahead.hpp"
#include "vfx/vfx_api.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>

namespace nitid {

using pipeline::Size;

struct Engine::Impl {
    EngineOptions options;
    Capabilities caps;
    std::unique_ptr<gpu::VulkanContext> vk;
    std::unique_ptr<gpu::CudaContext> cuda;
    std::shared_ptr<const vfx::Api> vfx;
    std::unique_ptr<gpu::Kernels> kernels;
    std::unique_ptr<dlss::Runtime> dlss;  // DLSS module (optional); started once the device exists

    std::unique_ptr<output::SwapchainPresenter> presenter;
    std::unique_ptr<input::FrameSource> source;
    input::PipeWireCapture* capture = nullptr;  // = source when live
    std::unique_ptr<pipeline::Pipeline> pipe;
    std::unique_ptr<audio::AudioPlayer> audio;
    OpenOptions open_options;
    SourceInfo info;
    float volume = 1;
    bool muted = false;
    double audio_delay = 0;

    Settings settings;
    bool paused = false;
    double refresh_hz = 60;
    pipeline::DisplayClock clock;
    std::unique_ptr<pipeline::RenderAhead> ahead;  // window output: the pipeline runs on its own thread
    bool waiting_first = false;                     // after open / seek: waiting for the worker's first frame
    float compare_split = 0.5f;
    double live_latency_ms = 0;
    std::optional<pipeline::Output> last_live;  // live: shown while paused or until a new frame arrives
    uint64_t last_live_gen = 0;                 // pipeline buffers it points into
    double live_start_s = 0;                    // arrival time of the first live frame
    std::optional<std::chrono::steady_clock::time_point> seek_started;  // logs how long a seek took

    Size headless_box;
    int64_t headless_n = 0;

    RunningAverage output_ms;
    uint64_t output_frames = 0;
    double position_s = 0;
    double fps_window_start = -1;
    uint64_t fps_window_frames = 0;
    double measured_fps = 0;

    SourceInfo open_live(input::PipeWireCapture::Connect connect, Engine& self)
    {
        if (!presenter) throw Error("live input needs a window (attach_window first)");
        cuda->make_current();
        self.close_source();
        auto cap = std::make_unique<input::PipeWireCapture>(std::move(connect), *vk, *cuda, *kernels);
        capture = cap.get();
        source = std::move(cap);
        pipe = std::make_unique<pipeline::Pipeline>(*cuda, vfx, caps.super_res, caps.frame_gen, gpu_extras());
        pipe->set_source(source.get());
        info = source->info();
        info.live = true;
        info.audio = "not captured (picture only)";
        live_latency_ms = 0;
        live_start_s = 0;
        position_s = 0;
        last_live.reset();
        self.reset_stats();
        return info;
    }

    pipeline::GpuExtras gpu_extras() const
    {
        pipeline::GpuExtras g;
        g.vk = vk.get();
        g.kernels = kernels.get();
        g.dlss = dlss && dlss->started() ? dlss.get() : nullptr;
        g.dlss_note = caps.dlss_error;
        return g;
    }

    void ensure_device(VkSurfaceKHR surface)
    {
        if (vk->has_device()) return;
        vk->create_device(surface, dlss ? dlss->device_extensions() : std::vector<std::string>{});
        caps.optical_flow = vk->optical_flow;
        if (!dlss) return;
        std::string why;
        if (!dlss->start(*vk, &why)) {
            caps.dlss_error = why;
            log::warn("DLSS not available: {}", why);
            return;
        }
        caps.dlss_version = dlss->sdk_version();
        caps.dlss_super_res = dlss->caps().super_res != 0;
        caps.dlss_frame_gen = dlss->caps().frame_gen != 0;
        caps.dlss_max_multiplier = caps.dlss_frame_gen ? dlss->caps().max_generated + 1 : 0;
        if (!caps.dlss_super_res && !caps.dlss_frame_gen) caps.dlss_error = "not supported on this GPU";
    }

    // Effect availability: create (not load) each effect once.
    void probe_effects()
    {
        std::string err;
        vfx = vfx::load_api(options.vfx_runtime_dir, &err);
        if (!vfx) {
            caps.vfx_error = err;
            return;
        }
        caps.vfx_version = vfx->version;
        for (auto [name, flag] : {std::pair{"VideoSuperRes", &caps.super_res}, std::pair{"VideoFrameGeneration", &caps.frame_gen}}) {
            NvVFX_Handle h = nullptr;
            NvCV_Status st = vfx->CreateEffect(name, &h);
            if (st == NVCV_SUCCESS && h) {
                *flag = true;
                vfx->DestroyEffect(h);
            } else {
                log::warn("VFX effect {} not available ({} {})", name, int(st), vfx->ErrorString(st));
            }
        }
    }

    int effective_multiplier(bool window) const
    {
        if (!pipe) return 1;
        Stats s;
        pipe->fill_stats(s);
        if (!s.frame_gen_active) return window ? 0 : 1;  // window: 0 = no rounding needed (no generation)
        int m = settings.frame_gen_multiplier;
        if (m <= 0) return window ? 0 : 2;
        if (s.frame_gen_max_multiplier > 0) m = std::clamp(m, 2, s.frame_gen_max_multiplier);  // DLSS: whole steps
        return m;
    }

    void count_output()
    {
        ++output_frames;
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (fps_window_start < 0) fps_window_start = now;
        ++fps_window_frames;
        if (now - fps_window_start >= 1.0) {
            measured_fps = double(fps_window_frames) / (now - fps_window_start);
            fps_window_start = now;
            fps_window_frames = 0;
        }
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine()
{
    if (!impl_) return;
    // Order matters: stop decoding, release GPU users, then the contexts.
    impl_->ahead.reset();  // stops the worker before the pipeline goes
    impl_->audio.reset();
    impl_->pipe.reset();
    impl_->source.reset();
    impl_->presenter.reset();
    impl_->kernels.reset();
    impl_->dlss.reset();  // NGX shutdown needs the device
    impl_->cuda.reset();
    impl_->vk.reset();
}

std::unique_ptr<Engine> Engine::create(const EngineOptions& options)
{
    std::unique_ptr<Engine> e(new Engine());
    Impl& m = *e->impl_;
    m.options = options;
    std::string why;
    m.dlss = dlss::Runtime::load(&why);
    if (!m.dlss) {
        m.caps.dlss_error = why;
        log::info("DLSS: {}", why);
    }
    auto inst_exts = options.instance_extensions;
    if (m.dlss)
        for (auto& e : m.dlss->instance_extensions()) inst_exts.push_back(e);
    m.vk = std::make_unique<gpu::VulkanContext>(inst_exts, options.validation);
    m.cuda = std::make_unique<gpu::CudaContext>(m.vk->device_uuid);
    m.kernels = std::make_unique<gpu::Kernels>();
    m.caps.gpu_name = m.vk->props.deviceName;
    m.caps.driver_version = m.vk->driver_version();
    m.probe_effects();
    // Headless use (no window to wait for): create the device now, so DLSS availability is known right away.
    if (options.instance_extensions.empty()) m.ensure_device(VK_NULL_HANDLE);
    return e;
}

const Capabilities& Engine::capabilities() const noexcept { return impl_->caps; }

SourceInfo Engine::open_file(const std::string& path, const OpenOptions& options)
{
    Impl& m = *impl_;
    m.cuda->make_current();
    m.ensure_device(VK_NULL_HANDLE);
    close_source();
    m.open_options = options;
    input::GpuDecode gd{m.cuda->ctx, m.kernels.get()};
    auto file = std::make_unique<input::FfmpegFileSource>(path, options, int(m.cuda->device), &gd);
    file->set_scene_cut_detection(m.settings.scene_cut_detection);
    m.source = std::move(file);
    m.pipe = std::make_unique<pipeline::Pipeline>(*m.cuda, m.vfx, m.caps.super_res, m.caps.frame_gen, m.gpu_extras());
    m.pipe->set_source(m.source.get());
    m.clock.reset();
    m.headless_n = 0;
    m.info = m.source->info();
    if (!m.presenter) {
        m.info.audio = "not played (no window)";
    } else if (!options.audio) {
        m.info.audio = "off";
    } else {
        std::string note;
        m.audio = audio::AudioPlayer::open(path, &note);
        if (m.audio) {
            m.audio->set_volume(m.volume);
            m.audio->set_muted(m.muted);
            m.audio->set_delay(m.audio_delay);
            m.audio->set_paused(m.paused);
            m.info.audio = m.audio->stats().description;
            m.info.has_audio = true;
        } else {
            m.info.audio = note;
            log::info("No sound: {}", note);
        }
    }
    if (m.presenter) {
        m.ahead = std::make_unique<pipeline::RenderAhead>(*m.cuda, *m.pipe);
        m.ahead->configure(m.presenter->window_size(), m.settings);
        m.ahead->start(0, m.info.fps / m.refresh_hz);
        m.waiting_first = true;
    }
    reset_stats();
    return m.info;
}

SourceInfo Engine::open_pipewire(const std::string& target)
{
    return impl_->open_live([target](input::PipeWireCapture::Target& t, std::string&, const std::atomic<bool>&,
                                     const std::function<void(std::string)>&) {
        // A number is a node id; anything else a node name or serial.
        if (!target.empty() && target.find_first_not_of("0123456789") == std::string::npos) t.node = uint32_t(std::stoul(target));
        else t.target = target;
        t.name = "PipeWire " + target;
        return true;
    }, *this);
}

SourceInfo Engine::open_screen_capture(const ScreenCaptureOptions& options)
{
    return impl_->open_live([options](input::PipeWireCapture::Target& t, std::string& error, const std::atomic<bool>& stop,
                                      const std::function<void(std::string)>& status) {
        auto portal = std::make_shared<input::ScreenCastPortal>();
        input::ScreenCastPortal::Options o{options.monitors, options.windows, options.cursor, options.remember};
        std::string token = options.remember ? input::ScreenCastPortal::load_token() : std::string();
        status("asking the desktop for permission");
        if (!portal->start(o, token, t.fd, t.node, error, stop, status)) return false;
        if (options.remember && !token.empty()) input::ScreenCastPortal::save_token(token);
        t.name = "Screen capture";
        t.keep_alive = portal;
        return true;
    }, *this);
}

void Engine::close_source()
{
    Impl& m = *impl_;
    m.capture = nullptr;
    m.ahead.reset();
    m.audio.reset();
    m.pipe.reset();
    m.source.reset();
}

bool Engine::has_source() const noexcept { return impl_->source != nullptr; }

void Engine::set_settings(const Settings& s)
{
    impl_->settings = s;
    if (impl_->source && !impl_->capture)
        static_cast<input::FfmpegFileSource*>(impl_->source.get())->set_scene_cut_detection(s.scene_cut_detection);
}

const Settings& Engine::settings() const noexcept { return impl_->settings; }

Stats Engine::stats() const
{
    const Impl& m = *impl_;
    Stats s;
    if (m.ahead) m.ahead->fill_stats(s);
    else if (m.pipe) m.pipe->fill_stats(s);
    if (m.source) s.source_fps = m.source->info().fps;
    if (m.capture) {
        s.live = true;
        s.capture_status = m.capture->status();
        s.capture_failed = m.capture->failed();
        s.capture_fps = m.pipe && m.pipe->live_interval() > 0 ? 1.0 / m.pipe->live_interval() : 0;
        s.source_fps = s.capture_fps;
        s.live_latency_ms = m.live_latency_ms;
        s.capture_dropped = m.capture->dropped();
    }
    s.display_hz = m.presenter ? m.refresh_hz : 0;
    s.output_frames = m.output_frames;
    s.output_fps = m.presenter ? m.presenter->measured_fps() : m.measured_fps;
    s.missed_refreshes = m.presenter ? m.presenter->missed_refreshes() : 0;
    if (m.presenter) {
        s.output_width = m.presenter->window_size().w;
        s.output_height = m.presenter->window_size().h;
    } else {
        s.output_width = s.work_width;
        s.output_height = s.work_height;
    }
    s.position_s = m.position_s;
    s.avg.output_ms = m.output_ms.value();
    if (m.presenter) {
        s.display_latency_ms = m.presenter->display_latency_s() * 1000;
        auto r = m.presenter->video_rect();
        s.video_x = r.x, s.video_y = r.y, s.video_w = r.w, s.video_h = r.h;
    }
    if (m.audio) {
        auto a = m.audio->stats();
        s.audio_active = true;
        s.av_sync_ms = a.sync_error_ms;
        s.av_sync_max_ms = a.max_sync_error_ms;
        s.audio_speed = a.speed;
        s.audio_latency_ms = a.latency_ms;
        s.audio_resyncs = a.resyncs;
        s.audio_underruns = a.underruns;
    }
    return s;
}

void Engine::reset_stats()
{
    Impl& m = *impl_;
    if (m.ahead) m.ahead->reset_stats();  // the worker resets the pipeline's counters
    else if (m.pipe) m.pipe->reset_stats();
    if (m.presenter) m.presenter->reset_stats();
    m.output_ms.reset();
    m.output_frames = 0;
    if (m.audio) m.audio->reset_stats();
}

void Engine::set_paused(bool p)
{
    impl_->paused = p;
    if (impl_->audio) impl_->audio->set_paused(p);
}

void Engine::set_volume(float g)
{
    impl_->volume = std::max(0.0f, g);
    if (impl_->audio) impl_->audio->set_volume(impl_->volume);
}

void Engine::set_muted(bool mu)
{
    impl_->muted = mu;
    if (impl_->audio) impl_->audio->set_muted(mu);
}

void Engine::set_audio_delay(double s)
{
    impl_->audio_delay = s;
    if (impl_->audio) impl_->audio->set_delay(s);
}

void Engine::set_compare_split(float split) { impl_->compare_split = std::clamp(split, 0.0f, 1.0f); }
float Engine::compare_split() const noexcept { return impl_->compare_split; }

float Engine::volume() const noexcept { return impl_->volume; }
bool Engine::muted() const noexcept { return impl_->muted; }
double Engine::audio_delay() const noexcept { return impl_->audio_delay; }
bool Engine::paused() const noexcept { return impl_->paused; }

void Engine::seek(double seconds)
{
    Impl& m = *impl_;
    if (!m.source || m.capture) return;  // live input can't seek
    const auto& info = m.source->info();
    double last = info.duration_s > 0 ? std::max(0.0, info.duration_s - 1.0 / info.fps) : seconds;
    seconds = std::clamp(seconds, 0.0, last);
    m.cuda->make_current();
    m.seek_started = std::chrono::steady_clock::now();
    if (m.ahead) m.ahead->stop();  // the worker must not be inside the pipeline while it restarts
    m.source->seek(seconds);
    if (m.audio) m.audio->seek(seconds);
    m.pipe->restart();
    m.clock.seek(seconds);
    if (m.ahead) {
        m.ahead->start(seconds * info.fps, info.fps / m.refresh_hz);
        m.waiting_first = true;
    }
    m.position_s = seconds;
    m.headless_n = int64_t(std::llround(seconds * info.fps * std::max(1, m.effective_multiplier(false))));
}

double Engine::position() const noexcept { return impl_->position_s; }

void Engine::attach_window(const WindowTarget& target)
{
    Impl& m = *impl_;
    if (m.vk->has_device() && !m.vk->swapchain)
        throw Error("attach_window() must be called before open_file()");
    VkSurfaceKHR surface = target.create_surface ? target.create_surface(m.vk->instance) : VK_NULL_HANDLE;
    if (!surface) throw Error("The app could not create a Vulkan surface for its window");
    m.ensure_device(surface);
    m.presenter = std::make_unique<output::SwapchainPresenter>(*m.vk, *m.cuda, surface, Size{target.width, target.height});
    set_refresh_rate(target.refresh_hz);
}

void Engine::resize_window(std::uint32_t width, std::uint32_t height)
{
    if (impl_->presenter) impl_->presenter->resize({width, height});
}

void Engine::set_refresh_rate(double hz)
{
    impl_->refresh_hz = hz > 1 ? hz : 60;
    if (impl_->presenter) impl_->presenter->set_refresh_period(1.0 / impl_->refresh_hz);
}

bool Engine::render_frame()
{
    Impl& m = *impl_;
    if (!m.presenter) throw Error("render_frame() needs attach_window() first");
    m.cuda->make_current();
    if (!m.source) {
        m.presenter->present(0, {});
        return true;
    }
    if (m.capture) return render_live();
    const double fps = m.source->info().fps;
    const double period = 1.0 / m.refresh_hz;
    const double step = fps * period;  // source frames per refresh
    m.ahead->configure(m.presenter->window_size(), m.settings);

    // After open / seek: keep showing the old picture until the worker has the first new frame, then start the
    // clock from that frame (so neither the picture nor the sound jumps ahead by the decoding time).
    if (m.waiting_first) {
        auto f = m.ahead->wait_first(m.ahead->shown() ? 4 : 30);
        if (!f) {
            auto old = m.ahead->shown();
            m.output_ms.add(m.presenter->present(old ? old->ptr : 0, old ? old->size : pipeline::Size{}));
            if (m.ahead->finished()) return end_of_source();
            return true;
        }
        m.waiting_first = false;
        m.clock.seek(f->pos / fps);
        if (m.seek_started) {
            log::info("seek to {:.3f} s: first frame ready after {:.0f} ms", f->pos / fps, ms_since(*m.seek_started));
            m.seek_started.reset();
        }
    }

    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double pos = m.clock.tick(now, period, m.paused) * fps;
    auto f = m.ahead->take(pos, step, period);
    if (!f) {
        m.presenter->present(0, {});
        return true;
    }
    if (m.ahead->finished() && pos >= f->pos + 1.0) return end_of_source();

    m.position_s = f->pos / fps;
    if (m.audio) {
        // What is on screen about display_latency after present() is the display clock's time; the sound follows
        // the clock (smooth), not the frame grid (with a frame-gen stride the frame is up to stride-1 refreshes
        // older than the clock).
        auto when = std::chrono::steady_clock::now() + std::chrono::duration<double>(m.presenter->display_latency_s());
        m.audio->sync(pos / fps, std::chrono::duration_cast<std::chrono::nanoseconds>(when.time_since_epoch()).count());
    }
    m.output_ms.add(m.settings.compare ? m.presenter->present(f->ptr, f->size, f->orig, f->orig_size, m.compare_split)
                                       : m.presenter->present(f->ptr, f->size));
    m.count_output();
    return true;
}

bool Engine::render_live()
{
    Impl& m = *impl_;
    m.pipe->configure(m.presenter->window_size(), m.settings);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool smooth = m.settings.live_mode == LiveMode::Smooth;
    // Smooth: show the moment one frame interval (+ one refresh of margin) ago, so there is a frame after it.
    const double target = now - (m.pipe->live_interval() + 1.0 / m.refresh_hz);
    if (m.last_live && m.pipe->generation() != m.last_live_gen) m.last_live.reset();  // buffers reallocated
    std::optional<pipeline::Output> out;
    if (!m.paused || !m.last_live) out = m.pipe->produce_live(smooth, target);
    if (out) {
        m.last_live = out;
        m.last_live_gen = m.pipe->generation();
    } else {
        out = m.last_live;
    }
    if (!out) {
        m.presenter->present(0, {});  // waiting for the first frame (or the permission dialog)
        return true;
    }
    if (m.live_start_s <= 0) m.live_start_s = out->arrival_s;
    m.position_s = out->arrival_s - m.live_start_s;  // seconds since the first frame (for the app's clock / tests)
    const double lat = (now - out->arrival_s) * 1000 + m.presenter->display_latency_s() * 1000;
    if (!m.paused) m.live_latency_ms = m.live_latency_ms > 0 ? m.live_latency_ms + (lat - m.live_latency_ms) * 0.05 : lat;
    m.output_ms.add(m.settings.compare ? m.presenter->present(out->ptr, out->size, out->orig, out->orig_size, m.compare_split)
                                       : m.presenter->present(out->ptr, out->size));
    m.count_output();
    return true;
}

bool Engine::end_of_source()
{
    Impl& m = *impl_;
    if (m.open_options.loop) {
        seek(0);
        return render_frame();
    }
    // Keep showing the last frame (and the app's overlay) until the app seeks or closes.
    auto last = m.ahead->shown();
    m.presenter->present(last ? last->ptr : 0, last ? last->size : pipeline::Size{});
    return false;
}

void Engine::set_overlay(OverlayCallback overlay)
{
    if (impl_->presenter) impl_->presenter->set_overlay(std::move(overlay));
}

VulkanDevice Engine::vulkan_device() const
{
    const Impl& m = *impl_;
    VulkanDevice d;
    d.api_version = VK_API_VERSION_1_3;
    d.instance = m.vk->instance;
    d.physical_device = m.vk->phys;
    d.device = m.vk->device;
    d.queue_family = m.vk->queue_family;
    d.queue = m.vk->queue;
    if (m.presenter) {
        d.window_format = m.presenter->format();
        d.window_image_count = m.presenter->image_count();
    }
    return d;
}

void Engine::capture_next_window_frame(const std::string& ppm_path)
{
    if (impl_->presenter) impl_->presenter->capture_next(ppm_path);
}

void Engine::set_headless_output(std::uint32_t width, std::uint32_t height)
{
    Impl& m = *impl_;
    if (!m.source) throw Error("set_headless_output() needs an open source");
    m.headless_box = width && height ? Size{width, height} : Size{m.source->info().width, m.source->info().height};
    m.cuda->make_current();
    m.pipe->configure(m.headless_box, m.settings);
}

std::uint32_t Engine::headless_width() const noexcept { return impl_->pipe ? impl_->pipe->work_size().w : 0; }
std::uint32_t Engine::headless_height() const noexcept { return impl_->pipe ? impl_->pipe->work_size().h : 0; }

double Engine::headless_fps() const noexcept
{
    const Impl& m = *impl_;
    return m.source ? m.source->info().fps * m.effective_multiplier(false) : 0;
}

bool Engine::render_headless(std::span<std::uint8_t> rgba)
{
    Impl& m = *impl_;
    if (!m.source) throw Error("render_headless() needs an open source");
    m.cuda->make_current();
    if (!m.headless_box.w) set_headless_output(0, 0);
    m.pipe->configure(m.headless_box, m.settings);
    int mult = m.effective_multiplier(false);
    double position = double(m.headless_n) / mult;
    auto out = m.pipe->produce(position, mult);
    if (!out) return false;
    ++m.headless_n;
    m.position_s = position / m.source->info().fps;
    if (!rgba.empty()) {
        size_t bytes = size_t(out->size.w) * out->size.h * 4;
        if (rgba.size() < bytes) throw Error("render_headless(): output buffer too small");
        auto t0 = SteadyClock::now();
        NITID_CU(cuMemcpyDtoHAsync(rgba.data(), out->ptr, bytes, m.cuda->stream));
        m.cuda->sync();
        m.output_ms.add(ms_since(t0));
    }
    m.count_output();
    return true;
}

} // namespace nitid
