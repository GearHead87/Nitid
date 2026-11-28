#pragma once

// Vulkan instance + NVIDIA device + one graphics/compute queue. Picks the NVIDIA GPU explicitly
// (machines with an iGPU list it too; see spike M2).
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nitid::gpu {

class VulkanContext {
public:
    VulkanContext(const std::vector<std::string>& instance_extensions, bool validation);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Creates the device. Call once, after any surface exists (so present support can be checked).
    // `optional_extensions`: enabled when all of them are available (DLSS needs a set of them; see ngx_extensions).
    void create_device(VkSurfaceKHR surface_or_null, const std::vector<std::string>& optional_extensions = {});
    bool has_device() const { return device != VK_NULL_HANDLE; }

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) const;
    std::string driver_version() const;

    // One-shot command buffer helpers for setup work (not for the per-frame path).
    VkCommandBuffer begin_once();
    void submit_once(VkCommandBuffer cb);

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;           // render thread: presenting, overlay
    VkQueue capture_queue = VK_NULL_HANDLE;   // capture thread: DMA-BUF copies (same family; = queue if only one)
    VkQueue work_queue = VK_NULL_HANDLE;      // pipeline thread: DLSS, optical flow copies (same family; may be null)
    uint32_t queue_family = 0;
    VkQueue flow_queue = VK_NULL_HANDLE;      // VK_NV_optical_flow (its own queue family on NVIDIA)
    uint32_t flow_family = UINT32_MAX;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceMemoryProperties mem{};
    std::array<uint8_t, VK_UUID_SIZE> device_uuid{};

    // Optional features, enabled when available.
    bool present_wait = false;  // VK_KHR_present_id + VK_KHR_present_wait
    bool swapchain = false;
    bool dma_buf_import = false;  // VK_EXT_external_memory_dma_buf + image_drm_format_modifier + queue_family_foreign
    bool optional_extensions = false;  // the optional set passed to create_device() is enabled
    bool optical_flow = false;    // VK_NV_optical_flow with a queue for it

private:
    std::vector<std::string> available_device_extensions() const;
};

} // namespace nitid::gpu
