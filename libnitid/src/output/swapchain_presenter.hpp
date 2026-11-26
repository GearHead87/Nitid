#pragma once

// Shows pipeline frames in a window: CUDA writes the frame into a buffer shared with Vulkan, Vulkan copies
// it into an image and blits it (scaled, letterboxed) onto the swapchain image. FIFO present mode, one
// present queued ahead, VK_KHR_present_wait for "when did it reach the screen" (spike S7).
#include "gpu/interop.hpp"
#include "pipeline/frame_layout.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nitid::output {

class SwapchainPresenter {
public:
    SwapchainPresenter(gpu::VulkanContext& vk, gpu::CudaContext& cuda, VkSurfaceKHR surface, pipeline::Size window);
    ~SwapchainPresenter();
    SwapchainPresenter(const SwapchainPresenter&) = delete;
    SwapchainPresenter& operator=(const SwapchainPresenter&) = delete;

    void resize(pipeline::Size window);
    pipeline::Size window_size() const { return window_; }

    // Presents one frame. `frame` is an RGBA8 GPU buffer of `size` (or 0 for a black frame).
    // Blocks until the display can take another frame. Returns the time in ms spent on the output copy.
    double present(CUdeviceptr frame, pipeline::Size size);

    // Test hook: saves the next presented window image (exactly what goes to the screen) as a PPM file.
    void capture_next(const std::string& ppm_path) { capture_path_ = ppm_path; }

    uint64_t missed_refreshes() const { return missed_; }
    // How long after present() a frame reaches the screen (measured with present-wait, + half a refresh for
    // scan-out). Used to line the sound up with the picture.
    double display_latency_s() const;
    double measured_fps() const { return measured_fps_; }
    void set_refresh_period(double s) { period_s_ = s; }
    void reset_stats() { missed_ = 0; }

private:
    static constexpr int kFrames = 3;  // frames in flight
    // Presents waiting for the display. 1 (spike S7) locked into 2/3 of 165 Hz at times on KDE Wayland: the
    // "shown" event arrives late, so the next frame was submitted too close to the compositor's deadline.
    // 2 is steady; it adds one refresh of delay (6 ms at 165 Hz).
    static constexpr uint64_t kQueueAhead = 2;

    struct Slot {
        std::unique_ptr<gpu::SharedBuffer> buffer;   // CUDA writes here
        std::unique_ptr<gpu::SharedSemaphore> ready; // CUDA -> Vulkan
        VkImage image = VK_NULL_HANDLE;              // frame as an image (blit source)
        VkDeviceMemory memory = VK_NULL_HANDLE;
        pipeline::Size size;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
    };

    void create_swapchain();
    void save_capture(VkBuffer buf, VkDeviceMemory mem);
    void destroy_swapchain();
    void ensure_slot_size(Slot& s, pipeline::Size size);
    void free_slot_image(Slot& s);

    gpu::VulkanContext& vk_;
    gpu::CudaContext& cuda_;
    VkSurfaceKHR surface_;
    pipeline::Size window_;
    bool dirty_ = false;

    std::string capture_path_;
    bool can_capture_ = false;  // swapchain images allow TRANSFER_SRC

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> rendered_;  // one per swapchain image
    std::array<Slot, kFrames> slots_;
    uint64_t frame_ = 0;
    uint64_t present_id_ = 0;
    PFN_vkWaitForPresentKHR wait_for_present_ = nullptr;

    // Stats
    double period_s_ = 1.0 / 60;
    double last_shown_s_ = -1;
    uint64_t missed_ = 0;
    uint64_t settle_until_ = 0;  // present id before which misses are not counted
    double measured_fps_ = 0;
    std::array<double, 8> submit_s_{};  // when each present id was submitted (id % 8)
    double latency_ema_s_ = -1;
    double fps_window_start_ = -1;
    int fps_count_ = 0;
};

} // namespace nitid::output
