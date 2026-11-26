#include <nitid/engine.hpp>
#include <nitid/error.hpp>

#include "core/log.hpp"
#include "core/running_average.hpp"
#include "gpu/cuda_context.hpp"
#include "gpu/kernels.hpp"
#include "gpu/vulkan_context.hpp"
#include "input/ffmpeg_file_source.hpp"
#include "output/swapchain_presenter.hpp"
#include "pipeline/frame_clock.hpp"
#include "pipeline/pipeline.hpp"
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

    std::unique_ptr<output::SwapchainPresenter> presenter;
    std::unique_ptr<input::FfmpegFileSource> source;
    std::unique_ptr<pipeline::Pipeline> pipe;
    OpenOptions open_options;

    Settings settings;
    bool paused = false;
    double refresh_hz = 60;
    pipeline::DisplayClock clock;
    std::optional<pipeline::Output> last_output;
    double last_position = -1;
    std::optional<std::chrono::steady_clock::time_point> seek_started;  // logs how long a seek took

    Size headless_box;
    int64_t headless_n = 0;

    RunningAverage output_ms;
    uint64_t output_frames = 0;
    double position_s = 0;
    double fps_window_start = -1;
    uint64_t fps_window_frames = 0;
    double measured_fps = 0;

    void ensure_device(VkSurfaceKHR surface)
    {
        if (vk->has_device()) return;
        vk->create_device(surface);
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
    impl_->pipe.reset();
    impl_->source.reset();
    impl_->presenter.reset();
    impl_->kernels.reset();
    impl_->cuda.reset();
    impl_->vk.reset();
}

std::unique_ptr<Engine> Engine::create(const EngineOptions& options)
{
    std::unique_ptr<Engine> e(new Engine());
    Impl& m = *e->impl_;
    m.options = options;
    m.vk = std::make_unique<gpu::VulkanContext>(options.instance_extensions, options.validation);
    m.cuda = std::make_unique<gpu::CudaContext>(m.vk->device_uuid);
    m.kernels = std::make_unique<gpu::Kernels>();
    m.caps.gpu_name = m.vk->props.deviceName;
    m.caps.driver_version = m.vk->driver_version();
    m.probe_effects();
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
    m.source = std::make_unique<input::FfmpegFileSource>(path, options, int(m.cuda->device), &gd);
    m.source->set_scene_cut_detection(m.settings.scene_cut_detection);
    m.pipe = std::make_unique<pipeline::Pipeline>(*m.cuda, m.vfx, m.caps.super_res, m.caps.frame_gen);
    m.pipe->set_source(m.source.get());
    m.clock.reset();
    m.last_output.reset();
    m.headless_n = 0;
    reset_stats();
    return m.source->info();
}

void Engine::close_source()
{
    Impl& m = *impl_;
    m.pipe.reset();
    m.source.reset();
    m.last_output.reset();
}

bool Engine::has_source() const noexcept { return impl_->source != nullptr; }

void Engine::set_settings(const Settings& s)
{
    impl_->settings = s;
    if (impl_->source) impl_->source->set_scene_cut_detection(s.scene_cut_detection);
    impl_->last_output.reset();
}

const Settings& Engine::settings() const noexcept { return impl_->settings; }

Stats Engine::stats() const
{
    const Impl& m = *impl_;
    Stats s;
    if (m.pipe) m.pipe->fill_stats(s);
    if (m.source) s.source_fps = m.source->info().fps;
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
    return s;
}

void Engine::reset_stats()
{
    Impl& m = *impl_;
    if (m.pipe) m.pipe->reset_stats();
    if (m.presenter) m.presenter->reset_stats();
    m.output_ms.reset();
    m.output_frames = 0;
}

void Engine::set_paused(bool p) { impl_->paused = p; }
bool Engine::paused() const noexcept { return impl_->paused; }

void Engine::seek(double seconds)
{
    Impl& m = *impl_;
    if (!m.source) return;
    const auto& info = m.source->info();
    double last = info.duration_s > 0 ? std::max(0.0, info.duration_s - 1.0 / info.fps) : seconds;
    seconds = std::clamp(seconds, 0.0, last);
    m.cuda->make_current();
    m.seek_started = std::chrono::steady_clock::now();
    m.source->seek(seconds);
    m.pipe->restart();
    m.clock.seek(seconds);
    m.last_output.reset();
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
    m.pipe->configure(m.presenter->window_size(), m.settings);
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    double t = m.clock.tick(now, 1.0 / m.refresh_hz, m.paused);
    double fps = m.source->info().fps;
    double position = t * fps;
    int mult = m.effective_multiplier(true);

    std::optional<pipeline::Output> out;
    if (m.paused && m.last_output && position == m.last_position) {
        out = m.last_output;  // nothing moves while paused: don't regenerate
    } else {
        out = m.pipe->produce(position, mult);
        if (!out && m.open_options.loop) {
            m.source->restart();
            m.pipe->restart();
            m.clock.reset();
            position = 0;
            out = m.pipe->produce(0, mult);
        }
    }
    if (!out) return false;
    m.last_output = out;
    m.last_position = position;
    m.position_s = position / fps;
    if (m.seek_started) {
        log::info("seek to {:.3f} s: first frame ready after {:.0f} ms", m.position_s, ms_since(*m.seek_started));
        m.seek_started.reset();
    }
    m.output_ms.add(m.presenter->present(out->ptr, out->size));
    m.count_output();
    return true;
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
