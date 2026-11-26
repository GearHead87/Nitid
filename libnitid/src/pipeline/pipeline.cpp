#include "pipeline/pipeline.hpp"

#include "core/log.hpp"
#include "pipeline/frame_clock.hpp"

#include <nitid/error.hpp>

#include <utility>

namespace nitid::pipeline {

Pipeline::Pipeline(gpu::CudaContext& cuda, std::shared_ptr<const vfx::Api> vfx, bool can_sr, bool can_fg)
    : cuda_(cuda), vfx_(std::move(vfx)), can_sr_(can_sr), can_fg_(can_fg)
{
    if (vfx_) {
        sr_ = std::make_unique<vfx::SuperRes>(vfx_, cuda_.stream);
        fg_ = std::make_unique<vfx::FrameGen>(vfx_, cuda_.stream);
    }
    spatial_ = std::make_unique<gpu::SpatialScaler>();
}

Pipeline::~Pipeline() { cuda_.sync(); }

template <class F> void Pipeline::timed(RunningAverage& avg, F&& fn)
{
    // Per-stage wall-clock timing: sync after the stage. The VFX SDK works on its own streams, so CUDA
    // events around it are not reliable (spike S5); the stages run one after another anyway.
    auto t0 = SteadyClock::now();
    fn();
    cuda_.sync();
    avg.add(ms_since(t0));
}

void Pipeline::set_source(input::FrameSource* source)
{
    source_ = source;
    src_ = source ? Size{source->info().width, source->info().height} : Size{};
    work_ = {};
    restart();
}

void Pipeline::restart()
{
    for (auto& s : slots_) s.index = -1;
    cur_ = 0, next_ = 1;
    ended_ = false;
    pending_cut_ = false;
}

void Pipeline::allocate_ring()
{
    ++generation_;
    size_t bytes = size_t(work_.w) * work_.h * 4;
    for (auto& s : slots_) {
        s.buf = gpu::CudaBuffer(bytes);
        s.index = -1;
    }
    cur_ = 0, next_ = 1;
}

void Pipeline::configure(Size box, const Settings& settings)
{
    if (!source_ || !box.w || !box.h) return;
    bool same = box == box_ && settings == settings_ && work_.w;
    if (same) return;
    box_ = box;
    settings_ = settings;

    // Upscaler: active only if wanted, available, and the frame actually gets bigger.
    const bool spatial = settings.upscaler == Upscaler::Fsr1 || settings.upscaler == Upscaler::Nis;
    const double max_scale = settings.upscaler == Upscaler::Nis ? gpu::SpatialScaler::kNisMaxScale : 4.0;
    Size target = upscale_target(src_, box, max_scale);
    up_ = Up::None;
    sr_note_.clear();
    if (settings.upscaler == Upscaler::Off) sr_note_ = "off";
    else if (target == src_) sr_note_ = "no upscale needed (window not larger than the video)";
    else if (spatial && !spatial_->available()) sr_note_ = "FSR / NIS kernels not in this build";
    else if (spatial) up_ = Up::Spatial;
    else if (!can_sr_) sr_note_ = "VFX runtime not available";
    else if (sr_failed_) sr_note_ = "failed to load (see log)";
    else up_ = Up::Vsr;
    sr_active_ = up_ != Up::None;

    Size work = sr_active_ ? target : src_;
    compare_ = settings.compare;
    for (auto& sl : slots_) {
        if (compare_ && sl.orig.size() != size_t(src_.w) * src_.h * 4) {
            sl.orig = gpu::CudaBuffer(size_t(src_.w) * src_.h * 4);
            sl.has_orig = false;
        } else if (!compare_ && sl.orig) {
            sl.orig = {};
            sl.has_orig = false;
        }
    }
    bool ring_changed = !(work == work_);
    if (ring_changed) {
        cuda_.sync();
        work_ = work;
        allocate_ring();
    }
    if (sr_active_ && (!upload_ || upload_.size() != size_t(src_.w) * src_.h * 4))
        upload_ = gpu::CudaBuffer(size_t(src_.w) * src_.h * 4);
    if (up_ == Up::Vsr) {
        try {
            sr_->configure(upload_.ptr(), src_.w, src_.h, work_.w, work_.h, unsigned(settings.super_res_quality));
        } catch (const Error& e) {
            log::error("{}", e.what());
            sr_failed_ = true;
            box_ = {};  // reconfigure without VSR
            configure(box, settings);
            return;
        }
    } else if (sr_ && sr_->configured()) {
        sr_->reset();  // free the model's memory
    }
    if (up_ == Up::Spatial)
        spatial_->configure(settings.upscaler == Upscaler::Nis ? gpu::SpatialKind::Nis : gpu::SpatialKind::Fsr1, src_.w,
                            src_.h, work_.w, work_.h, settings.sharpness);

    // Frame generation.
    fg_active_ = false;
    fg_note_.clear();
    if (settings.frame_gen != FrameGen::VfxFrameGen) fg_note_ = "off";
    else if (!can_fg_) fg_note_ = "not available on this GPU / runtime";
    else if (fg_failed_) fg_note_ = "failed to load (see log)";
    else {
        try {
            fg_->configure(work_.w, work_.h, unsigned(settings.frame_gen_quality));
            fg_active_ = true;
        } catch (const Error& e) {
            log::error("{}", e.what());
            fg_failed_ = true;
            fg_note_ = "failed to load (see log)";
        }
    }
    if (!fg_active_ && fg_ && fg_->configured()) fg_->reset();
    log::info("Pipeline: source {}x{} -> work {}x{} (upscaler {}, frame gen {})", src_.w, src_.h, work_.w, work_.h,
              sr_active_ ? upscaling_name(settings) : sr_note_, fg_active_ ? "on" : fg_note_);
}

void Pipeline::upload(input::DecodedFrame& f, Slot& dst)
{
    const size_t bytes = size_t(f.width) * f.height * 4;
    // GPU frames: a device copy (~0.02 ms at 1080p). CPU frames: an upload.
    auto copy_in = [&](CUdeviceptr to) {
        if (f.gpu_rgba) NITID_CU(cuMemcpyDtoDAsync(to, f.gpu_rgba.ptr(), bytes, cuda_.stream));
        else NITID_CU(cuMemcpyHtoDAsync(to, f.rgba.data(), bytes, cuda_.stream));
    };
    if (compare_ && dst.orig) {
        copy_in(dst.orig.ptr());
        dst.has_orig = true;
    }
    if (up_ == Up::Vsr) {
        timed(upload_ms_, [&] { copy_in(upload_.ptr()); });
        timed(upscale_ms_, [&] {
            sr_->run();
            NITID_CU(cuMemcpyDtoDAsync(dst.buf.ptr(), sr_->output(), size_t(work_.w) * work_.h * 4, cuda_.stream));
        });
    } else if (up_ == Up::Spatial) {
        // GPU frames are read in place; CPU frames are uploaded first.
        CUdeviceptr in = f.gpu_rgba.ptr();
        if (!f.gpu_rgba) {
            timed(upload_ms_, [&] { copy_in(upload_.ptr()); });
            in = upload_.ptr();
        }
        timed(upscale_ms_, [&] { spatial_->run(in, dst.buf.ptr(), cuda_.stream); });
    } else {
        timed(upload_ms_, [&] { copy_in(dst.buf.ptr()); });
    }
}

bool Pipeline::fetch_next(int64_t min_index)
{
    for (;;) {
        auto f = source_->next();
        if (!f) return false;
        decode_ms_.add(f->decode_ms);
        if (f->scene_cut) ++cuts_;
        bool cut = f->scene_cut || pending_cut_;
        if (f->index < min_index || f->width != src_.w || f->height != src_.h) {
            // Already in the past (playback fell behind): never shown, so don't spend GPU time on it.
            ++skipped_;
            pending_cut_ = cut;
            source_->recycle(std::move(*f));
            continue;
        }
        pending_cut_ = false;
        Slot& dst = slots_[next_];
        upload(*f, dst);
        dst.index = f->index;
        dst.scene_cut = cut;
        source_->recycle(std::move(*f));
        return true;
    }
}

std::optional<Output> Pipeline::produce(double position, int multiplier)
{
    if (!source_ || !work_.w) return std::nullopt;
    const FramePick p = pick(position, multiplier);
    const int64_t i = p.frame;

    // Advance until current.index <= i < next.index (or the source ends).
    for (;;) {
        Slot& nx = slots_[next_];
        if (nx.valid() && nx.index <= i) {
            std::swap(cur_, next_);
            slots_[next_].index = -1;
            ++source_frames_;
            continue;
        }
        if (nx.valid() || ended_) break;
        if (!fetch_next(i)) ended_ = true;
    }

    Slot& c = slots_[cur_];
    Slot& n = slots_[next_];
    auto with_orig = [&](Output o, const Slot& from) {
        if (compare_ && from.has_orig) o.orig = from.orig.ptr(), o.orig_size = src_;
        return o;
    };
    if (!c.valid()) {
        // Only right after a reconfigure (ring reallocated) or at the very start: show what we have.
        if (n.valid()) return with_orig(Output{n.buf.ptr(), work_, false}, n);
        return std::nullopt;
    }
    if (ended_ && !n.valid() && i > c.index) return std::nullopt;  // past the last frame

    const bool gen = fg_active_ && p.phase > 0 && n.valid() && n.index == c.index + 1 && !n.scene_cut;
    if (gen) {
        timed(frame_gen_ms_, [&] { fg_->run(c.buf.ptr(), n.buf.ptr(), p.phase, false); });
        ++generated_;
        return with_orig(Output{fg_->output(), work_, true}, p.phase < 0.5f ? c : n);  // nearest real frame
    }
    return with_orig(Output{c.buf.ptr(), work_, false}, c);
}

void Pipeline::fill_stats(Stats& s) const
{
    s.source_width = src_.w, s.source_height = src_.h;
    s.work_width = work_.w, s.work_height = work_.h;
    s.upscaler_active = sr_active_;
    s.frame_gen_active = fg_active_;
    s.upscaler_note = sr_note_;
    s.frame_gen_note = fg_note_;
    s.source_frames = source_frames_;
    s.skipped_frames = skipped_;
    s.generated_frames = generated_;
    s.scene_cuts = cuts_;
    s.avg.decode_ms = decode_ms_.value();
    s.avg.upload_ms = upload_ms_.value();
    s.avg.upscale_ms = upscale_ms_.value();
    s.avg.frame_gen_ms = frame_gen_ms_.value();
}

void Pipeline::reset_stats()
{
    decode_ms_.reset(), upload_ms_.reset(), upscale_ms_.reset(), frame_gen_ms_.reset();
    source_frames_ = skipped_ = generated_ = cuts_ = 0;
}

} // namespace nitid::pipeline
