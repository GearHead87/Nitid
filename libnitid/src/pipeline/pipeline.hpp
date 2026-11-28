#pragma once

// The processing chain, independent of where frames come from and where they go:
//
//   FrameSource --upload--> [upscaler: VFX VSR, DLSS, FSR 1 or NIS] --> two-frame ring (current, next) --> [VFX frame gen] --> output
//
// produce(position) returns a GPU pointer to the RGBA8 frame for that moment: a real frame, or one
// generated between `current` and `next`. Callers (window presenter, headless runner) copy it out.
#include "core/running_average.hpp"
#include "dlss/frame_gen.hpp"
#include "dlss/super_res.hpp"
#include "gpu/cuda_context.hpp"
#include "gpu/spatial_scaler.hpp"
#include "input/frame_source.hpp"
#include "pipeline/frame_layout.hpp"
#include "vfx/frame_gen.hpp"
#include "vfx/super_res.hpp"

#include <nitid/settings.hpp>
#include <nitid/stats.hpp>

#include <memory>
#include <optional>
#include <string>

namespace nitid::pipeline {

struct Output {
    CUdeviceptr ptr = 0;
    Size size;
    bool generated = false;
    // Compare mode: the original source frame nearest in time (source size), or 0.
    CUdeviceptr orig = 0;
    Size orig_size;
    double arrival_s = 0;  // live sources: when the (newest real) frame shown arrived
};

// GPU pieces beyond CUDA + VFX, used by the DLSS stages (all may be missing).
struct GpuExtras {
    gpu::VulkanContext* vk = nullptr;     // device with the pipeline thread's queue (work_queue)
    gpu::Kernels* kernels = nullptr;      // optical flow's luma / motion kernels
    const dlss::Runtime* dlss = nullptr;  // started DLSS runtime, or nullptr
    std::string dlss_note;                // why DLSS is unavailable, if it is
};

class Pipeline {
public:
    Pipeline(gpu::CudaContext& cuda, std::shared_ptr<const vfx::Api> vfx, bool can_super_res, bool can_frame_gen,
             GpuExtras extras = {});
    ~Pipeline();

    void set_source(input::FrameSource* source);  // nullptr = none; clears the ring
    void restart();                               // source restarted from frame 0
    // Output box (window size, or the requested headless size) and settings. Cheap when nothing changed.
    void configure(Size box, const Settings& settings);

    // Frame for `position` (in source frames). multiplier > 0 rounds to k/multiplier steps (DLSS frame generation
    // needs one: 0 is treated as 2).
    // nullopt once the source has ended and position is past the last frame.
    std::optional<Output> produce(double position, int multiplier);

    // Live sources (screen capture): takes the newest frame that arrived (if any) and returns what to show:
    //   interpolate = false: the newest frame (lowest latency);
    //   interpolate = true: the moment `target_s` (arrival clock) between the last two frames, generated when frame
    //   generation is on (smooth, about one frame interval behind).
    std::optional<Output> produce_live(bool interpolate, double target_s);
    double live_interval() const { return live_interval_; }  // average time between arriving frames

    Size work_size() const { return work_; }
    // Changes whenever the frame buffers are reallocated (an Output from before is then invalid).
    uint64_t generation() const { return generation_; }
    void fill_stats(Stats& s) const;
    void reset_stats();

private:
    struct Slot {
        gpu::CudaBuffer buf;
        gpu::CudaBuffer orig;    // compare mode: the source frame as decoded
        bool has_orig = false;
        int64_t index = -1;
        double arrival = 0;      // live sources
        bool scene_cut = false;  // a cut happened between the previous real frame and this one
        bool valid() const { return index >= 0; }
    };

    bool fetch_next(int64_t min_index);  // decode + upload one frame into slots_[next_]
    void upload(input::DecodedFrame& f, Slot& dst, bool cut);
    void allocate_ring();
    template <class F> void timed(RunningAverage& avg, F&& fn);
    // DLSS-G: frame k of m between slots c and n (generating the pair's frames when needed).
    CUdeviceptr dlss_generated(const Slot& c, const Slot& n, int m, int k);

    gpu::CudaContext& cuda_;
    std::shared_ptr<const vfx::Api> vfx_;
    bool can_sr_, can_fg_;
    GpuExtras gx_;
    input::FrameSource* source_ = nullptr;

    Settings settings_;
    Size box_, src_, work_;
    enum class Up { None, Vsr, Spatial, Dlss };
    Up up_ = Up::None;  // upscaler actually running
    bool sr_active_ = false, fg_active_ = false;  // sr_active_: any upscaler running
    bool sr_failed_ = false, fg_failed_ = false, dlss_failed_ = false, dlss_fg_failed_ = false;
    bool fg_dlss_ = false;   // frame generation is DLSS-G (else VFX)
    bool fg_motion_ = false; // DLSS-G gets optical flow motion vectors (lite mode)
    bool dlss_reset_ = true;  // DLSS history: start over with the next frame (start, seek, new size)
    bool compare_ = false;
    double live_interval_ = 0, last_arrival_ = 0;
    std::string sr_note_, fg_note_, cleanup_note_;
    bool cleanup_active_ = false;

    std::unique_ptr<vfx::SuperRes> sr_;
    std::unique_ptr<gpu::SpatialScaler> spatial_;
    std::unique_ptr<dlss::SuperRes> dlss_sr_;
    std::unique_ptr<dlss::FrameGen> dlss_fg_;
    std::unique_ptr<gpu::OpticalFlow> flow_;  // motion for DLSS (source size)
    bool flow_active_ = false;
    bool flow_reset_ = true;  // next frame starts a new motion sequence (start, seek, reconfigure)
    std::unique_ptr<vfx::FrameGen> fg_;
    gpu::CudaBuffer upload_;  // source-size frame (VSR input)
    Slot slots_[2];
    int cur_ = 0, next_ = 1;
    uint64_t generation_ = 0;
    bool ended_ = false;
    bool pending_cut_ = false;  // cut seen on a skipped frame, carried to the next uploaded one

    // Stats
    RunningAverage decode_ms_, upload_ms_, upscale_ms_, frame_gen_ms_, motion_ms_;
    uint64_t source_frames_ = 0, skipped_ = 0, generated_ = 0, cuts_ = 0;
};

} // namespace nitid::pipeline
