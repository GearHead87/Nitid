// S7: How evenly can we put frames on screen (KDE Wayland, NVIDIA)?
//
// 1. Surface capabilities: present modes, VK_EXT_present_timing support (absolute / relative time).
// 2. Measures the interval between *displayed* frames using VK_KHR_present_id + VK_KHR_present_wait
//    (vkWaitForPresentKHR returns when a present has reached the display), for each present mode.
//    One frame is kept queued ahead so the GPU/CPU never starve the display.
// Window only (no fullscreen), slowly changing low-contrast colour (no flashing), ~4 s per mode.
//
// Usage: s7-present-timing [seconds_per_mode=4]

#include "vk_ctx.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace spike;
using clk = std::chrono::steady_clock;

static const char* mode_name(VkPresentModeKHR m)
{
    switch (m) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: return "FIFO_LATEST_READY";
        default: return "other";
    }
}

int main(int argc, char** argv)
{
    double secs = argc > 1 ? std::stod(argv[1]) : 4.0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(disp);
    double hz = dm ? double(dm->refresh_rate) : 0;
    std::printf("display: %dx%d @ %.2f Hz\n", dm ? dm->w : 0, dm ? dm->h : 0, hz);
    SDL_Window* win = SDL_CreateWindow("Nitid S7 frame-timing test (closes by itself)", 960, 540, SDL_WINDOW_VULKAN);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    Uint32 n_ext = 0;
    const char* const* sdl_ext = SDL_Vulkan_GetInstanceExtensions(&n_ext);
    std::vector<std::string> iexts(sdl_ext, sdl_ext + n_ext);
    iexts.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    VkCtx vk;
    vk.create_instance(iexts);
    vk.pick_nvidia();
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(win, vk.instance, nullptr, &surface)) {
        std::fprintf(stderr, "SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        return 1;
    }

    // Surface capabilities incl. present timing.
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys, surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys, surface, &n, modes.data());
    std::printf("present modes:");
    for (auto m : modes) std::printf(" %s", mode_name(m));
    std::printf("\n");
    VkPresentTimingSurfaceCapabilitiesEXT ptc{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
    VkSurfaceCapabilities2KHR sc2{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, &ptc};
    VkPhysicalDeviceSurfaceInfo2KHR si2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
    si2.surface = surface;
    auto getCaps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
        vkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    VkResult cr = getCaps2(vk.phys, &si2, &sc2);
    std::printf("present timing (surface): query=%d supported=%u absolute=%u relative=%u stage_queries=0x%x\n", cr,
                ptc.presentTimingSupported, ptc.presentAtAbsoluteTimeSupported, ptc.presentAtRelativeTimeSupported,
                ptc.presentStageQueries);

    VkPhysicalDevicePresentWaitFeaturesKHR pw{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDevicePresentIdFeaturesKHR pid{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &pw};
    vk.create_device({VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME, VK_KHR_PRESENT_WAIT_EXTENSION_NAME}, &pid);
    VkBool32 can_present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(vk.phys, vk.queue_family, surface, &can_present);
    std::printf("presentId=%u presentWait=%u, queue family %u can present: %u\n", pid.presentId, pw.presentWait,
                vk.queue_family, can_present);
    auto waitForPresent = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(vk.device, "vkWaitForPresentKHR"));

    std::printf("\n| Present mode | Frames | Mean interval (ms) | Std dev (ms) | p1 / p99 (ms) | > 1.5x refresh | Effective Hz |\n|---|---|---|---|---|---|---|\n");
    const double frame_ms = hz > 0 ? 1000.0 / hz : 0;
    VkSwapchainKHR old = VK_NULL_HANDLE;
    for (VkPresentModeKHR mode : {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_LATEST_READY_KHR,
                                  VK_PRESENT_MODE_IMMEDIATE_KHR}) {
        if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
            std::printf("| %s | not offered | | | | | |\n", mode_name(mode));
            continue;
        }
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys, surface, &caps);
        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = std::max(3u, caps.minImageCount);
        sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        sci.imageExtent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{960, 540};
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = mode;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = old;
        VkSwapchainKHR sw;
        VK_CHECK(vkCreateSwapchainKHR(vk.device, &sci, nullptr, &sw));
        if (old) vkDestroySwapchainKHR(vk.device, old, nullptr);
        old = sw;
        uint32_t ni = 0;
        vkGetSwapchainImagesKHR(vk.device, sw, &ni, nullptr);
        std::vector<VkImage> imgs(ni);
        vkGetSwapchainImagesKHR(vk.device, sw, &ni, imgs.data());

        const int F = 3;  // frames in flight (CPU side)
        VkSemaphore acq[F], done[8];
        VkFence fences[F];
        VkCommandBuffer cbs[F];
        VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = vk.pool;
        cai.commandBufferCount = F;
        VK_CHECK(vkAllocateCommandBuffers(vk.device, &cai, cbs));
        for (int i = 0; i < F; ++i) {
            VK_CHECK(vkCreateSemaphore(vk.device, &sci2, nullptr, &acq[i]));
            VK_CHECK(vkCreateFence(vk.device, &fci, nullptr, &fences[i]));
        }
        for (uint32_t i = 0; i < ni && i < 8; ++i) VK_CHECK(vkCreateSemaphore(vk.device, &sci2, nullptr, &done[i]));

        std::vector<double> done_t;
        auto t_start = clk::now();
        uint64_t id = 0;
        for (int frame = 0;; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            double el = std::chrono::duration<double>(clk::now() - t_start).count();
            if (el > secs) break;
            int f = frame % F;
            VK_CHECK(vkWaitForFences(vk.device, 1, &fences[f], VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(vk.device, 1, &fences[f]));
            uint32_t idx;
            VkResult ar = vkAcquireNextImageKHR(vk.device, sw, UINT64_MAX, acq[f], VK_NULL_HANDLE, &idx);
            if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) break;
            VkCommandBuffer cb = cbs[f];
            vkResetCommandBuffer(cb, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkBeginCommandBuffer(cb, &bi);
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = imgs[idx];
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            float ph = float(el / 4.0 * 2 * M_PI);  // slow, low-contrast colour drift
            VkClearColorValue col{{0.30f + 0.05f * std::sin(ph), 0.30f, 0.32f + 0.05f * std::cos(ph), 1}};
            vkCmdClearColorImage(cb, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &b.subresourceRange);
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            vkEndCommandBuffer(cb);
            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &acq[f];
            si.pWaitDstStageMask = &ws;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cb;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &done[idx];
            VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, fences[f]));
            ++id;
            VkPresentIdKHR pidi{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
            pidi.swapchainCount = 1;
            pidi.pPresentIds = &id;
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, &pidi};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &done[idx];
            pi.swapchainCount = 1;
            pi.pSwapchains = &sw;
            pi.pImageIndices = &idx;
            VkResult pr = vkQueuePresentKHR(vk.queue, &pi);
            if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) break;
            // Keep one present queued ahead: wait for the previous one to reach the display.
            if (id > 1) {
                VkResult wr = waitForPresent(vk.device, sw, id - 1, 1'000'000'000ull);
                if (wr == VK_SUCCESS) done_t.push_back(std::chrono::duration<double, std::milli>(clk::now() - t_start).count());
            }
        }
        vkDeviceWaitIdle(vk.device);
        std::vector<double> iv;
        for (size_t i = 30; i < done_t.size(); ++i) iv.push_back(done_t[i] - done_t[i - 1]);  // skip warm-up
        if (iv.size() > 10) {
            double m = 0, s2 = 0;
            for (double x : iv) m += x;
            m /= double(iv.size());
            for (double x : iv) s2 += (x - m) * (x - m);
            auto sorted = iv;
            std::sort(sorted.begin(), sorted.end());
            size_t miss = std::count_if(iv.begin(), iv.end(), [&](double x) { return frame_ms > 0 && x > 1.5 * frame_ms; });
            std::printf("| %s | %zu | %.3f | %.3f | %.2f / %.2f | %zu | %.1f |\n", mode_name(mode), iv.size(), m,
                        std::sqrt(s2 / double(iv.size())), sorted[sorted.size() / 100], sorted[sorted.size() * 99 / 100],
                        miss, 1000.0 / m);
        } else {
            std::printf("| %s | %zu | (too few completed presents) | | | | |\n", mode_name(mode), iv.size());
        }
        for (int i = 0; i < F; ++i) {
            vkDestroySemaphore(vk.device, acq[i], nullptr);
            vkDestroyFence(vk.device, fences[i], nullptr);
        }
        for (uint32_t i = 0; i < ni && i < 8; ++i) vkDestroySemaphore(vk.device, done[i], nullptr);
        vkFreeCommandBuffers(vk.device, vk.pool, F, cbs);
    }
    if (old) vkDestroySwapchainKHR(vk.device, old, nullptr);
    std::printf("\nrefresh interval: %.3f ms\n", frame_ms);
    vkDestroySurfaceKHR(vk.instance, surface, nullptr);
    vk.destroy();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
