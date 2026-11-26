#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace nitid {

// How the engine shows frames in an app's window. The app owns the window (SDL, Qt, ...); the engine
// owns the Vulkan instance, so it asks the app to create the surface for it.
struct WindowTarget {
    std::function<VkSurfaceKHR(VkInstance)> create_surface;  // return VK_NULL_HANDLE on failure
    std::uint32_t width = 0;   // drawable size in pixels
    std::uint32_t height = 0;
    double refresh_hz = 60;    // refresh rate of the display the window is on (used for pacing)
};

// The engine's Vulkan objects, for an app that draws its own overlay (e.g. Dear ImGui's Vulkan backend).
// Valid after Engine::attach_window(). The queue is the one the engine presents on; use it from the render
// thread only.
struct VulkanDevice {
    std::uint32_t api_version = 0;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkFormat window_format = VK_FORMAT_UNDEFINED;  // swapchain image format (for the overlay's pipeline)
    std::uint32_t window_image_count = 0;
};

// Called while recording each window frame, after the video is drawn, inside vkCmdBeginRendering on the window
// image (dynamic rendering, load = keep the video, one colour attachment of VulkanDevice::window_format).
struct OverlayFrame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::uint32_t width = 0, height = 0;
};
using OverlayCallback = std::function<void(const OverlayFrame&)>;

} // namespace nitid
