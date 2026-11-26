#pragma once

#include <nitid/export.hpp>
#include <nitid/settings.hpp>
#include <nitid/source.hpp>
#include <nitid/stats.hpp>
#include <nitid/window.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nitid {

struct EngineOptions {
    // Vulkan instance extensions the app's window system needs (e.g. from SDL_Vulkan_GetInstanceExtensions).
    // Leave empty for headless use.
    std::vector<std::string> instance_extensions;
    bool validation = false;     // enable VK_LAYER_KHRONOS_validation if installed
    // Folder with the NVIDIA VFX runtime (libVideoFX.so and friends). Empty: $NITID_VFX_DIR, then the
    // folder configured at build time. If none works, AI effects are reported as unavailable.
    std::string vfx_runtime_dir;
};

// What this machine can do. Filled in by Engine::create().
struct Capabilities {
    std::string gpu_name;
    std::string driver_version;
    std::string vfx_version;     // empty if the VFX runtime was not found
    std::string vfx_error;       // why VFX is unavailable, if it is
    bool super_res = false;      // VFX Video Super Resolution can be used
    bool frame_gen = false;      // VFX Video Frame Generation can be used (needs RTX 40/50)
};

// The Nitid engine: input -> AI upscaling -> AI frame generation -> window or CPU buffer.
//
// One engine plays one source at a time. Not thread-safe: call it from one thread (the app's
// render loop). Errors are thrown as nitid::Error.
class NITID_EXPORT Engine {
public:
    static std::unique_ptr<Engine> create(const EngineOptions& options = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    const Capabilities& capabilities() const noexcept;

    // --- Input -------------------------------------------------------------------------------
    SourceInfo open_file(const std::string& path, const OpenOptions& options = {});
    void close_source();
    bool has_source() const noexcept;

    // --- Settings and stats --------------------------------------------------------------------
    void set_settings(const Settings& settings);
    const Settings& settings() const noexcept;
    Stats stats() const;
    void reset_stats();

    void set_paused(bool paused);
    bool paused() const noexcept;

    // Jumps to `seconds` (clamped to the source; frame-exact). Playback continues from there (or stays paused,
    // showing that frame). Takes from a few ms to ~0.3 s (decoding from the keyframe before the target).
    void seek(double seconds);
    double position() const noexcept;  // seconds

    // Sound (window output). Volume is a linear gain (0..1, more amplifies); delay moves the sound against the
    // picture (+ = later), for speakers / headphones whose real delay differs from what they report.
    void set_volume(float gain);
    void set_muted(bool muted);
    void set_audio_delay(double seconds);
    float volume() const noexcept;
    bool muted() const noexcept;
    double audio_delay() const noexcept;

    // --- Window output -------------------------------------------------------------------------
    // Shows frames in the app's window, paced to its refresh rate (Vulkan FIFO).
    void attach_window(const WindowTarget& target);
    void resize_window(std::uint32_t width, std::uint32_t height);
    void set_refresh_rate(double hz);
    // Renders and presents one display refresh. Blocks until the display can take a new frame.
    // Returns false when the source has ended (and is not looping); the last frame stays on screen.
    bool render_frame();
    // Compare mode (Settings::compare): where the window splits original | Nitid, 0..1 of the picture's width.
    void set_compare_split(float split);
    float compare_split() const noexcept;
    // Overlay: the app draws on top of the video (on-screen controls). nullptr = none.
    void set_overlay(OverlayCallback overlay);
    VulkanDevice vulkan_device() const;
    // Test hook: saves the next window image (exactly what is presented) as a PPM file.
    void capture_next_window_frame(const std::string& ppm_path);

    // --- Headless output -----------------------------------------------------------------------
    // Produces frames at source fps x multiplier (or source fps with frame gen off), as fast as possible.
    // width/height 0 = source size.
    void set_headless_output(std::uint32_t width, std::uint32_t height);
    std::uint32_t headless_width() const noexcept;
    std::uint32_t headless_height() const noexcept;
    double headless_fps() const noexcept;
    // Produces the next output frame. If rgba is not empty (width*height*4 bytes), the frame is copied
    // into it. Returns false at the end of the source.
    bool render_headless(std::span<std::uint8_t> rgba = {});

private:
    Engine();
    bool end_of_source();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nitid
