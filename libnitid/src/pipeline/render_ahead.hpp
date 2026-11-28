#pragma once

// Runs the pipeline (upscaling + frame generation) on its own thread, ahead of the display (window output).
//
// The render thread only presents: every refresh it asks for the newest prepared frame whose position has been
// reached. The worker prepares frames for the positions coming next, on a grid:
//   - frame gen per refresh: one frame every `stride` refreshes. stride is 1 when the GPU keeps up and grows
//     (2, 3, 4) when frame generation takes longer than a refresh (e.g. HIGH quality at 165 Hz), so motion stays
//     evenly paced at refresh / stride instead of stuttering;
//   - fixed multiplier m: every 1/m source frame; frame gen off: every source frame. DLSS frame generation always
//     works in whole steps: with "every refresh" it uses the step closest to the refresh rate (2x .. its maximum).
// The GPU work overlaps the render thread's waits for the display, and loading models (settings changes) no
// longer freezes the window.
//
// Threading: the Pipeline is used only by the worker while it runs (start..stop). Frames are copied into a small
// pool of buffers; the render thread holds one ("shown") until a newer one replaces it.
#include "gpu/cuda_context.hpp"
#include "pipeline/pipeline.hpp"

#include <nitid/settings.hpp>
#include <nitid/stats.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace nitid::pipeline {

class RenderAhead {
public:
    struct Frame {
        int slot = -1;        // pool buffer
        CUdeviceptr ptr = 0;
        Size size;
        double pos = 0;       // position in source frames
        bool generated = false;
        CUdeviceptr orig = 0; // compare mode: original frame (source size), or 0
        Size orig_size;
    };

    static constexpr int kPool = 6;     // shown + up to 4 queued + 1 being written
    static constexpr int kMaxQueued = 4;
    static constexpr int kMaxStride = 4;

    RenderAhead(gpu::CudaContext& cuda, Pipeline& pipe);
    ~RenderAhead();
    RenderAhead(const RenderAhead&) = delete;
    RenderAhead& operator=(const RenderAhead&) = delete;

    // Output box and settings; applied by the worker before its next frame (queued frames are dropped).
    void configure(Size box, const Settings& settings);
    // Starts the worker at `pos` (source frames). refresh_step: source frames per display refresh.
    void start(double pos, double refresh_step);
    // Stops the worker and drops all frames (the shown one too). The pipeline is free to use afterwards.
    void stop();
    bool running() const { return thread_.joinable(); }

    // Render thread, once per refresh: the display has reached `pos`. Returns the frame to show (the newest ready
    // one at or before pos; the previous one if nothing newer is ready), or nullopt before the first frame.
    // period_s: display refresh period (for frame-gen pacing).
    std::optional<Frame> take(double pos, double refresh_step, double period_s);
    std::optional<Frame> shown() const;
    // Waits up to `timeout_ms` for the first frame after start() (so playback starts with a picture).
    std::optional<Frame> wait_first(int timeout_ms);
    // The source has ended and every frame up to its end has been taken.
    bool finished() const;

    // Pipeline stats as of the worker's last frame (thread-safe copy), plus render-ahead counters.
    void fill_stats(Stats& s) const;
    void reset_stats();
    int stride() const;  // current frame-gen stride (1 = every refresh)

private:
    void worker();
    double grid(const Stats& ps) const;  // positions between prepared frames (m_ held)
    int multiplier(const Stats& ps) const;  // fixed multiplier in use, 0 = one frame per refresh (m_ held)

    gpu::CudaContext& cuda_;
    Pipeline& pipe_;
    std::thread thread_;

    mutable std::mutex m_;
    std::condition_variable cv_;      // worker waits for room / restart; render thread waits for the first frame
    bool stop_ = false;
    bool reconfigure_ = false;
    Size box_;
    Settings settings_;
    double refresh_step_ = 0.1;       // source frames per refresh
    double period_s_ = 0;             // display refresh period
    double display_pos_ = 0;          // where the display is (render thread)
    double next_pos_ = 0;             // next position the worker prepares
    bool ended_ = false;              // the worker hit the end of the source
    std::deque<Frame> queue_;
    std::optional<Frame> shown_;
    std::vector<int> free_;           // free pool slots
    std::vector<gpu::CudaBuffer> pool_;
    std::vector<gpu::CudaBuffer> pool_orig_;  // compare mode
    Size pool_size_;

    // Frame-gen pacing (worker): average cost of one prepared frame, and the stride it implies.
    double frame_ms_ = 0;
    int stride_ = 1;
    int stride_hold_ = 0;             // frames before the stride may drop again
    int warmup_ = 0;                  // frames after start / reconfigure not used for pacing (decoder and model
                                      // warm-up make them slow)

    Stats pipe_stats_;                // snapshot after each frame (guarded by m_)
    uint64_t late_ = 0;               // refreshes where a due frame was not ready (render thread)
    uint64_t new_frames_ = 0;         // distinct frames shown
    bool reset_requested_ = false;    // pipeline stats reset, done by the worker
};

} // namespace nitid::pipeline
