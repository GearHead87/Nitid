#pragma once

// The processing chain, independent of where frames come from and where they go:
//
//   FrameSource --upload--> [upscaler: VFX VSR, FSR 1 or NIS] --> two-frame ring (current, next) --> [VFX frame gen] --> output
//
// produce(position) returns a GPU pointer to the RGBA8 frame for that moment: a real frame, or one
// generated between `current` and `next`. Callers (window presenter, headless runner) copy it out.
#include "core/running_average.hpp"
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
};

class Pipeline {
public:
    Pipeline(gpu::CudaContext& cuda, std::shared_ptr<const vfx::Api> vfx, bool can_super_res, bool can_frame_gen);
    ~Pipeline();

    void set_source(input::FrameSource* source);  // nullptr = none; clears the ring
    void restart();                               // source restarted from frame 0
    // Output box (window size, or the requested headless size) and settings. Cheap when nothing changed.
    void configure(Size box, const Settings& settings);

    // Frame for `position` (in source frames). multiplier > 0 rounds to k/multiplier steps.
    // nullopt once the source has ended and position is past the last frame.
    std::optional<Output> produce(double position, int multiplier);

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
        bool scene_cut = false;  // a cut happened between the previous real frame and this one
        bool valid() const { return index >= 0; }
    };

    bool fetch_next(int64_t min_index);  // decode + upload one frame into slots_[next_]
    void upload(input::DecodedFrame& f, Slot& dst);
    void allocate_ring();
    template <class F> void timed(RunningAverage& avg, F&& fn);

    gpu::CudaContext& cuda_;
    std::shared_ptr<const vfx::Api> vfx_;
    bool can_sr_, can_fg_;
    input::FrameSource* source_ = nullptr;

    Settings settings_;
    Size box_, src_, work_;
    enum class Up { None, Vsr, Spatial };
    Up up_ = Up::None;  // upscaler actually running
    bool sr_active_ = false, fg_active_ = false;  // sr_active_: any upscaler running
    bool sr_failed_ = false, fg_failed_ = false;
    bool compare_ = false;
    std::string sr_note_, fg_note_;

    std::unique_ptr<vfx::SuperRes> sr_;
    std::unique_ptr<gpu::SpatialScaler> spatial_;
    std::unique_ptr<vfx::FrameGen> fg_;
    gpu::CudaBuffer upload_;  // source-size frame (VSR input)
    Slot slots_[2];
    int cur_ = 0, next_ = 1;
    uint64_t generation_ = 0;
    bool ended_ = false;
    bool pending_cut_ = false;  // cut seen on a skipped frame, carried to the next uploaded one

    // Stats
    RunningAverage decode_ms_, upload_ms_, upscale_ms_, frame_gen_ms_;
    uint64_t source_frames_ = 0, skipped_ = 0, generated_ = 0, cuts_ = 0;
};

} // namespace nitid::pipeline
