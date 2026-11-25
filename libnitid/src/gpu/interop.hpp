#pragma once

// Memory and semaphores shared between Vulkan and CUDA (opaque file descriptors), proven in spike S5.
#include "gpu/cuda_context.hpp"
#include "gpu/vulkan_context.hpp"

namespace nitid::gpu {

// A Vulkan buffer that CUDA can read and write through ptr().
class SharedBuffer {
public:
    SharedBuffer(VulkanContext& vk, VkDeviceSize size);
    ~SharedBuffer();
    SharedBuffer(const SharedBuffer&) = delete;
    SharedBuffer& operator=(const SharedBuffer&) = delete;

    VkBuffer buffer() const { return buf_; }
    CUdeviceptr ptr() const { return ptr_; }
    VkDeviceSize size() const { return size_; }

private:
    VulkanContext& vk_;
    VkBuffer buf_ = VK_NULL_HANDLE;
    VkDeviceMemory mem_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    CUexternalMemory ext_ = nullptr;
    CUdeviceptr ptr_ = 0;
};

// A binary semaphore CUDA signals and Vulkan waits on (or the other way round).
class SharedSemaphore {
public:
    explicit SharedSemaphore(VulkanContext& vk);
    ~SharedSemaphore();
    SharedSemaphore(const SharedSemaphore&) = delete;
    SharedSemaphore& operator=(const SharedSemaphore&) = delete;

    VkSemaphore vk() const { return sem_; }
    void cuda_signal(CUstream s);
    void cuda_wait(CUstream s);

private:
    VulkanContext& vk_;
    VkSemaphore sem_ = VK_NULL_HANDLE;
    CUexternalSemaphore ext_ = nullptr;
};

} // namespace nitid::gpu
