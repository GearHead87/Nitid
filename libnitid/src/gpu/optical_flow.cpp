#include "gpu/optical_flow.hpp"

#include "core/log.hpp"
#include "gpu/vk_check.hpp"

namespace nitid::gpu {

namespace {

PFN_vkCreateOpticalFlowSessionNV p_create;
PFN_vkDestroyOpticalFlowSessionNV p_destroy;
PFN_vkBindOpticalFlowSessionImageNV p_bind;
PFN_vkCmdOpticalFlowExecuteNV p_exec;

constexpr VkFormat kLuma = VK_FORMAT_R8_UNORM;
constexpr VkFormat kFlow = VK_FORMAT_R16G16_SFIXED5_NV;  // S10.5 fixed point: value / 32 = pixels (S4)

} // namespace

bool OpticalFlow::available(const VulkanContext& vk, const Kernels& k)
{
    return vk.optical_flow && vk.work_queue && k.has_motion_kernels();
}

OpticalFlow::OpticalFlow(VulkanContext& vk, CudaContext& cuda, Kernels& kernels) : vk_(vk), cuda_(cuda), kernels_(kernels)
{
    if (!p_create) {
        p_create = reinterpret_cast<PFN_vkCreateOpticalFlowSessionNV>(vkGetDeviceProcAddr(vk.device, "vkCreateOpticalFlowSessionNV"));
        p_destroy = reinterpret_cast<PFN_vkDestroyOpticalFlowSessionNV>(vkGetDeviceProcAddr(vk.device, "vkDestroyOpticalFlowSessionNV"));
        p_bind = reinterpret_cast<PFN_vkBindOpticalFlowSessionImageNV>(vkGetDeviceProcAddr(vk.device, "vkBindOpticalFlowSessionImageNV"));
        p_exec = reinterpret_cast<PFN_vkCmdOpticalFlowExecuteNV>(vkGetDeviceProcAddr(vk.device, "vkCmdOpticalFlowExecuteNV"));
    }
    if (!p_create || !p_destroy || !p_bind || !p_exec) fail("VK_NV_optical_flow functions missing");
    copy_in_ = std::make_unique<CommandRunner>(vk, vk.queue_family);
    copy_out_ = std::make_unique<CommandRunner>(vk, vk.queue_family);
    flow_run_ = std::make_unique<CommandRunner>(vk, vk.flow_family);
    luma_ready_ = std::make_unique<Semaphore>(vk);
    flow_ready_ = std::make_unique<Semaphore>(vk);
}

OpticalFlow::~OpticalFlow() { release(); }

void OpticalFlow::release()
{
    if (copy_in_) copy_in_->wait();
    if (flow_run_) flow_run_->wait();
    if (copy_out_) copy_out_->wait();
    if (session_) p_destroy(vk_.device, session_, nullptr);
    session_ = VK_NULL_HANDLE;
    luma_[0] = {}, luma_[1] = {}, flow_ = {}, motion_ = {};
    luma_buf_.reset(), flow_buf_.reset(), motion_buf_.reset();
    w_ = h_ = 0;
    have_prev_ = hints_ = false;
}

void OpticalFlow::configure(uint32_t w, uint32_t h)
{
    if (w == w_ && h == h_ && session_) return;
    release();
    w_ = w, h_ = h;
    fw_ = (w + kGrid - 1) / kGrid, fh_ = (h + kGrid - 1) / kGrid;
    const std::vector<uint32_t> both{vk_.queue_family, vk_.flow_family};
    VkOpticalFlowImageFormatInfoNV in_info{VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV};
    in_info.usage = VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV;
    VkOpticalFlowImageFormatInfoNV out_info{VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV};
    out_info.usage = VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV;
    const VkImageUsageFlags copy = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (auto& l : luma_) l = Image(vk_, w, h, kLuma, copy, both, &in_info);
    flow_ = Image(vk_, fw_, fh_, kFlow, copy, both, &out_info);
    motion_ = Image(vk_, w, h, VK_FORMAT_R16G16_SFLOAT,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | copy, {vk_.queue_family});
    luma_buf_ = std::make_unique<SharedBuffer>(vk_, VkDeviceSize(w) * h);
    flow_buf_ = std::make_unique<SharedBuffer>(vk_, VkDeviceSize(fw_) * fh_ * 4);
    motion_buf_ = std::make_unique<SharedBuffer>(vk_, VkDeviceSize(w) * h * 4);
    NITID_CU(cuMemsetD8Async(motion_buf_->ptr(), 0, size_t(w) * h * 4, cuda_.stream));

    VkOpticalFlowSessionCreateInfoNV sci{VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_INFO_NV};
    sci.width = w;
    sci.height = h;
    sci.imageFormat = kLuma;
    sci.flowVectorFormat = kFlow;
    sci.outputGridSize = VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV;
    sci.performanceLevel = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_NV;  // F1: 4x4 MEDIUM, ~1 ms at 1080p
    NITID_VK(p_create(vk_.device, &sci, nullptr, &session_));
    NITID_VK(p_bind(vk_.device, session_, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_FLOW_VECTOR_NV, flow_.view,
                    VK_IMAGE_LAYOUT_GENERAL));
    cuda_.sync();
    log::info("optical flow {}x{} (4x4 grid, medium)", w, h);
}

void OpticalFlow::reset()
{
    have_prev_ = false;
    hints_ = false;
}

bool OpticalFlow::push(CUdeviceptr rgba)
{
    kernels_.rgba_to_luma(rgba, luma_buf_->ptr(), int(w_), int(h_), cuda_.stream);
    cuda_.sync();
    const int cur = cur_;
    cur_ ^= 1;

    // Luma -> image (work queue); the flow image goes back to GENERAL for the flow engine.
    VkCommandBuffer cb = copy_in_->begin();
    copy_buffer_to_image(cb, luma_buf_->buffer(), luma_[cur]);
    transition(cb, luma_[cur], VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, flow_, VK_IMAGE_LAYOUT_GENERAL);
    if (!have_prev_) {
        copy_in_->submit(vk_.work_queue);
        have_prev_ = true;
        hints_ = false;
        NITID_CU(cuMemsetD8Async(motion_buf_->ptr(), 0, size_t(w_) * h_ * 4, cuda_.stream));
        cuda_.sync();
        return false;
    }
    copy_in_->submit(vk_.work_queue, false, VK_NULL_HANDLE, luma_ready_->get());

    // Flow: input = current, reference = previous -> vectors current -> previous (flow queue).
    flow_run_->wait();  // the session must not be in use while rebinding
    NITID_VK(p_bind(vk_.device, session_, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_INPUT_NV, luma_[cur].view, VK_IMAGE_LAYOUT_GENERAL));
    NITID_VK(p_bind(vk_.device, session_, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_REFERENCE_NV, luma_[cur ^ 1].view,
                    VK_IMAGE_LAYOUT_GENERAL));
    cb = flow_run_->begin();
    VkOpticalFlowExecuteInfoNV ei{VK_STRUCTURE_TYPE_OPTICAL_FLOW_EXECUTE_INFO_NV};
    ei.flags = hints_ ? 0 : VK_OPTICAL_FLOW_EXECUTE_DISABLE_TEMPORAL_HINTS_BIT_NV;
    p_exec(cb, session_, &ei);
    flow_run_->submit(vk_.flow_queue, false, luma_ready_->get(), flow_ready_->get());

    // Flow grid -> buffer (work queue), then CUDA expands it.
    cb = copy_out_->begin();
    copy_image_to_buffer(cb, flow_, flow_buf_->buffer());
    copy_out_->submit(vk_.work_queue, true, flow_ready_->get());
    copy_in_->wait();
    flow_run_->wait();
    kernels_.flow_to_motion(flow_buf_->ptr(), int(fw_), int(fh_), kGrid, motion_buf_->ptr(), int(w_), int(h_), 1.0f, cuda_.stream);
    cuda_.sync();
    hints_ = true;
    return true;
}

void OpticalFlow::record_upload(VkCommandBuffer cb)
{
    copy_buffer_to_image(cb, motion_buf_->buffer(), motion_);
    transition(cb, motion_, VK_IMAGE_LAYOUT_GENERAL);
}

} // namespace nitid::gpu
