#pragma once

// Video file input through FFmpeg. Decodes on its own thread (NVDEC when available, else software),
// converts to RGBA8 and keeps a few frames ready in a queue.
//
// 0.1 shortcut: NVDEC frames are copied to the CPU for colour conversion and uploaded again. Keeping
// them on the GPU (CUDA NV12 -> RGBA) is the planned next step.
#include "core/bounded_queue.hpp"
#include "input/frame_source.hpp"
#include "input/scene_cut.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nitid::input {

class FfmpegFileSource final : public FrameSource {
public:
    // cuda_device: CUDA device ordinal used for NVDEC (the GPU Nitid runs on).
    FfmpegFileSource(const std::string& path, const OpenOptions& options, int cuda_device);
    ~FfmpegFileSource() override;

    const SourceInfo& info() const override { return info_; }
    std::optional<DecodedFrame> next() override;
    void recycle(std::vector<uint8_t>&& buffer) override;
    void restart() override;

    void set_scene_cut_detection(bool on) { detect_cuts_ = on; }

private:
    struct Ffmpeg;  // FFmpeg state, kept out of the header
    void start_thread();
    void stop_thread();
    void decode_loop();

    std::unique_ptr<Ffmpeg> ff_;
    SourceInfo info_;
    OpenOptions opts_;
    BoundedQueue<DecodedFrame> queue_{4};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> detect_cuts_{true};
    std::mutex pool_mutex_;
    std::vector<std::vector<uint8_t>> pool_;
    SceneCutDetector cuts_;
};

} // namespace nitid::input
