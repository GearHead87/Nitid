#include "output/swapchain_presenter.hpp"

#include "core/log.hpp"
#include "core/running_average.hpp"
#include "gpu/vk_check.hpp"

#include <algorithm>
#include <cmath>
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
    int least = 0, greatest = 0;
    NITID_CU(cuCtxGetStreamPriorityRange(&least, &greatest));
    NITID_CU(cuStreamCreateWithPriority(&stream_, CU_STREAM_NON_BLOCKING, greatest));
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
    cuStreamSynchronize(stream_);
    destroy_swapchain();
    for (auto& s : slots_) {
        free_image(s.img);
        free_image(s.orig);
        s.ready.reset();
        vkDestroySemaphore(vk_.device, s.acquired, nullptr);
        vkDestroyFence(vk_.device, s.fence, nullptr);
        vkFreeCommandBuffers(vk_.device, vk_.pool, 1, &s.cmd);
    }
    cuStreamDestroy(stream_);
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
    can_overlay_ = caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | (can_capture_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0) |
                    (can_overlay_ ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0);
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
    if (can_overlay_) {
        views_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = images_[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = format_;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            NITID_VK(vkCreateImageView(vk_.device, &vci, nullptr, &views_[i]));
        }
    }
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
    for (auto v : views_) vkDestroyImageView(vk_.device, v, nullptr);
    views_.clear();
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

void SwapchainPresenter::free_image(Img& i)
{
    if (i.image) vkDestroyImage(vk_.device, i.image, nullptr);
    if (i.memory) vkFreeMemory(vk_.device, i.memory, nullptr);
    i.image = VK_NULL_HANDLE;
    i.memory = VK_NULL_HANDLE;
    i.buffer.reset();
    i.size = {};
}

void SwapchainPresenter::ensure_image(Img& i, pipeline::Size size)
{
    if (i.size == size && i.image) return;
    free_image(i);
    i.buffer = std::make_unique<gpu::SharedBuffer>(vk_, VkDeviceSize(size.w) * size.h * 4);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {size.w, size.h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    NITID_VK(vkCreateImage(vk_.device, &ci, nullptr, &i.image));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(vk_.device, i.image, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk_.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    NITID_VK(vkAllocateMemory(vk_.device, &ai, nullptr, &i.memory));
    NITID_VK(vkBindImageMemory(vk_.device, i.image, i.memory, 0));
    i.size = size;
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
    return present(frame, size, 0, {}, 0.5f);
}

double SwapchainPresenter::present(CUdeviceptr frame, pipeline::Size size, CUdeviceptr orig, pipeline::Size orig_size,
                                   float split)
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
    const bool has_orig = has_frame && orig && orig_size.w && orig_size.h;
    if (has_frame) {
        ensure_image(s.img, size);
        if (has_orig) ensure_image(s.orig, orig_size);
        auto t0 = SteadyClock::now();
        NITID_CU(cuMemcpyDtoDAsync(s.img.buffer->ptr(), frame, size_t(size.w) * size.h * 4, stream_));
        if (has_orig) NITID_CU(cuMemcpyDtoDAsync(s.orig.buffer->ptr(), orig, size_t(orig_size.w) * orig_size.h * 4, stream_));
        s.ready->cuda_signal(stream_);
        NITID_CU(cuStreamSynchronize(stream_));
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
        auto upload = [&](Img& im) {
            barrier(cb, im.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, T, T);
            VkBufferImageCopy c{};
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.imageExtent = {im.size.w, im.size.h, 1};
            vkCmdCopyBufferToImage(cb, im.buffer->buffer(), im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            barrier(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, T, T);
        };
        upload(s.img);
        if (has_orig) upload(s.orig);
        barrier(cb, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, T, T);  // clear before blit
        pipeline::Size out{extent_.width, extent_.height};
        pipeline::Rect r = pipeline::centre(pipeline::fit(size, out), out);
        video_rect_ = r;
        // Blit the part [u0, u1) (fractions of the width) of an image onto the same part of the picture rectangle.
        auto blit_part = [&](const Img& im, float u0, float u1) {
            const int32_t sx0 = int32_t(std::lround(u0 * im.size.w)), sx1 = int32_t(std::lround(u1 * im.size.w));
            const int32_t dx0 = r.x + int32_t(std::lround(u0 * r.w)), dx1 = r.x + int32_t(std::lround(u1 * r.w));
            if (sx1 <= sx0 || dx1 <= dx0) return;
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[0] = {sx0, 0, 0};
            blit.srcOffsets[1] = {sx1, int32_t(im.size.h), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[0] = {dx0, r.y, 0};
            blit.dstOffsets[1] = {dx1, r.y + int32_t(r.h), 1};
            VkFilter filter = (r.w == im.size.w && r.h == im.size.h) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            vkCmdBlitImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, filter);
        };
        if (has_orig) {
            const float u = std::clamp(split, 0.0f, 1.0f);
            blit_part(s.orig, 0.0f, u);  // the original, plainly scaled (what a normal player shows)
            blit_part(s.img, u, 1.0f);   // Nitid's picture
        } else {
            blit_part(s.img, 0.0f, 1.0f);
        }
    }
    VkImageLayout last = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkAccessFlags last_access = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkPipelineStageFlags last_stage = T;
    // The app's overlay, drawn on top of the video with dynamic rendering.
    if (overlay_ && can_overlay_) {
        const auto C = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier(cb, target, last, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, T, C);
        VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = views_[idx];
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, extent_};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &att;
        vkCmdBeginRendering(cb, &ri);
        overlay_(OverlayFrame{cb, extent_.width, extent_.height});
        vkCmdEndRendering(cb);
        last = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        last_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        last_stage = C;
    }
    // Test hook: copy the finished window image to a host buffer.
    VkBuffer cap_buf = VK_NULL_HANDLE;
    VkDeviceMemory cap_mem = VK_NULL_HANDLE;
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
        barrier(cb, target, last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, last_access, VK_ACCESS_TRANSFER_READ_BIT, last_stage, T);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cb, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cap_buf, 1, &c);
        last = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        last_access = VK_ACCESS_TRANSFER_READ_BIT;
        last_stage = T;
    }
    barrier(cb, target, last, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, last_access, 0, last_stage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    NITID_VK(vkEndCommandBuffer(cb));

    VkSemaphore waits[2] = {s.acquired, has_frame ? s.ready->vk() : VK_NULL_HANDLE};
    VkPipelineStageFlags stages[2] = {T | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, T};
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
    submit_s_[present_id_ % submit_s_.size()] = now_s();
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
        if (wr == VK_SUCCESS) {
            double lat = shown - submit_s_[(present_id_ - kQueueAhead) % submit_s_.size()];
            if (lat > 0 && lat < 0.2) latency_ema_s_ = latency_ema_s_ < 0 ? lat : latency_ema_s_ + (lat - latency_ema_s_) * 0.05;
        }
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

double SwapchainPresenter::display_latency_s() const
{
    if (latency_ema_s_ < 0) return double(kQueueAhead + 1) * period_s_;
    return latency_ema_s_ + 0.5 * period_s_;
}

} // namespace nitid::output
