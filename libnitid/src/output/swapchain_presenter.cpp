#include "output/swapchain_presenter.hpp"

#include "core/log.hpp"
#include "core/running_average.hpp"
#include "gpu/vk_check.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace nitid::output {

namespace {

double now_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
             VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

SwapchainPresenter::SwapchainPresenter(gpu::VulkanContext& vk, gpu::CudaContext& cuda, VkSurfaceKHR surface,
                                       pipeline::Size window)
    : vk_(vk), cuda_(cuda), surface_(surface), window_(window)
{
    if (vk_.present_wait)
        wait_for_present_ = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(vk_.device, "vkWaitForPresentKHR"));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = vk_.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& s : slots_) {
        NITID_VK(vkAllocateCommandBuffers(vk_.device, &ai, &s.cmd));
        NITID_VK(vkCreateFence(vk_.device, &fci, nullptr, &s.fence));
        NITID_VK(vkCreateSemaphore(vk_.device, &sci, nullptr, &s.acquired));
        s.ready = std::make_unique<gpu::SharedSemaphore>(vk_);
    }
    create_swapchain();
}

SwapchainPresenter::~SwapchainPresenter()
{
    vkDeviceWaitIdle(vk_.device);
    cuda_.sync();
    destroy_swapchain();
    for (auto& s : slots_) {
        free_slot_image(s);
        s.ready.reset();
        vkDestroySemaphore(vk_.device, s.acquired, nullptr);
        vkDestroyFence(vk_.device, s.fence, nullptr);
        vkFreeCommandBuffers(vk_.device, vk_.pool, 1, &s.cmd);
    }
    vkDestroySurfaceKHR(vk_.instance, surface_, nullptr);
}

void SwapchainPresenter::create_swapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    NITID_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_.phys, surface_, &caps));
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_.phys, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_.phys, surface_, &n, formats.data());
    format_ = VK_FORMAT_UNDEFINED;
    for (VkFormat want : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
        for (auto& f : formats)
            if (format_ == VK_FORMAT_UNDEFINED && f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                format_ = want;
    if (format_ == VK_FORMAT_UNDEFINED) fail("The window offers no 8-bit UNORM sRGB swapchain format");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) fail("The swapchain can't be a copy target");

    extent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{window_.w, window_.h};
    extent_.width = std::clamp(extent_.width, std::max(1u, caps.minImageExtent.width), caps.maxImageExtent.width);
    extent_.height = std::clamp(extent_.height, std::max(1u, caps.minImageExtent.height), caps.maxImageExtent.height);

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = std::max(4u, caps.minImageCount);  // on screen + 2 queued + 1 being drawn
    if (caps.maxImageCount) ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
    ci.imageFormat = format_;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    can_capture_ = caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | (can_capture_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // steady pacing (spike S7)
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain_;
    VkSwapchainKHR sw;
    NITID_VK(vkCreateSwapchainKHR(vk_.device, &ci, nullptr, &sw));
    destroy_swapchain();
    swapchain_ = sw;
    vkGetSwapchainImagesKHR(vk_.device, swapchain_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(vk_.device, swapchain_, &n, images_.data());
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    rendered_.resize(n);
    for (auto& s : rendered_) NITID_VK(vkCreateSemaphore(vk_.device, &sci, nullptr, &s));
    dirty_ = false;
    last_shown_s_ = -1;
    settle_until_ = present_id_ + uint64_t(0.5 / period_s_);
    log::debug("swapchain {}x{}, {} images", extent_.width, extent_.height, n);
}

void SwapchainPresenter::destroy_swapchain()
{
    for (auto s : rendered_) vkDestroySemaphore(vk_.device, s, nullptr);
    rendered_.clear();
    images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(vk_.device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void SwapchainPresenter::resize(pipeline::Size window)
{
    if (window == window_) return;
    window_ = window;
    dirty_ = true;
}

void SwapchainPresenter::free_slot_image(Slot& s)
{
    if (s.image) vkDestroyImage(vk_.device, s.image, nullptr);
    if (s.memory) vkFreeMemory(vk_.device, s.memory, nullptr);
    s.image = VK_NULL_HANDLE;
    s.memory = VK_NULL_HANDLE;
    s.buffer.reset();
    s.size = {};
}

void SwapchainPresenter::ensure_slot_size(Slot& s, pipeline::Size size)
{
    if (s.size == size && s.image) return;
    free_slot_image(s);
    s.buffer = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(size.w) * size.h * 4);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {size.w, size.h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    NITID_VK(vkCreateImage(vk_.device, &ci, nullptr, &s.image));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(vk_.device, s.image, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk_.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    NITID_VK(vkAllocateMemory(vk_.device, &ai, nullptr, &s.memory));
    NITID_VK(vkBindImageMemory(vk_.device, s.image, s.memory, 0));
    s.size = size;
}

void SwapchainPresenter::save_capture(VkBuffer, VkDeviceMemory mem)
{
    void* map = nullptr;
    NITID_VK(vkMapMemory(vk_.device, mem, 0, VK_WHOLE_SIZE, 0, &map));
    const auto* px = static_cast<const uint8_t*>(map);
    const bool bgra = format_ == VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<uint8_t> rgb(size_t(extent_.width) * extent_.height * 3);
    for (size_t i = 0; i < size_t(extent_.width) * extent_.height; ++i) {
        rgb[i * 3 + 0] = px[i * 4 + (bgra ? 2 : 0)];
        rgb[i * 3 + 1] = px[i * 4 + 1];
        rgb[i * 3 + 2] = px[i * 4 + (bgra ? 0 : 2)];
    }
    vkUnmapMemory(vk_.device, mem);
    if (FILE* f = std::fopen(capture_path_.c_str(), "wb")) {
        std::fprintf(f, "P6 %u %u 255\n", extent_.width, extent_.height);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
        log::info("saved window image {}x{} to {}", extent_.width, extent_.height, capture_path_);
    } else {
        log::error("cannot write {}", capture_path_);
    }
}

double SwapchainPresenter::present(CUdeviceptr frame, pipeline::Size size)
{
    Slot& s = slots_[frame_ % kFrames];
    NITID_VK(vkWaitForFences(vk_.device, 1, &s.fence, VK_TRUE, UINT64_MAX));
    if (dirty_) {
        vkDeviceWaitIdle(vk_.device);
        create_swapchain();
    }

    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(vk_.device, swapchain_, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(vk_.device);
        create_swapchain();
        ar = vkAcquireNextImageKHR(vk_.device, swapchain_, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &idx);
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) failf("Vulkan: acquiring a window image failed ({})", int(ar));
    if (ar == VK_SUBOPTIMAL_KHR) dirty_ = true;

    // CUDA: copy the pipeline frame into this slot's shared buffer, then signal Vulkan.
    double copy_ms = 0;
    const bool has_frame = frame && size.w && size.h;
    if (has_frame) {
        ensure_slot_size(s, size);
        auto t0 = SteadyClock::now();
        NITID_CU(cuMemcpyDtoDAsync(s.buffer->ptr(), frame, size_t(size.w) * size.h * 4, cuda_.stream));
        s.ready->cuda_signal(cuda_.stream);
        cuda_.sync();
        copy_ms = ms_since(t0);
    }

    // Vulkan: buffer -> image -> scaled blit onto the black-cleared window image.
    VkCommandBuffer cb = s.cmd;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NITID_VK(vkBeginCommandBuffer(cb, &bi));
    VkImage target = images_[idx];
    const auto T = VK_PIPELINE_STAGE_TRANSFER_BIT;
    barrier(cb, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, T);
    VkClearColorValue black{{0, 0, 0, 1}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    if (has_frame) {
        barrier(cb, s.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                T, T);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {size.w, size.h, 1};
        vkCmdCopyBufferToImage(cb, s.buffer->buffer(), s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        barrier(cb, s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, T, T);
        barrier(cb, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, T, T);  // clear before blit
        pipeline::Size out{extent_.width, extent_.height};
        pipeline::Rect r = pipeline::centre(pipeline::fit(size, out), out);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {int32_t(size.w), int32_t(size.h), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = {r.x, r.y, 0};
        blit.dstOffsets[1] = {r.x + int32_t(r.w), r.y + int32_t(r.h), 1};
        VkFilter filter = (r.w == size.w && r.h == size.h) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        vkCmdBlitImage(cb, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &blit, filter);
    }
    // Test hook: copy the finished window image to a host buffer.
    VkBuffer cap_buf = VK_NULL_HANDLE;
    VkDeviceMemory cap_mem = VK_NULL_HANDLE;
    VkImageLayout last = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (!capture_path_.empty() && can_capture_) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = VkDeviceSize(extent_.width) * extent_.height * 4;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        NITID_VK(vkCreateBuffer(vk_.device, &bci, nullptr, &cap_buf));
        VkMemoryRequirements r;
        vkGetBufferMemoryRequirements(vk_.device, cap_buf, &r);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = r.size;
        mai.memoryTypeIndex = vk_.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        NITID_VK(vkAllocateMemory(vk_.device, &mai, nullptr, &cap_mem));
        NITID_VK(vkBindBufferMemory(vk_.device, cap_buf, cap_mem, 0));
        barrier(cb, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, T, T);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cb, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cap_buf, 1, &c);
        last = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    barrier(cb, target, last, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0, T,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    NITID_VK(vkEndCommandBuffer(cb));

    VkSemaphore waits[2] = {s.acquired, has_frame ? s.ready->vk() : VK_NULL_HANDLE};
    VkPipelineStageFlags stages[2] = {T, T};
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = has_frame ? 2 : 1;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = stages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &rendered_[idx];
    NITID_VK(vkResetFences(vk_.device, 1, &s.fence));
    NITID_VK(vkQueueSubmit(vk_.queue, 1, &si, s.fence));
    if (cap_buf) {
        NITID_VK(vkWaitForFences(vk_.device, 1, &s.fence, VK_TRUE, UINT64_MAX));
        save_capture(cap_buf, cap_mem);
        vkDestroyBuffer(vk_.device, cap_buf, nullptr);
        vkFreeMemory(vk_.device, cap_mem, nullptr);
        capture_path_.clear();
    }

    ++present_id_;
    VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    pid.swapchainCount = 1;
    pid.pPresentIds = &present_id_;
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, wait_for_present_ ? &pid : nullptr};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered_[idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &idx;
    VkResult pr = vkQueuePresentKHR(vk_.queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) dirty_ = true;
    else if (pr != VK_SUCCESS) failf("Vulkan: present failed ({})", int(pr));
    ++frame_;

    // Keep kQueueAhead presents queued: wait until the one before them is on screen (spike S7).
    double shown = now_s();
    if (wait_for_present_ && present_id_ > kQueueAhead) {
        VkResult wr = wait_for_present_(vk_.device, swapchain_, present_id_ - kQueueAhead, 200'000'000ull);
        shown = now_s();
        // Ignore the first half second after the swapchain is (re)made: the compositor is still settling.
        bool settling = present_id_ < settle_until_;
        if (wr == VK_SUCCESS && !settling && last_shown_s_ > 0 && shown - last_shown_s_ > 1.5 * period_s_) ++missed_;
    }
    last_shown_s_ = shown;
    if (fps_window_start_ < 0) fps_window_start_ = shown;
    ++fps_count_;
    if (shown - fps_window_start_ >= 1.0) {
        measured_fps_ = fps_count_ / (shown - fps_window_start_);
        fps_window_start_ = shown;
        fps_count_ = 0;
    }
    return copy_ms;
}

} // namespace nitid::output
