#include "pipeline/render_ahead.hpp"

#include "core/log.hpp"
#include "core/running_average.hpp"
#include "pipeline/frame_clock.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace nitid::pipeline {

namespace {
constexpr double kEps = 1e-6;
constexpr int kStrideHoldFrames = 90;  // frames at a stride before it may drop again (no flapping)
constexpr int kWarmupFrames = 12;      // frames after a start / reconfigure ignored for pacing
} // namespace

RenderAhead::RenderAhead(gpu::CudaContext& cuda, Pipeline& pipe) : cuda_(cuda), pipe_(pipe)
{
    pool_.resize(kPool);
    pool_orig_.resize(kPool);
    for (int i = kPool - 1; i >= 0; --i) free_.push_back(i);
}

RenderAhead::~RenderAhead() { stop(); }

void RenderAhead::configure(Size box, const Settings& settings)
{
    std::lock_guard l(m_);
    if (box == box_ && settings == settings_) return;
    box_ = box;
    settings_ = settings;
    reconfigure_ = true;
    cv_.notify_all();
}

void RenderAhead::start(double pos, double refresh_step)
{
    stop();
    std::lock_guard l(m_);
    stop_ = false;
    display_pos_ = next_pos_ = pos;
    refresh_step_ = refresh_step;
    ended_ = false;
    reconfigure_ = true;  // the worker (re)applies box and settings first
    stride_ = 1;
    stride_hold_ = 0;
    frame_ms_ = 0;
    warmup_ = kWarmupFrames;
    thread_ = std::thread([this] { worker(); });
}

void RenderAhead::stop()
{
    {
        std::lock_guard l(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard l(m_);
    // Queued frames are dropped; the shown one stays (the window keeps showing it until a new one is ready).
    for (auto& f : queue_) free_.push_back(f.slot);
    queue_.clear();
}

double RenderAhead::grid(const Stats& ps) const
{
    if (!ps.frame_gen_active) return 1.0;
    if (settings_.frame_gen_multiplier > 0) return 1.0 / settings_.frame_gen_multiplier;
    return refresh_step_ * stride_;
}

void RenderAhead::worker()
{
    cuda_.make_current();
    for (;;) {
        Size box;
        Settings s;
        bool reconf = false;
        double pos = 0;
        int slot = -1;
        {
            std::unique_lock l(m_);
            cv_.wait(l, [&] {
                return stop_ || reconfigure_ || (!ended_ && queue_.size() < size_t(kMaxQueued) && !free_.empty());
            });
            if (stop_) return;
            reconf = reconfigure_;
            reconfigure_ = false;
            if (reset_requested_) {
                reset_requested_ = false;
                pipe_.reset_stats();  // the worker owns the pipeline; cheap
            }
            box = box_;
            s = settings_;
            if (reconf) {
                // Frames prepared with the old settings / size are dropped; continue where the display is.
                for (auto& f : queue_) free_.push_back(f.slot);
                queue_.clear();
                next_pos_ = display_pos_;
                ended_ = false;
            }
        }
        if (reconf) {
            pipe_.configure(box, s);  // may load AI models (seconds); the window keeps running meanwhile
            Stats ps;
            pipe_.fill_stats(ps);
            std::lock_guard l(m_);
            pipe_stats_ = ps;
            stride_ = 1;
            stride_hold_ = 0;
            frame_ms_ = 0;
            warmup_ = kWarmupFrames;
            continue;  // re-check stop / room
        }
        {
            std::lock_guard l(m_);
            if (free_.empty()) continue;
            const double g = grid(pipe_stats_);
            // Never prepare a position the display has already passed: jump ahead on the grid.
            if (next_pos_ + kEps < display_pos_) next_pos_ += std::ceil((display_pos_ - next_pos_) / g - kEps) * g;
            pos = next_pos_;
            slot = free_.back();
            free_.pop_back();
        }

        const auto t0 = SteadyClock::now();
        const int mult = s.frame_gen_multiplier > 0 ? s.frame_gen_multiplier : 0;
        std::optional<Output> out;
        try {
            out = pipe_.produce(pos, mult);
        } catch (const std::exception& e) {
            log::error("render worker: {}", e.what());
        }
        if (out) {
            const size_t bytes = size_t(out->size.w) * out->size.h * 4;
            if (pool_[slot].size() != bytes) pool_[slot] = gpu::CudaBuffer(bytes);
            NITID_CU(cuMemcpyDtoDAsync(pool_[slot].ptr(), out->ptr, bytes, cuda_.stream));
            if (out->orig) {
                const size_t ob = size_t(out->orig_size.w) * out->orig_size.h * 4;
                if (pool_orig_[slot].size() != ob) pool_orig_[slot] = gpu::CudaBuffer(ob);
                NITID_CU(cuMemcpyDtoDAsync(pool_orig_[slot].ptr(), out->orig, ob, cuda_.stream));
            }
            cuda_.sync();
        }
        const double ms = ms_since(t0);
        Stats ps;
        pipe_.fill_stats(ps);

        std::lock_guard l(m_);
        pipe_stats_ = ps;
        if (!out) {
            free_.push_back(slot);
            ended_ = true;
            cv_.notify_all();
            continue;
        }
        if (reconfigure_ || stop_) {  // settings changed while it was being made: drop it
            free_.push_back(slot);
            continue;
        }
        queue_.push_back(Frame{slot, pool_[slot].ptr(), out->size, pos, out->generated,
                               out->orig ? pool_orig_[slot].ptr() : 0, out->orig_size});
        // Frame-gen pacing (one frame per refresh mode): if a frame costs more than 95 % of the time it may
        // take, prepare one every `stride` refreshes instead (evenly paced), and only come back down after a
        // while below the threshold (tests/v0.3/01: 90 % flapped between 2 and 3 at HIGH).
        if (warmup_ > 0) {
            --warmup_;
        } else if (ps.frame_gen_active && s.frame_gen_multiplier == 0 && period_s_ > 0) {
            frame_ms_ = frame_ms_ > 0 ? frame_ms_ + (ms - frame_ms_) * 0.1 : ms;
            const int want = frame_gen_stride(frame_ms_, period_s_, kMaxStride);
            if (want > stride_) {
                log::info("frame generation can't keep up ({:.1f} ms per frame): one frame every {} refreshes", frame_ms_, want);
                stride_ = want;
                stride_hold_ = kStrideHoldFrames;
            } else if (want < stride_ && --stride_hold_ <= 0) {
                stride_ = want;
                stride_hold_ = kStrideHoldFrames;
            }
        }
        next_pos_ = pos + grid(ps);
        cv_.notify_all();
    }
}

std::optional<RenderAhead::Frame> RenderAhead::take(double pos, double refresh_step, double period_s)
{
    std::lock_guard l(m_);
    display_pos_ = pos;
    refresh_step_ = refresh_step;
    period_s_ = period_s;
    std::optional<Frame> newest;
    while (!queue_.empty() && queue_.front().pos <= pos + kEps) {
        if (newest) free_.push_back(newest->slot);  // passed without being shown
        newest = queue_.front();
        queue_.pop_front();
    }
    if (newest) {
        if (shown_) free_.push_back(shown_->slot);
        shown_ = newest;
        ++new_frames_;
    } else if (shown_ && !ended_ && thread_.joinable() && pos - shown_->pos >= grid(pipe_stats_) - kEps) {
        ++late_;  // a newer frame was due and isn't ready
    }
    cv_.notify_all();
    return shown_;
}

std::optional<RenderAhead::Frame> RenderAhead::wait_first(int timeout_ms)
{
    std::unique_lock l(m_);
    cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return !queue_.empty() || ended_ || stop_; });
    if (queue_.empty()) return std::nullopt;
    if (shown_) free_.push_back(shown_->slot);
    shown_ = queue_.front();
    queue_.pop_front();
    ++new_frames_;
    cv_.notify_all();
    return shown_;
}

std::optional<RenderAhead::Frame> RenderAhead::shown() const
{
    std::lock_guard l(m_);
    return shown_;
}

bool RenderAhead::finished() const
{
    std::lock_guard l(m_);
    return ended_ && queue_.empty();
}

void RenderAhead::fill_stats(Stats& s) const
{
    std::lock_guard l(m_);
    const Stats& p = pipe_stats_;
    s.source_width = p.source_width, s.source_height = p.source_height;
    s.work_width = p.work_width, s.work_height = p.work_height;
    s.upscaler_active = p.upscaler_active, s.frame_gen_active = p.frame_gen_active;
    s.upscaler_note = p.upscaler_note, s.frame_gen_note = p.frame_gen_note;
    s.upscaler_mode = p.upscaler_mode;
    s.cleanup_active = p.cleanup_active, s.cleanup_note = p.cleanup_note;
    s.source_frames = p.source_frames, s.skipped_frames = p.skipped_frames;
    s.generated_frames = p.generated_frames, s.scene_cuts = p.scene_cuts;
    s.avg.decode_ms = p.avg.decode_ms, s.avg.upload_ms = p.avg.upload_ms;
    s.avg.upscale_ms = p.avg.upscale_ms, s.avg.frame_gen_ms = p.avg.frame_gen_ms;
    s.late_frames = late_;
    s.frame_gen_stride = (p.frame_gen_active && settings_.frame_gen_multiplier == 0) ? stride_ : 0;
}

void RenderAhead::reset_stats()
{
    std::lock_guard l(m_);
    late_ = new_frames_ = 0;
    reset_requested_ = true;
}

int RenderAhead::stride() const
{
    std::lock_guard l(m_);
    return stride_;
}

} // namespace nitid::pipeline
