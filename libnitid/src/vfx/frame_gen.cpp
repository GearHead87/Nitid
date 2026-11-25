#include "vfx/frame_gen.hpp"

#include "core/log.hpp"

namespace nitid::vfx {

FrameGen::FrameGen(std::shared_ptr<const Api> api, CUstream stream) : api_(std::move(api)), stream_(stream) {}

FrameGen::~FrameGen() { reset(); }

void FrameGen::reset()
{
    if (fx_) api_->DestroyEffect(fx_);
    fx_ = nullptr;
    out_ = {};
    w_ = h_ = 0;
    mode_ = ~0u;
}

void FrameGen::configure(unsigned width, unsigned height, unsigned mode)
{
    if (fx_ && width == w_ && height == h_ && mode == mode_) return;
    reset();
    out_ = gpu::CudaBuffer(size_t(width) * height * 4);
    out_img_ = rgba_image(*api_, reinterpret_cast<void*>(out_.ptr()), width, height);
    const Api& a = *api_;
    a.check(a.CreateEffect("VideoFrameGeneration", &fx_), "CreateEffect(VideoFrameGeneration)");
    a.check(a.SetU32(fx_, "InputWidth", width), "VideoFrameGeneration InputWidth");
    a.check(a.SetU32(fx_, "InputHeight", height), "VideoFrameGeneration InputHeight");
    a.check(a.SetU32(fx_, "Mode", mode), "VideoFrameGeneration Mode");
    a.check(a.SetU32(fx_, "AutomaticShotChangeDetectionEnabled", 0), "VideoFrameGeneration shot-change detection");
    a.check(a.SetCudaStream(fx_, NVVFX_CUDA_STREAM, stream_), "VideoFrameGeneration stream");
    a.check(a.Load(fx_), "VideoFrameGeneration load");
    a.check(a.SetImage(fx_, NVVFX_OUTPUT_IMAGE, &out_img_), "VideoFrameGeneration output");
    a.check(a.SetU32(fx_, "FrameMultiplier", 0), "VideoFrameGeneration FrameMultiplier");  // 0 = use Timestep
    w_ = width, h_ = height, mode_ = mode;
    log::info("Frame generation loaded: {}x{}, mode {}", width, height, mode);
}

void FrameGen::run(CUdeviceptr previous, CUdeviceptr current, float timestep, bool shot_change)
{
    const Api& a = *api_;
    prev_img_ = rgba_image(a, reinterpret_cast<void*>(previous), w_, h_);
    cur_img_ = rgba_image(a, reinterpret_cast<void*>(current), w_, h_);
    a.check(a.SetImage(fx_, NVVFX_INPUT_IMAGE_0, &prev_img_), "VideoFrameGeneration previous");
    a.check(a.SetImage(fx_, NVVFX_INPUT_IMAGE_1, &cur_img_), "VideoFrameGeneration current");
    a.check(a.SetU32(fx_, "ShotChange", shot_change ? 1 : 0), "VideoFrameGeneration ShotChange");
    a.check(a.SetF32(fx_, "Timestep", timestep), "VideoFrameGeneration Timestep");
    a.check(a.Run(fx_, 1), "VideoFrameGeneration run");
}

} // namespace nitid::vfx
