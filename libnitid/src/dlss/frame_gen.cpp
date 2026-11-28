#include "dlss/frame_gen.hpp"

#include "core/check.hpp"
#include "core/log.hpp"

namespace nitid::dlss {

namespace {

constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

NitidDlssImage ngx_image(gpu::Image& im)
{
    return {im.image, im.view, im.format, im.w, im.h};
}

} // namespace

FrameGen::FrameGen(gpu::VulkanContext& vk, gpu::CudaContext& cuda, const Runtime& rt)
    : vk_(vk), cuda_(cuda), rt_(rt), runner_(vk, vk.queue_family)
{
}

FrameGen::~FrameGen() { reset(); }

void FrameGen::reset()
{
    runner_.wait();
    if (feature_) rt_.api().release(feature_);
    feature_ = nullptr;
    back_ = {}, out_ = {}, zero_motion_ = {}, depth_ = {};
    older_buf_.reset(), newer_buf_.reset();
    outs_.clear();
    w_ = h_ = 0;
    restart();
}

void FrameGen::restart()
{
    last_fed_ = -1;
    done_index_ = -1;
    done_count_ = 0;
}

void FrameGen::configure(uint32_t w, uint32_t h, uint32_t mv_w, uint32_t mv_h, bool lite)
{
    if (feature_ && w == w_ && h == h_ && depth_.w == mv_w && depth_.h == mv_h && lite == lite_) return;
    reset();
    const std::vector<uint32_t> fam{vk_.queue_family};
    back_ = gpu::Image(vk_, w, h, VK_FORMAT_R8G8B8A8_UNORM, kUsage, fam);
    out_ = gpu::Image(vk_, w, h, VK_FORMAT_R8G8B8A8_UNORM, kUsage, fam);
    zero_motion_ = gpu::Image(vk_, mv_w, mv_h, VK_FORMAT_R16G16_SFLOAT, kUsage, fam);
    depth_ = gpu::Image(vk_, mv_w, mv_h, VK_FORMAT_R32_SFLOAT, kUsage, fam);
    older_buf_ = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(w) * h * 4);
    newer_buf_ = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(w) * h * 4);

    NitidDlssFrameGenCreate c{w, h, mv_w, mv_h, VK_FORMAT_R8G8B8A8_UNORM, lite ? 1 : 0};
    char err[512] = {};
    VkCommandBuffer cb = runner_.begin();
    gpu::clear(cb, zero_motion_, 0.0f);
    gpu::clear(cb, depth_, 0.5f);
    for (auto* im : {&back_, &out_, &zero_motion_, &depth_}) gpu::transition(cb, *im, VK_IMAGE_LAYOUT_GENERAL);
    feature_ = rt_.api().frame_gen_create(cb, &c, err, sizeof err);
    runner_.submit(vk_.work_queue);
    if (!feature_) {
        reset();
        failf("DLSS: {}", err);
    }
    w_ = w, h_ = h, lite_ = lite;
    log::info("DLSS Frame Generation {}x{} (motion {}x{}{})", w, h, mv_w, mv_h, lite ? ", lite" : "");
}

void FrameGen::generate(CUdeviceptr older, int64_t older_index, CUdeviceptr newer, int64_t newer_index, int count,
                        gpu::OpticalFlow* flow)
{
    if (newer_index == done_index_ && count == done_count_) return;
    const size_t bytes = size_t(w_) * h_ * 4;
    while (outs_.size() < size_t(count)) outs_.push_back(std::make_unique<gpu::SharedBuffer>(vk_, bytes));
    const bool feed_older = last_fed_ != older_index;  // start, seek, cut, or a skipped pair: prime with `older`
    if (feed_older) NITID_CU(cuMemcpyDtoDAsync(older_buf_->ptr(), older, bytes, cuda_.stream));
    NITID_CU(cuMemcpyDtoDAsync(newer_buf_->ptr(), newer, bytes, cuda_.stream));
    cuda_.sync();

    char err[512] = {};
    VkCommandBuffer cb = runner_.begin();
    auto eval = [&](gpu::Image& mv, uint32_t n, uint32_t k, bool reset) {
        gpu::transition(cb, back_, VK_IMAGE_LAYOUT_GENERAL);
        gpu::transition(cb, mv, VK_IMAGE_LAYOUT_GENERAL);
        gpu::transition(cb, out_, VK_IMAGE_LAYOUT_GENERAL);
        NitidDlssFrameGenEval e{ngx_image(back_), ngx_image(mv), ngx_image(depth_), ngx_image(out_), n, k, frame_id_,
                                reset ? 1 : 0};
        if (!rt_.api().frame_gen_evaluate(cb, feature_, &e, err, sizeof err)) {
            runner_.submit(vk_.work_queue);
            restart();
            failf("DLSS: {}", err);
        }
    };
    if (feed_older) {
        // DLSS-G keeps the previous backbuffer itself: give it `older` first (its generated frame is not used).
        gpu::copy_buffer_to_image(cb, older_buf_->buffer(), back_);
        eval(zero_motion_, 1, 1, true);
        ++frame_id_;
        gpu::full_barrier(cb);
    }
    gpu::copy_buffer_to_image(cb, newer_buf_->buffer(), back_);
    if (flow) flow->record_upload(cb);
    gpu::Image& mv = flow ? flow->motion_image() : zero_motion_;
    for (int k = 1; k <= count; ++k) {
        eval(mv, uint32_t(count), uint32_t(k), false);
        gpu::copy_image_to_buffer(cb, out_, outs_[size_t(k - 1)]->buffer());
    }
    ++frame_id_;
    runner_.submit(vk_.work_queue);
    last_fed_ = newer_index;
    done_index_ = newer_index;
    done_count_ = count;
}

} // namespace nitid::dlss
