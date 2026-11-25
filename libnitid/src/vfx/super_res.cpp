#include "vfx/super_res.hpp"

#include "core/log.hpp"

namespace nitid::vfx {

SuperRes::SuperRes(std::shared_ptr<const Api> api, CUstream stream) : api_(std::move(api)), stream_(stream) {}

SuperRes::~SuperRes() { reset(); }

void SuperRes::reset()
{
    if (fx_) api_->DestroyEffect(fx_);
    fx_ = nullptr;
    out_ = {};
    in_w_ = in_h_ = out_w_ = out_h_ = 0;
    quality_ = ~0u;
}

void SuperRes::configure(CUdeviceptr input, unsigned in_w, unsigned in_h, unsigned out_w, unsigned out_h, unsigned quality)
{
    if (fx_ && input == in_ && in_w == in_w_ && in_h == in_h_ && out_w == out_w_ && out_h == out_h_ && quality == quality_)
        return;
    reset();
    out_ = gpu::CudaBuffer(size_t(out_w) * out_h * 4);
    in_img_ = rgba_image(*api_, reinterpret_cast<void*>(input), in_w, in_h);
    out_img_ = rgba_image(*api_, reinterpret_cast<void*>(out_.ptr()), out_w, out_h);
    const Api& a = *api_;
    a.check(a.CreateEffect("VideoSuperRes", &fx_), "CreateEffect(VideoSuperRes)");
    a.check(a.SetU32(fx_, "ImageEncodingMode", 0), "VideoSuperRes ImageEncodingMode");
    a.check(a.SetImage(fx_, NVVFX_INPUT_IMAGE, &in_img_), "VideoSuperRes input");
    a.check(a.SetImage(fx_, NVVFX_OUTPUT_IMAGE, &out_img_), "VideoSuperRes output");
    a.check(a.SetU32(fx_, "QualityLevel", quality), "VideoSuperRes QualityLevel");
    a.check(a.SetF32(fx_, "Strength", 1.0f), "VideoSuperRes Strength");
    a.check(a.SetCudaStream(fx_, NVVFX_CUDA_STREAM, stream_), "VideoSuperRes stream");
    a.check(a.Load(fx_), "VideoSuperRes load");
    in_ = input;
    in_w_ = in_w, in_h_ = in_h, out_w_ = out_w, out_h_ = out_h, quality_ = quality;
    log::info("VSR loaded: {}x{} -> {}x{}, quality {}", in_w, in_h, out_w, out_h, quality);
}

// Blocking run (async = 0): the SDK works on internal streams, and with async = 1 the result was not
// ready for the next operation on our stream (the window showed the previous frame; tests/v0.1/02).
void SuperRes::run() { api_->check(api_->Run(fx_, 0), "VideoSuperRes run"); }

} // namespace nitid::vfx
