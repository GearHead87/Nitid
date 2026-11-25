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

} // namespace nitid
