# Nitid: Plan

A new **Linux-only** real-time video enhancer. It is not a port of
[Veyra](https://github.com/Likely7/Veyra-NRVideo). It reuses Veyra's approach and some of its
shader logic, and runs on NVIDIA's Linux SDKs.

## 1. Goal

One app that takes **a video file, a capture card, or your screen**, and enhances it live on an RTX GPU:

- AI upscaling (VFX SDK VSR, or DLSS on video the way Veyra does it)
- AI frame generation (VFX SDK, or DLSS Frame Gen up to 6x)
- SDR to HDR
- Shown fullscreen with low lag, and optionally recorded or sent to OBS or Discord

**Target:** Linux (Wayland first, KDE tested), NVIDIA RTX GPUs. Frame generation needs RTX 40 or 50.

## 2. Tech stack

| Layer | Choice | Why |
|---|---|---|
| Language | **C++23** | All NVIDIA SDKs, Vulkan and FFmpeg have C/C++ APIs, and Veyra is C++ too |
| Build | CMake + Ninja | Standard; Veyra uses it too |
| GPU | **Vulkan 1.3+** with volk and VMA (Vulkan Memory Allocator) | Replaces DirectX 12. DLSS has Linux support only through Vulkan. |
| Shaders | HLSL compiled to SPIR-V with **DXC** | Lets us reuse Veyra's HLSL compute shaders (color, optical flow, sharpening) with small changes |
| AI upscaling | **NVIDIA VFX SDK**: VideoSuperRes | Built for video |
| AI frame gen (video) | **NVIDIA VFX SDK**: VideoFrameGeneration | The same SDK Veyra uses for this |
| SDR to HDR | **NVIDIA VFX SDK**: TrueHDR | Replaces RTX Video HDR |
| DLSS | **NVIDIA DLSS SDK 310.9.16**: `libnvidia-ngx-dlss.so`, `libnvidia-ngx-dlssg.so` | Veyra-style DLSS upscaling and Frame Gen on video |
| Motion estimation | **`VK_NV_optical_flow`** (hardware), with a GPU shader fallback ported from Veyra | Motion vectors for DLSS |
| Sharing frames between CUDA and Vulkan | `VK_KHR_external_memory_fd` + `cudaImportExternalMemory` | The VFX SDK runs on CUDA, and this avoids copying frames through the CPU |
| Decoding | **FFmpeg** with NVDEC or Vulkan hardware decoding | Files, network streams, RTSP |
| Capture card | **V4L2**, with frames passed straight into Vulkan (DMA-BUF) | Uses the capture card's own frames without copying them |
| Screen capture | **xdg-desktop-portal + PipeWire** (DMA-BUF) | The standard way to capture the screen on Wayland |
| Audio | **PipeWire** | Low-latency capture-card audio and keeping sound in sync with video |
| Window and input | **SDL3** | Wayland, Vulkan, fullscreen, gamepads |
| UI | **Dear ImGui** drawn over the video | Simple, fast, suited to a live tool. A Qt/QML interface can come later. |
| Recording | FFmpeg with **NVENC** (H.264, HEVC, AV1) | Records the enhanced output |
| Virtual camera | **v4l2loopback** | OBS and Discord can use the enhanced feed |
| Config / logs / CLI | toml++ / spdlog / CLI11 | Presets in TOML |
| License | **GPL-3.0** | Required if we reuse Veyra code |
| Packaging | Arch `PKGBUILD` | The VFX SDK is downloaded during install, like vsr-player does |

## 3. How frames flow

```
 Input                Prepare              Enhance                     Output
┌────────────┐    ┌──────────────┐    ┌──────────────────────┐    ┌──────────────┐
│ File/FFmpeg│    │ YUV → RGB    │    │ Upscale (choose one):│    │ Vulkan window│
│ Capture    │──▶ │ linear color,│──▶ │  VFX VSR / DLSS SR / │──▶ │ even frame   │
│ PipeWire   │    │ HDR, scaling │    │  NIS / FSR1          │    │ timing, HDR, │
└────────────┘    └──────┬───────┘    │ TrueHDR (optional)   │    │ VRR          │
                         │            │ Sharpen / LUT / color│    ├──────────────┤
                         ▼            │ Frame gen (choose):  │    │ NVENC record │
                  ┌──────────────┐    │  VFX VFG / DLSS-G    │    │ Virtual cam  │
                  │ Optical flow │──▶ │  (motion vectors +   │    └──────────────┘
                  │ NVOF / shader│    │   flat depth)        │
                  └──────────────┘    └──────────────────────┘
```

Frames stay on the GPU the whole way. CUDA and Vulkan share memory, so nothing is copied through the CPU.

## 4. Features compared with Veyra

| Veyra (Windows) | Nitid (Linux) | Phase |
|---|---|---|
| Video file playback with audio | ✅ FFmpeg + PipeWire | 1 |
| Capture card | ✅ V4L2, with frames passed straight into Vulkan | 3 |
| Screen capture / game mode | ✅ PipeWire portal | 3 |
| Driver RTX VSR | ➡️ **VFX SDK VSR**, including denoise and deblur modes | 2 |
| RTX Video HDR | ➡️ **VFX SDK TrueHDR** | 2 |
| Video frame generation (VFX SDK) | ✅ Same SDK, 2–4x | 2 |
| DLSS upscaling on video | ✅ Optical flow + flat depth + DLSS SR (presets K, M, L) | 4 |
| DLSS Frame Gen on video | ✅ DLSS-G with up to 5 generated frames (6x) | 4 |
| FSR / NIS spatial upscaling | ✅ Shaders (MIT license) | 1 |
| Sharpening, FXAA/SMAA, 3D LUT, color controls | ✅ Compute shaders | 5 |
| Side-by-side original vs. enhanced | ✅ | 2 |
| Per-stage GPU timing and stats overlay | ✅ Vulkan timestamp queries | 1–2 |
| Recording and export | ✅ NVENC | 5 |
| Virtual camera | ✅ v4l2loopback | 5 |
| Presets, hotkeys, gamepad control | ✅ | 5 |
| Moonlight / PS5 / Xbox streaming | ⏳ Later: moonlight-common-c, chiaki-ng | 6 |
| Real-time subtitle translation | ⏳ Later: whisper.cpp | 6 |
| **Not included** | Driver RTX VSR (not on Linux), AMD/Intel AI backends, RTX 30 Frame Gen unlock patches, Windows-specific features | – |

## 5. Phases

**Phase 0: Feasibility tests (about 1 week).** Small programs that each test one risky piece before we build on it:

1. DLSS upscaling through Vulkan NGX: create the feature on driver 620 and upscale one test image.
2. **DLSS Frame Gen in our own Vulkan app** (the biggest risk). Veyra does this in DirectX 12, but the
   Vulkan version might expect to control the swapchain. If it fails, we use VFX frame generation only.
3. VFX SDK on Linux: run VSR on a Vulkan image shared with CUDA.
4. `VK_NV_optical_flow` on the RTX 5080.
5. Capture card and PipeWire frames passed directly into Vulkan.
6. HDR output on KDE Wayland with NVIDIA.

**Phase 1: Basic player.** Video file → decode → color conversion → NIS/FSR1 → fullscreen window, with
audio sync, ImGui controls and a stats overlay.

**Phase 2: NVIDIA video AI.** VFX VSR, TrueHDR and frame generation, plus side-by-side comparison and
per-stage timing.

**Phase 3: Live input.** Capture card (V4L2 with audio passthrough), screen capture (PipeWire), and a
low-latency mode with a small frame queue and immediate display.

**Phase 4: Veyra-style DLSS.** Optical flow (NVOF plus the shader fallback), DLSS SR on video, DLSS-G
multi-frame generation, and even frame timing for 2x–6x.

**Phase 5: Output and polish.** NVENC recording, virtual camera, LUT, sharpening, anti-aliasing,
presets, hotkeys, gamepad, and the Arch package.

**Phase 6: Optional extras.** Moonlight and PS5 Remote Play inputs, subtitle translation, and a Qt/QML
interface.

**Rough time:** Phases 0–3 give a useful app in about 4–6 weeks. Phase 4 adds another 2–4 weeks,
because frame timing and artifact fixes take time. These are estimates.

## 6. Code layout

```
src/
  app/        main, ImGui UI, hotkeys, presets
  core/       pipeline, frame scheduler, A/V clock
  gpu/        Vulkan device, memory (VMA), CUDA & DMA-BUF sharing, timing
  sources/    ffmpeg_file, v4l2_capture, pipewire_screen
  audio/      PipeWire output, sync
  effects/    colorspace, nis, fsr1, cas_sharpen, lut, aa
  nvidia/vfx/ vsr, vfg, truehdr
  nvidia/ngx/ dlss_sr, dlss_fg
  motion/     nvof_vulkan, dis_fallback (ported from Veyra)
  present/    swapchain, pacing, hdr
  sinks/      nvenc_record, v4l2loopback
shaders/      HLSL → SPIR-V
tools/        test programs, benchmarks, test images
```

## 7. Risks

| Risk | Impact | Fallback |
|---|---|---|
| DLSS-G doesn't work outside a game's swapchain on Vulkan | No DLSS Frame Gen | VFX frame generation (2–4x) |
| VFX SDK on Linux is labelled Early Access (according to vsr-player) and could change | Builds break on updates | Pin a version and check it on startup |
| HDR on NVIDIA + KDE Wayland is still new | No HDR output | SDR output with tone mapping |
| DLSS on video shows artifacts | Worse picture | Use VFX VSR by default and keep DLSS marked experimental |
| Frame generation adds lag | Bad for fast games | Low-latency mode, with frame generation off for competitive play |
| NVIDIA SDK licenses | Limits on sharing the files | Don't bundle the SDKs; download them during install |

## 8. Open decisions

1. **License:** GPL-3.0 (lets us reuse Veyra's code) or our own license (no Veyra code).
2. **Main use:** capture card (console) or video files first? This decides the order of Phases 1–3.
3. **Capture card model**, if any, so we can test with its formats.

## References

- Veyra (reference design, GPL-3.0): https://github.com/Likely7/Veyra-NRVideo
- NVIDIA DLSS SDK (includes Linux x86_64 libraries): https://github.com/NVIDIA/DLSS
- NVIDIA VFX SDK, Video Super Resolution: https://docs.nvidia.com/maxine/vfx/latest/Filters/VideoSuperResolution.html
- NVIDIA DLSS / Smooth Motion / Reflex on Linux: https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/latest/gaming.html
- vsr-player (VFX SDK on Linux, prior art): https://github.com/zhangmq/vsr-player
