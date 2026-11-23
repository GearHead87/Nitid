// Simple Vulkan image / buffer helpers for the spikes (upload, download, clear).
#pragma once

#include "vk_ctx.hpp"

#include <cstring>

namespace spike {

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void* map = nullptr;
    VkDeviceSize size = 0;
};

inline Buffer make_host_buffer(VkCtx& vk, VkDeviceSize size)
{
    Buffer b;
    b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateBuffer(vk.device, &ci, nullptr, &b.buf));
    VkMemoryRequirements r;
    vkGetBufferMemoryRequirements(vk.device, b.buf, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(vk.device, &ai, nullptr, &b.mem));
    VK_CHECK(vkBindBufferMemory(vk.device, b.buf, b.mem, 0));
    VK_CHECK(vkMapMemory(vk.device, b.mem, 0, size, 0, &b.map));
    return b;
}

inline void destroy(VkCtx& vk, Buffer& b)
{
    if (b.mem) vkFreeMemory(vk.device, b.mem, nullptr);
    if (b.buf) vkDestroyBuffer(vk.device, b.buf, nullptr);
    b = {};
}

struct Image {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

inline Image make_image(VkCtx& vk, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags extra = 0)
{
    Image im;
    im.fmt = fmt;
    im.w = w;
    im.h = h;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | extra;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vk.device, &ci, nullptr, &im.img));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(vk.device, im.img, &r);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(vk.device, &ai, nullptr, &im.mem));
    VK_CHECK(vkBindImageMemory(vk.device, im.img, im.mem, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(vk.device, &vi, nullptr, &im.view));
    return im;
}

inline void destroy(VkCtx& vk, Image& im)
{
    if (im.view) vkDestroyImageView(vk.device, im.view, nullptr);
    if (im.img) vkDestroyImage(vk.device, im.img, nullptr);
    if (im.mem) vkFreeMemory(vk.device, im.mem, nullptr);
    im = {};
}

// Full barrier to a new layout (spike quality: ALL_COMMANDS both sides).
inline void to_layout(VkCommandBuffer cb, Image& im, VkImageLayout nl)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = im.layout;
    b.newLayout = nl;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im.img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);
    im.layout = nl;
}

inline void cmd_upload(VkCommandBuffer cb, Buffer& staging, Image& im)
{
    to_layout(cb, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {im.w, im.h, 1};
    vkCmdCopyBufferToImage(cb, staging.buf, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    to_layout(cb, im, VK_IMAGE_LAYOUT_GENERAL);
}

inline void cmd_download(VkCommandBuffer cb, Image& im, Buffer& staging)
{
    to_layout(cb, im, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {im.w, im.h, 1};
    vkCmdCopyImageToBuffer(cb, im.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buf, 1, &c);
    to_layout(cb, im, VK_IMAGE_LAYOUT_GENERAL);
}

inline void cmd_clear(VkCommandBuffer cb, Image& im, float r, float g = 0, float b = 0, float a = 0)
{
    to_layout(cb, im, VK_IMAGE_LAYOUT_GENERAL);
    VkClearColorValue v{};
    v.float32[0] = r;
    v.float32[1] = g;
    v.float32[2] = b;
    v.float32[3] = a;
    VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, im.img, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &rng);
}

// GPU timestamps around a region.
struct Timer {
    VkQueryPool pool = VK_NULL_HANDLE;
    float period_ns = 1;
    void init(VkCtx& vk)
    {
        VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        ci.queryCount = 2;
        VK_CHECK(vkCreateQueryPool(vk.device, &ci, nullptr, &pool));
        period_ns = vk.props.limits.timestampPeriod;
    }
    void begin(VkCommandBuffer cb)
    {
        vkCmdResetQueryPool(cb, pool, 0, 2);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
    }
    void end(VkCommandBuffer cb) { vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 1); }
    double ms(VkCtx& vk)
    {
        uint64_t t[2];
        VK_CHECK(vkGetQueryPoolResults(vk.device, pool, 0, 2, sizeof t, t, sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        return double(t[1] - t[0]) * period_ns / 1e6;
    }
    void destroy(VkCtx& vk) { vkDestroyQueryPool(vk.device, pool, nullptr); }
};

}  // namespace spike
