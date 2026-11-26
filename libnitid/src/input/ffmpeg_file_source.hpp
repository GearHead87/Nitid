#pragma once

// Video file input through FFmpeg. Decodes on its own thread and keeps a few RGBA8 frames ready.
//
//  - GPU path (NVDEC + Nitid's kernels): FFmpeg decodes into CUDA memory on Nitid's own CUDA context and
//    stream; a kernel converts NV12/P010 -> RGBA8 into a pooled GPU buffer. Nothing goes through the CPU
//    except a tiny luma thumbnail for scene-cut detection.
//  - CPU path (software decoding, other pixel formats, or no kernels): FFmpeg + swscale -> RGBA8 in CPU
//    memory, uploaded later by the pipeline.
#include "core/bounded_queue.hpp"
#include "gpu/kernels.hpp"
#include "input/frame_source.hpp"
#include "input/scene_cut.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct AVFrame;  // FFmpeg

namespace nitid::input {

// What the GPU path needs from the engine. Null = CPU path only.
struct GpuDecode {
    CUcontext ctx = nullptr;         // Nitid's CUDA context (FFmpeg decodes into it)
    gpu::Kernels* kernels = nullptr; // colour conversion
};

class FfmpegFileSource final : public FrameSource {
public:
    // cuda_device: CUDA device ordinal used for NVDEC (the GPU Nitid runs on).
    FfmpegFileSource(const std::string& path, const OpenOptions& options, int cuda_device, const GpuDecode* gpu);
    ~FfmpegFileSource() override;

    const SourceInfo& info() const override { return info_; }
    std::optional<DecodedFrame> next() override;
    void recycle(DecodedFrame&& frame) override;
    void seek(double seconds) override;

    void set_scene_cut_detection(bool on) { detect_cuts_ = on; }

private:
    struct Ffmpeg;  // FFmpeg state, kept out of the header
    void start_thread();
    void stop_thread();
    void decode_loop();
    bool convert_on_gpu(::AVFrame* fr, DecodedFrame& out);

    std::unique_ptr<Ffmpeg> ff_;
    SourceInfo info_;
    OpenOptions opts_;
    BoundedQueue<DecodedFrame> queue_{4};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> detect_cuts_{true};
    std::mutex pool_mutex_;
    std::vector<std::vector<uint8_t>> pool_;
    std::vector<gpu::CudaBuffer> gpu_pool_;
    SceneCutDetector cuts_;

    // GPU path
    CUcontext ctx_ = nullptr;
    gpu::Kernels* kernels_ = nullptr;
    CUstream stream_ = nullptr;      // decoder stream: FFmpeg's copies and our kernels, in order
    gpu::CudaBuffer thumb_dev_;
    std::vector<uint8_t> thumb_host_;
    bool warned_format_ = false;
};

} // namespace nitid::input
