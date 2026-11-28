#pragma once

// Small Vulkan building blocks for the pipeline thread's GPU work (DLSS, optical flow): device images, a reusable
// command buffer with a fence, and the copies between CUDA-shared buffers and images.
//
// Synchronisation is deliberately simple: CUDA work is finished (stream synchronised) before a submission that reads
// its buffers, and a submission is waited for (fence) before CUDA reads what it wrote. Every pipeline stage already
// works this way (Pipeline::timed), and the copies are tiny next to the AI work.
#include "gpu/vulkan_context.hpp"

#include <vector>

namespace nitid::gpu {

class Image {
public:
    Image() = default;
    // `families`: queue families that use the image (more than one = concurrent sharing, no ownership transfers).
    // `format_info`: pNext for the image (e.g. VkOpticalFlowImageFormatInfoNV).
    Image(VulkanContext& vk, uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
          const std::vector<uint32_t>& families, const void* format_info = nullptr);
    ~Image();
    Image(Image&& o) noexcept { *this = std::move(o); }
    Image& operator=(Image&& o) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    explicit operator bool() const { return image != VK_NULL_HANDLE; }

    VulkanContext* vk = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;  // as recorded so far (one user records at a time)
};

// Moves `im` to `to` (full barrier; these are small, infrequent transitions).
void transition(VkCommandBuffer cb, Image& im, VkImageLayout to);
// Every write before -> every read/write after (between copies and NGX work in one command buffer).
void full_barrier(VkCommandBuffer cb);
// Tightly packed buffer <-> image (whole image).
void copy_buffer_to_image(VkCommandBuffer cb, VkBuffer src, Image& dst);
void copy_image_to_buffer(VkCommandBuffer cb, Image& src, VkBuffer dst);
// Fills a colour image with one value (all channels).
void clear(VkCommandBuffer cb, Image& im, float value);

// One reusable primary command buffer + fence, for one thread and one queue family.
class CommandRunner {
public:
    CommandRunner(VulkanContext& vk, uint32_t family);
    ~CommandRunner();
    CommandRunner(const CommandRunner&) = delete;
    CommandRunner& operator=(const CommandRunner&) = delete;

    VkCommandBuffer begin();  // waits for the previous submission first
    // Submits to `queue`; waits for completion unless `wait` is false (then begin() waits).
    void submit(VkQueue queue, bool wait = true, VkSemaphore wait_sem = VK_NULL_HANDLE,
                VkSemaphore signal_sem = VK_NULL_HANDLE);
    void wait();

private:
    VulkanContext& vk_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cb_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    bool pending_ = false;
};

// A plain binary semaphore (between the work queue and the optical flow queue).
class Semaphore {
public:
    explicit Semaphore(VulkanContext& vk);
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    VkSemaphore get() const { return sem_; }

private:
    VulkanContext& vk_;
    VkSemaphore sem_ = VK_NULL_HANDLE;
};

} // namespace nitid::gpu
