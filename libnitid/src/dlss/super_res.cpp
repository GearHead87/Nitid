#include "dlss/super_res.hpp"

#include "core/check.hpp"
#include "core/log.hpp"

#include <format>

namespace nitid::dlss {

namespace {

constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

NitidDlssImage ngx_image(gpu::Image& im)
{
    return {im.image, im.view, im.format, im.w, im.h};
}

std::string mode_name(uint32_t in_w, uint32_t out_w)
{
    const double r = double(out_w) / double(in_w);
    const char* m = r <= 1.01 ? "DLAA" : r <= 1.55 ? "Quality" : r <= 1.80 ? "Balanced" : r <= 2.05 ? "Performance" : "Ultra Performance";
    return r <= 1.01 ? std::string(m) : std::format("{} {:.2f}x", m, r);
}

} // namespace

SuperRes::SuperRes(gpu::VulkanContext& vk, gpu::CudaContext& cuda, const Runtime& rt)
    : vk_(vk), cuda_(cuda), rt_(rt), runner_(vk, vk.queue_family)
{
}

SuperRes::~SuperRes() { reset(); }

void SuperRes::reset()
{
    runner_.wait();
    if (feature_) rt_.api().release(feature_);
    feature_ = nullptr;
    color_ = {}, zero_motion_ = {}, depth_ = {}, out_ = {};
    in_buf_.reset(), out_buf_.reset();
    mode_.clear();
}

void SuperRes::configure(uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, int preset)
{
    if (feature_ && color_.w == in_w && color_.h == in_h && out_.w == out_w && out_.h == out_h && preset_ == preset)
        return;  // unchanged (e.g. only frame generation settings changed): keep the history
    reset();
    preset_ = preset;
    const std::vector<uint32_t> fam{vk_.queue_family};
    color_ = gpu::Image(vk_, in_w, in_h, VK_FORMAT_R8G8B8A8_UNORM, kUsage, fam);
    zero_motion_ = gpu::Image(vk_, in_w, in_h, VK_FORMAT_R16G16_SFLOAT, kUsage, fam);
    depth_ = gpu::Image(vk_, in_w, in_h, VK_FORMAT_R32_SFLOAT, kUsage, fam);
    out_ = gpu::Image(vk_, out_w, out_h, VK_FORMAT_R8G8B8A8_UNORM, kUsage, fam);
    in_buf_ = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(in_w) * in_h * 4);
    out_buf_ = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(out_w) * out_h * 4);

    NitidDlssSuperResCreate c{in_w, in_h, out_w, out_h, preset};
    char err[512] = {};
    VkCommandBuffer cb = runner_.begin();
    gpu::clear(cb, zero_motion_, 0.0f);
    gpu::clear(cb, depth_, 0.5f);  // a flat plane: video has no depth
    for (auto* im : {&color_, &zero_motion_, &depth_, &out_}) gpu::transition(cb, *im, VK_IMAGE_LAYOUT_GENERAL);
    feature_ = rt_.api().super_res_create(cb, &c, err, sizeof err);
    runner_.submit(vk_.work_queue);
    if (!feature_) {
        reset();
        failf("DLSS: {}", err);
    }
    mode_ = mode_name(in_w, out_w);
    log::info("DLSS Super Resolution {}x{} -> {}x{} ({}{})", in_w, in_h, out_w, out_h, mode_,
              preset ? std::format(", preset {}", char('A' + preset - 1)) : std::string());
}

void SuperRes::run(bool reset_history)
{
    VkCommandBuffer cb = runner_.begin();
    gpu::copy_buffer_to_image(cb, in_buf_->buffer(), color_);
    gpu::transition(cb, color_, VK_IMAGE_LAYOUT_GENERAL);
    if (flow_) flow_->record_upload(cb);
    gpu::Image& mv = flow_ ? flow_->motion_image() : zero_motion_;
    gpu::transition(cb, mv, VK_IMAGE_LAYOUT_GENERAL);
    gpu::transition(cb, out_, VK_IMAGE_LAYOUT_GENERAL);
    NitidDlssSuperResEval e{ngx_image(color_), ngx_image(mv), ngx_image(depth_), ngx_image(out_), reset_history ? 1 : 0};
    char err[512] = {};
    if (!rt_.api().super_res_evaluate(cb, feature_, &e, err, sizeof err)) {
        runner_.submit(vk_.work_queue);
        failf("DLSS: {}", err);
    }
    gpu::copy_image_to_buffer(cb, out_, out_buf_->buffer());
    runner_.submit(vk_.work_queue);  // waits: CUDA reads output() next
}

} // namespace nitid::dlss
