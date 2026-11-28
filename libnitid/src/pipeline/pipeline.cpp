#include "pipeline/pipeline.hpp"

#include "core/log.hpp"
#include "pipeline/frame_clock.hpp"

#include <nitid/error.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace nitid::pipeline {

Pipeline::Pipeline(gpu::CudaContext& cuda, std::shared_ptr<const vfx::Api> vfx, bool can_sr, bool can_fg, GpuExtras extras)
    : cuda_(cuda), vfx_(std::move(vfx)), can_sr_(can_sr), can_fg_(can_fg), gx_(std::move(extras))
{
    if (vfx_) {
        sr_ = std::make_unique<vfx::SuperRes>(vfx_, cuda_.stream);
        fg_ = std::make_unique<vfx::FrameGen>(vfx_, cuda_.stream);
    }
    spatial_ = std::make_unique<gpu::SpatialScaler>();
}

Pipeline::~Pipeline()
{
    cuda_.sync();
    dlss_sr_.reset();  // before the Vulkan device / DLSS runtime go
    dlss_fg_.reset();
    flow_.reset();
}

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
    dlss_reset_ = true;
    flow_reset_ = true;
    if (dlss_fg_) dlss_fg_->restart();
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
    if (!src_.w || !src_.h) {  // live source before its first frame: remember, configure once the size is known
        box_ = box;
        settings_ = settings;
        return;
    }
    bool same = box == box_ && settings == settings_ && work_.w;
    if (same) return;
    box_ = box;
    settings_ = settings;

    // Upscaler: active only if wanted, available, and the frame actually gets bigger (DLSS: or at the same size, as
    // DLAA).
    const bool spatial = settings.upscaler == Upscaler::Fsr1 || settings.upscaler == Upscaler::Nis;
    const bool dlss = settings.upscaler == Upscaler::Dlss;
    const double max_scale = settings.upscaler == Upscaler::Nis ? gpu::SpatialScaler::kNisMaxScale : dlss ? 3.0 : 4.0;
    Size target = upscale_target(src_, box, max_scale);
    up_ = Up::None;
    sr_note_.clear();
    const bool dlss_ok = gx_.dlss && gx_.vk && gx_.dlss->caps().super_res;
    if (settings.upscaler == Upscaler::Off) sr_note_ = "off";
    else if (dlss && !dlss_ok) sr_note_ = gx_.dlss_note.empty() ? "DLSS not available" : gx_.dlss_note;
    else if (dlss && dlss_failed_) sr_note_ = "DLSS failed to start (see log)";
    else if (dlss) up_ = Up::Dlss;
    else if (target == src_) sr_note_ = "no upscale needed (window not larger than the video)";
    else if (spatial && !spatial_->available()) sr_note_ = "FSR / NIS kernels not in this build";
    else if (spatial) up_ = Up::Spatial;
    else if (!can_sr_) sr_note_ = "VFX runtime not available";
    else if (sr_failed_) sr_note_ = "failed to load (see log)";
    else up_ = Up::Vsr;
    sr_active_ = up_ != Up::None;
    // Clean-up (same size) when nothing upscales: VSR's denoise / deblur models.
    cleanup_active_ = false;
    cleanup_note_.clear();
    unsigned vsr_mode = unsigned(settings.super_res_quality);
    if (settings.cleanup != Cleanup::Off) {
        if (up_ == Up::Dlss && target == src_) cleanup_note_ = "DLSS runs as DLAA at this size";
        else if (sr_active_) cleanup_note_ = "the video is being upscaled (upscaling cleans up too)";
        else if (!can_sr_) cleanup_note_ = "VFX runtime not available";
        else if (sr_failed_) cleanup_note_ = "failed to load (see log)";
        else {
            const unsigned strength = std::max(1u, unsigned(settings.cleanup_strength)) - 1;  // Low..Ultra -> 0..3
            vsr_mode = (settings.cleanup == Cleanup::Denoise ? 8u : 12u) + std::min(strength, 3u);
            up_ = Up::Vsr;
            cleanup_active_ = true;
        }
    }

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
    if (up_ != Up::None && (!upload_ || upload_.size() != size_t(src_.w) * src_.h * 4))
        upload_ = gpu::CudaBuffer(size_t(src_.w) * src_.h * 4);
    if (up_ == Up::Vsr) {
        try {
            sr_->configure(upload_.ptr(), src_.w, src_.h, work_.w, work_.h, vsr_mode);
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
    // DLSS frame generation possible? (decided here: it may need optical flow)
    const bool dlss_fg_ok = gx_.dlss && gx_.vk && gx_.dlss->caps().frame_gen;
    const bool want_dlss_fg = settings.frame_gen == FrameGen::DlssFrameGen && dlss_fg_ok && !dlss_fg_failed_;
    const bool lite = settings.dlss_frame_gen_mode == DlssFrameGenMode::Lite;
    // Motion from optical flow (source size): for DLSS when it upscales (at the same size, DLAA, motion vectors made
    // it worse: tests/v0.5/03), and for DLSS-G lite.
    // (The full DLSS-G model gets zero motion: it finds motion itself, and optical flow vectors helped on one clip and
    // hurt on another, tests/v0.5/04 and spike F1.)
    fg_motion_ = want_dlss_fg && lite;
    const bool want_flow = (up_ == Up::Dlss && !(work_ == src_)) || fg_motion_;
    flow_active_ = false;
    if (want_flow && gx_.vk && gx_.kernels && gpu::OpticalFlow::available(*gx_.vk, *gx_.kernels)) {
        try {
            if (!flow_) flow_ = std::make_unique<gpu::OpticalFlow>(*gx_.vk, cuda_, *gx_.kernels);
            flow_->configure(src_.w, src_.h);
            flow_reset_ = true;
            flow_active_ = true;
        } catch (const Error& e) {
            log::error("optical flow: {} (DLSS gets no motion vectors)", e.what());
            flow_.reset();
        }
    } else if (want_flow) {
        log::warn("no hardware optical flow: DLSS gets no motion vectors");
    }
    if (!flow_active_) fg_motion_ = false;
    if (!want_flow && flow_) flow_.reset();
    if (up_ == Up::Dlss) {
        try {
            if (!dlss_sr_) dlss_sr_ = std::make_unique<dlss::SuperRes>(*gx_.vk, cuda_, *gx_.dlss);
            dlss_sr_->set_motion(flow_active_ && !(work_ == src_) ? flow_.get() : nullptr);
            // Auto: preset M when upscaling (best on video at 1.5x and 2x with motion), K for DLAA (tests/v0.5/03).
            const DlssPreset preset = settings.dlss_preset != DlssPreset::Auto ? settings.dlss_preset
                                      : work_ == src_                         ? DlssPreset::K
                                                                              : DlssPreset::M;
            dlss_sr_->configure(src_.w, src_.h, work_.w, work_.h, int(preset));
            dlss_reset_ = true;
        } catch (const Error& e) {
            log::error("{}", e.what());
            dlss_failed_ = true;
            box_ = {};  // reconfigure without DLSS
            configure(box, settings);
            return;
        }
    } else if (dlss_sr_ && dlss_sr_->configured()) {
        dlss_sr_->reset();
    }
    if (up_ == Up::Spatial)
        spatial_->configure(settings.upscaler == Upscaler::Nis ? gpu::SpatialKind::Nis : gpu::SpatialKind::Fsr1, src_.w,
                            src_.h, work_.w, work_.h, settings.sharpness);

    // Frame generation.
    fg_active_ = false;
    fg_dlss_ = false;
    fg_note_.clear();
    if (settings.frame_gen == FrameGen::DlssFrameGen) {
        if (!dlss_fg_ok)
            fg_note_ = !gx_.dlss ? (gx_.dlss_note.empty() ? "DLSS not available" : gx_.dlss_note)
                                 : "DLSS frame generation needs an RTX 40 / 50 GPU";
        else if (dlss_fg_failed_) fg_note_ = "DLSS frame generation failed to start (see log)";
        else {
            try {
                if (!dlss_fg_) dlss_fg_ = std::make_unique<dlss::FrameGen>(*gx_.vk, cuda_, *gx_.dlss);
                dlss_fg_->configure(work_.w, work_.h, src_.w, src_.h, lite);
                dlss_fg_->restart();
                fg_active_ = fg_dlss_ = true;
            } catch (const Error& e) {
                log::error("{}", e.what());
                dlss_fg_failed_ = true;
                fg_note_ = "DLSS frame generation failed to start (see log)";
            }
        }
    } else if (settings.frame_gen != FrameGen::VfxFrameGen) fg_note_ = "off";
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
    if ((!fg_active_ || fg_dlss_) && fg_ && fg_->configured()) fg_->reset();
    if (!fg_dlss_ && dlss_fg_ && dlss_fg_->configured()) dlss_fg_->reset();
    log::info("Pipeline: source {}x{} -> work {}x{} (upscaler {}, frame gen {})", src_.w, src_.h, work_.w, work_.h,
              sr_active_ ? upscaling_name(settings) : cleanup_active_ ? std::string(to_string(settings.cleanup)) : sr_note_,
              fg_active_ ? "on" : fg_note_);
}

void Pipeline::upload(input::DecodedFrame& f, Slot& dst, bool cut)
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
    // Optical flow (DLSS motion vectors) on the source frame at `src`, current -> previous uploaded frame.
    auto motion = [&](CUdeviceptr src) {
        if (!flow_active_) return;
        if (cut || flow_reset_) flow_->reset();  // no motion across a cut or a seek
        flow_reset_ = false;
        timed(motion_ms_, [&] { flow_->push(src); });
    };
    if (up_ == Up::Vsr) {
        timed(upload_ms_, [&] { copy_in(upload_.ptr()); });
        motion(upload_.ptr());
        timed(upscale_ms_, [&] {
            sr_->run();
            NITID_CU(cuMemcpyDtoDAsync(dst.buf.ptr(), sr_->output(), size_t(work_.w) * work_.h * 4, cuda_.stream));
        });
    } else if (up_ == Up::Dlss) {
        timed(upload_ms_, [&] { copy_in(dlss_sr_->input()); });
        motion(dlss_sr_->input());
        timed(upscale_ms_, [&] {
            dlss_sr_->run(dlss_reset_ || cut);  // a new shot: don't carry detail over from the old one
            NITID_CU(cuMemcpyDtoDAsync(dst.buf.ptr(), dlss_sr_->output(), size_t(work_.w) * work_.h * 4, cuda_.stream));
        });
        dlss_reset_ = false;
    } else if (up_ == Up::Spatial) {
        // GPU frames are read in place; CPU frames are uploaded first.
        CUdeviceptr in = f.gpu_rgba.ptr();
        if (!f.gpu_rgba) {
            timed(upload_ms_, [&] { copy_in(upload_.ptr()); });
            in = upload_.ptr();
        }
        motion(in);
        timed(upscale_ms_, [&] { spatial_->run(in, dst.buf.ptr(), cuda_.stream); });
    } else {
        timed(upload_ms_, [&] { copy_in(dst.buf.ptr()); });
        motion(dst.buf.ptr());
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
        upload(*f, dst, cut);
        dst.index = f->index;
        dst.scene_cut = cut;
        source_->recycle(std::move(*f));
        return true;
    }
}

CUdeviceptr Pipeline::dlss_generated(const Slot& c, const Slot& n, int m, int k)
{
    timed(frame_gen_ms_, [&] {
        dlss_fg_->generate(c.buf.ptr(), c.index, n.buf.ptr(), n.index, m - 1, fg_motion_ ? flow_.get() : nullptr);
    });
    return dlss_fg_->output(k);
}

std::optional<Output> Pipeline::produce(double position, int multiplier)
{
    if (!source_ || !work_.w) return std::nullopt;
    if (fg_dlss_) {  // DLSS-G: whole steps between real frames, 2x .. its maximum
        const int max_m = gx_.dlss->caps().max_generated + 1;
        multiplier = std::clamp(multiplier > 0 ? multiplier : 2, 2, max_m);
    }
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
    if (gen && fg_dlss_) {
        const int k = std::clamp(int(std::lround(p.phase * multiplier)), 1, multiplier - 1);
        const CUdeviceptr out = dlss_generated(c, n, multiplier, k);
        ++generated_;
        return with_orig(Output{out, work_, true}, p.phase < 0.5f ? c : n);
    }
    if (gen) {
        timed(frame_gen_ms_, [&] { fg_->run(c.buf.ptr(), n.buf.ptr(), p.phase, false); });
        ++generated_;
        return with_orig(Output{fg_->output(), work_, true}, p.phase < 0.5f ? c : n);  // nearest real frame
    }
    return with_orig(Output{c.buf.ptr(), work_, false}, c);
}

std::optional<Output> Pipeline::produce_live(bool interpolate, double target)
{
    if (!source_) return std::nullopt;
    // Newest arrived frame; anything older that wasn't shown yet is dropped.
    std::optional<input::DecodedFrame> f;
    while (auto g = source_->try_next()) {
        if (f) {
            ++skipped_;
            source_->recycle(std::move(*f));
        }
        f = std::move(g);
    }
    if (f) {
        decode_ms_.add(f->decode_ms);
        if (last_arrival_ > 0) {
            const double dt = f->arrival_s - last_arrival_;
            live_interval_ = live_interval_ > 0 ? live_interval_ + (dt - live_interval_) * 0.1 : dt;
        }
        last_arrival_ = f->arrival_s;
        if (f->width != src_.w || f->height != src_.h) {
            // First frame, or the captured window changed size: start over at the new size.
            src_ = {f->width, f->height};
            work_ = {};
            for (auto& sl : slots_) sl.index = -1;
            const Size box = box_;
            const Settings st = settings_;
            box_ = {};
            configure(box, st);
        }
        if (work_.w) {
            if (slots_[next_].valid()) std::swap(cur_, next_);  // next becomes the previous frame
            Slot& dst = slots_[next_];
            upload(*f, dst, false);
            dst.index = f->index;
            dst.arrival = f->arrival_s;
            dst.scene_cut = false;
            ++source_frames_;
        }
        source_->recycle(std::move(*f));
    }
    const Slot& c = slots_[cur_];
    const Slot& n = slots_[next_];
    if (!n.valid()) return std::nullopt;  // nothing has arrived yet
    auto with = [&](Output o, const Slot& sl) {
        if (compare_ && sl.has_orig) o.orig = sl.orig.ptr(), o.orig_size = src_;
        o.arrival_s = sl.arrival;
        return o;
    };
    const Output newest = with(Output{n.buf.ptr(), work_, false}, n);
    if (!interpolate || !c.valid()) return newest;
    const double span = n.arrival - c.arrival;
    // A long gap (a still desktop sends no frames) is not motion to interpolate.
    if (target >= n.arrival || span <= 0 || span > 0.25) return newest;
    if (target <= c.arrival) return with(Output{c.buf.ptr(), work_, false}, c);
    const float phase = float((target - c.arrival) / span);
    if (fg_dlss_) {
        // DLSS-G makes whole steps: use its largest multiplier and the nearest step.
        const int m = gx_.dlss->caps().max_generated + 1;
        const int k = int(std::lround(phase * m));
        if (k <= 0) return with(Output{c.buf.ptr(), work_, false}, c);
        if (k >= m) return newest;
        const CUdeviceptr out = dlss_generated(c, n, m, k);
        ++generated_;
        return with(Output{out, work_, true}, phase < 0.5f ? c : n);
    }
    if (fg_active_) {
        timed(frame_gen_ms_, [&] { fg_->run(c.buf.ptr(), n.buf.ptr(), phase, false); });
        ++generated_;
        return with(Output{fg_->output(), work_, true}, phase < 0.5f ? c : n);
    }
    return phase < 0.5f ? with(Output{c.buf.ptr(), work_, false}, c) : newest;
}

void Pipeline::fill_stats(Stats& s) const
{
    s.source_width = src_.w, s.source_height = src_.h;
    s.work_width = work_.w, s.work_height = work_.h;
    s.upscaler_active = sr_active_;
    s.frame_gen_active = fg_active_;
    s.upscaler_note = sr_note_;
    s.upscaler_mode = up_ == Up::Dlss && dlss_sr_ ? dlss_sr_->mode() : std::string();
    s.cleanup_active = cleanup_active_;
    s.cleanup_note = cleanup_note_;
    s.frame_gen_note = fg_note_;
    s.frame_gen_max_multiplier = fg_dlss_ ? gx_.dlss->caps().max_generated + 1 : 0;
    s.source_frames = source_frames_;
    s.skipped_frames = skipped_;
    s.generated_frames = generated_;
    s.scene_cuts = cuts_;
    s.avg.decode_ms = decode_ms_.value();
    s.avg.upload_ms = upload_ms_.value();
    s.avg.upscale_ms = upscale_ms_.value();
    s.avg.frame_gen_ms = frame_gen_ms_.value();
    s.avg.motion_ms = flow_active_ ? motion_ms_.value() : 0;
}

void Pipeline::reset_stats()
{
    decode_ms_.reset(), upload_ms_.reset(), upscale_ms_.reset(), frame_gen_ms_.reset(), motion_ms_.reset();
    source_frames_ = skipped_ = generated_ = cuts_ = 0;
}

} // namespace nitid::pipeline
