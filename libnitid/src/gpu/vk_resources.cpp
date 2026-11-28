#include "gpu/vk_resources.hpp"

#include "gpu/vk_check.hpp"

#include <utility>

namespace nitid::gpu {

Image::Image(VulkanContext& v, uint32_t width, uint32_t height, VkFormat fmt, VkImageUsageFlags usage,
             const std::vector<uint32_t>& families, const void* format_info)
    : vk(&v), format(fmt), w(width), h(height)
{
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, format_info};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (families.size() > 1) {
        ci.sharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = uint32_t(families.size());
        ci.pQueueFamilyIndices = families.data();
    }
    NITID_VK(vkCreateImage(v.device, &ci, nullptr, &image));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(v.device, image, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = v.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    NITID_VK(vkAllocateMemory(v.device, &ai, nullptr, &mem));
    NITID_VK(vkBindImageMemory(v.device, image, mem, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NITID_VK(vkCreateImageView(v.device, &vi, nullptr, &view));
}

Image& Image::operator=(Image&& o) noexcept
{
    if (this == &o) return *this;
    this->~Image();
    vk = o.vk, image = o.image, view = o.view, mem = o.mem, format = o.format, w = o.w, h = o.h, layout = o.layout;
    o.image = VK_NULL_HANDLE, o.view = VK_NULL_HANDLE, o.mem = VK_NULL_HANDLE;
    return *this;
}

Image::~Image()
{
    if (!vk) return;
    if (view) vkDestroyImageView(vk->device, view, nullptr);
    if (image) vkDestroyImage(vk->device, image, nullptr);
    if (mem) vkFreeMemory(vk->device, mem, nullptr);
    view = VK_NULL_HANDLE, image = VK_NULL_HANDLE, mem = VK_NULL_HANDLE;
}

void transition(VkCommandBuffer cb, Image& im, VkImageLayout to)
{
    if (im.layout == to) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = im.layout;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    im.layout = to;
}

void full_barrier(VkCommandBuffer cb)
{
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0,
                         nullptr, 0, nullptr);
}

void copy_buffer_to_image(VkCommandBuffer cb, VkBuffer src, Image& dst)
{
    transition(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageExtent = {dst.w, dst.h, 1};
    vkCmdCopyBufferToImage(cb, src, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
}

void copy_image_to_buffer(VkCommandBuffer cb, Image& src, VkBuffer dst)
{
    transition(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageExtent = {src.w, src.h, 1};
    vkCmdCopyImageToBuffer(cb, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &r);
}

void clear(VkCommandBuffer cb, Image& im, float value)
{
    transition(cb, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue c{};
    c.float32[0] = c.float32[1] = c.float32[2] = c.float32[3] = value;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &range);
}

CommandRunner::CommandRunner(VulkanContext& vk, uint32_t family) : vk_(vk)
{
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    NITID_VK(vkCreateCommandPool(vk.device, &pci, nullptr, &pool_));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    NITID_VK(vkAllocateCommandBuffers(vk.device, &ai, &cb_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    NITID_VK(vkCreateFence(vk.device, &fci, nullptr, &fence_));
}

CommandRunner::~CommandRunner()
{
    if (pending_) vkWaitForFences(vk_.device, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkDestroyFence(vk_.device, fence_, nullptr);
    vkDestroyCommandPool(vk_.device, pool_, nullptr);
}

void CommandRunner::wait()
{
    if (!pending_) return;
    NITID_VK(vkWaitForFences(vk_.device, 1, &fence_, VK_TRUE, UINT64_MAX));
    NITID_VK(vkResetFences(vk_.device, 1, &fence_));
    pending_ = false;
}

VkCommandBuffer CommandRunner::begin()
{
    wait();
    NITID_VK(vkResetCommandBuffer(cb_, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NITID_VK(vkBeginCommandBuffer(cb_, &bi));
    return cb_;
}

void CommandRunner::submit(VkQueue queue, bool wait_done, VkSemaphore wait_sem, VkSemaphore signal_sem)
{
    NITID_VK(vkEndCommandBuffer(cb_));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb_;
    if (wait_sem) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &wait_sem;
        si.pWaitDstStageMask = &stage;
    }
    if (signal_sem) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &signal_sem;
    }
    NITID_VK(vkQueueSubmit(queue, 1, &si, fence_));
    pending_ = true;
    if (wait_done) wait();
}

Semaphore::Semaphore(VulkanContext& vk) : vk_(vk)
{
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    NITID_VK(vkCreateSemaphore(vk.device, &ci, nullptr, &sem_));
}

Semaphore::~Semaphore()
{
    if (sem_) vkDestroySemaphore(vk_.device, sem_, nullptr);
}

} // namespace nitid::gpu
