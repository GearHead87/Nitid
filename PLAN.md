# Nitid: Plan

A new **Linux-only** real-time video enhancer. It is not a port of
[Veyra](https://github.com/Likely7/Veyra-NRVideo). It reuses Veyra's approach and some of its
shader logic, and runs on NVIDIA's Linux SDKs.

## Status

| Step | State |
|---|---|
| Name, plan, repo | ✅ Done |
| Project setup: engine + GUI + CLI layout, CMake build, install/package files | ✅ Done (placeholder code; combined and standalone builds tested) |
| Phase 0: feasibility tests | ⏭️ Next: plan in [tools/spikes/TESTPLAN.md](tools/spikes/TESTPLAN.md), results in [tools/spikes/README.md](tools/spikes/README.md) |

## 1. Goal

An engine plus two apps that take **a video file, a capture card, or your screen**, and enhance it
live on an RTX GPU:

- AI upscaling (VFX SDK VSR, or DLSS on video the way Veyra does it)
- AI frame generation (VFX SDK, or DLSS Frame Gen up to 6x)
- SDR to HDR
- Shown fullscreen with low lag, and optionally recorded or sent to OBS or Discord

| Component | What it is |
|---|---|
| **libnitid** | The engine: inputs, GPU pipeline, NVIDIA AI effects, display, recording. No UI code. |
| **nitid** | The GUI player you run day to day (SDL3 window + ImGui overlay). |
| **nitid-cli** | No window: export files, benchmarks, virtual camera, automated tests. |

**Target:** Linux (Wayland first, KDE tested), NVIDIA RTX GPUs. Frame generation needs RTX 40 or 50.

## 2. Tech stack

| Layer | Choice | Used in | Why |
|---|---|---|---|
| Language | **C++23** | all | All NVIDIA SDKs, Vulkan and FFmpeg have C/C++ APIs, and Veyra is C++ too |
| Build | CMake + Ninja; **each component is its own CMake project** | all | Build together or separately (see section 6) |
| GPU | **Vulkan 1.3+** with volk and VMA (Vulkan Memory Allocator) | libnitid | Replaces DirectX 12. DLSS has Linux support only through Vulkan. |
| Shaders | HLSL compiled to SPIR-V with **DXC** | libnitid | Lets us reuse Veyra's HLSL compute shaders (color, optical flow, sharpening) with small changes |
| AI upscaling | **NVIDIA VFX SDK**: VideoSuperRes | libnitid | Built for video |
| AI frame gen (video) | **NVIDIA VFX SDK**: VideoFrameGeneration | libnitid | The same SDK Veyra uses for this |
| SDR to HDR | **NVIDIA VFX SDK**: TrueHDR | libnitid | Replaces RTX Video HDR |
| DLSS | **NVIDIA DLSS SDK 310.9.16**: `libnvidia-ngx-dlss.so`, `libnvidia-ngx-dlssg.so` | libnitid | Veyra-style DLSS upscaling and Frame Gen on video |
| Motion estimation | **`VK_NV_optical_flow`** (hardware), with a GPU shader fallback ported from Veyra | libnitid | Motion vectors for DLSS |
| Sharing frames between CUDA and Vulkan | `VK_KHR_external_memory_fd` + `cudaImportExternalMemory` | libnitid | The VFX SDK runs on CUDA, and this avoids copying frames through the CPU |
| Decoding | **FFmpeg** with NVDEC or Vulkan hardware decoding | libnitid | Files, network streams, RTSP |
| Capture card | **V4L2**, with frames passed straight into Vulkan (DMA-BUF) | libnitid | Uses the capture card's own frames without copying them |
| Screen capture | **xdg-desktop-portal + PipeWire** (DMA-BUF) | libnitid | The standard way to capture the screen on Wayland |
| Audio | **PipeWire** | libnitid | Low-latency capture-card audio and keeping sound in sync with video |
| Recording | FFmpeg with **NVENC** (H.264, HEVC, AV1) | libnitid | Records the enhanced output |
| Virtual camera | **v4l2loopback** | libnitid | OBS and Discord can use the enhanced feed |
| Presets / logs | toml++ / spdlog | libnitid | Presets in TOML, shared by both apps |
| Window and input | **SDL3** | nitid | Wayland, Vulkan, fullscreen, gamepads |
| UI | **Dear ImGui** drawn over the video | nitid | Simple, fast, suited to a live tool. A Qt/QML interface can come later. |
| Command line | CLI11 | nitid-cli | Subcommands and flags |
| License | **GPL-3.0** (to confirm) | all | Required if we reuse Veyra code |
| Packaging | Arch `PKGBUILD`: one `nitid` package now, split into `libnitid` / `nitid` / `nitid-cli` later | all | The VFX SDK is downloaded during install, like vsr-player does |

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
        ◀──────────────────────── all inside libnitid ────────────────────────▶

   nitid (GUI): changes settings, reads stats, draws the ImGui overlay through the
                engine's per-frame overlay hook
   nitid-cli:   same settings and stats, no window (export, benchmark, virtual cam)
```

Frames stay on the GPU the whole way. CUDA and Vulkan share memory, so nothing is copied through the CPU.

## 4. Features compared with Veyra

| Veyra (Windows) | Nitid (Linux) | Component | Phase |
|---|---|---|---|
| Video file playback with audio | ✅ FFmpeg + PipeWire | libnitid, nitid | 1 |
| Capture card | ✅ V4L2, with frames passed straight into Vulkan | libnitid | 3 |
| Screen capture / game mode | ✅ PipeWire portal | libnitid | 3 |
| Driver RTX VSR | ➡️ **VFX SDK VSR**, including denoise and deblur modes | libnitid | 2 |
| RTX Video HDR | ➡️ **VFX SDK TrueHDR** | libnitid | 2 |
| Video frame generation (VFX SDK) | ✅ Same SDK, 2–4x | libnitid | 2 |
| DLSS upscaling on video | ✅ Optical flow + flat depth + DLSS SR (presets K, M, L) | libnitid | 4 |
| DLSS Frame Gen on video | ✅ DLSS-G with up to 5 generated frames (6x) | libnitid | 4 |
| FSR / NIS spatial upscaling | ✅ Shaders (MIT license) | libnitid | 1 |
| Sharpening, FXAA/SMAA, 3D LUT, color controls | ✅ Compute shaders | libnitid | 5 |
| Side-by-side original vs. enhanced | ✅ | nitid | 2 |
| Per-stage GPU timing and stats overlay | ✅ Vulkan timestamp queries | libnitid (stats), nitid (overlay) | 1–2 |
| Recording and export | ✅ NVENC | libnitid, nitid-cli | 5 |
| Virtual camera | ✅ v4l2loopback | libnitid, nitid-cli | 5 |
| Presets, hotkeys, gamepad control | ✅ | libnitid (presets), nitid (input) | 5 |
| — (new in Nitid) | Headless benchmarks and automated quality tests | nitid-cli | 1–2 |
| Moonlight / PS5 / Xbox streaming | ⏳ Later: moonlight-common-c, chiaki-ng | libnitid | 6 |
| Real-time subtitle translation | ⏳ Later: whisper.cpp | libnitid | 6 |
| **Not included** | Driver RTX VSR (not on Linux), AMD/Intel AI backends, RTX 30 Frame Gen unlock patches, Windows-specific features | – | – |

## 5. Phases

**Setup (done).** Repo, README, plan, and the three-component layout with a CMake build that works
both combined and per component. The code is placeholders that only prove the apps call the engine.

**Phase 0: Feasibility tests (about 1 week).** Small standalone programs in `tools/spikes/` that each
test one risky piece before we build on it. What works moves into `libnitid` in later phases.
The detailed test plan (games, tools, VFX SDK and C++ spikes) is in
[tools/spikes/TESTPLAN.md](tools/spikes/TESTPLAN.md). The main questions:

1. DLSS upscaling through Vulkan NGX: create the feature on driver 620 and upscale one test image.
2. **DLSS Frame Gen in our own Vulkan app** (the biggest risk). Veyra does this in DirectX 12, but the
   Vulkan version might expect to control the swapchain. If it fails, we use VFX frame generation only.
3. VFX SDK on Linux: run VSR on a Vulkan image shared with CUDA.
4. `VK_NV_optical_flow` on the RTX 5080.
5. Capture card and PipeWire frames passed directly into Vulkan.
6. HDR output on KDE Wayland with NVIDIA.

**Phase 1: Basic player.**
- libnitid: first real public API (engine, settings, stats, overlay hook), Vulkan device, FFmpeg
  file input, color conversion, NIS/FSR1, display with frame timing, PipeWire audio with A/V sync.
- nitid: SDL3 fullscreen window, ImGui controls and stats overlay.
- nitid-cli: `bench` command that runs a file through the pipeline without a window and prints
  per-stage GPU times.

**Phase 2: NVIDIA video AI.** VFX VSR, TrueHDR and frame generation in libnitid; side-by-side
comparison in nitid; quality and timing tests in nitid-cli.

**Phase 3: Live input.** Capture card (V4L2 with audio passthrough), screen capture (PipeWire), and a
low-latency mode with a small frame queue and immediate display.

**Phase 4: Veyra-style DLSS.** Optical flow (NVOF plus the shader fallback), DLSS SR on video, DLSS-G
multi-frame generation, and even frame timing for 2x–6x.

**Phase 5: Output and polish.** NVENC recording and export (both apps), virtual camera (nitid-cli),
LUT, sharpening, anti-aliasing, presets, hotkeys, gamepad, and the Arch package.

**Phase 6: Optional extras.** Moonlight and PS5 Remote Play inputs, subtitle translation, a Qt/QML
interface, and splitting into separate packages if needed.

**Rough time:** Phases 0–3 give a useful app in about 4–6 weeks. Phase 4 adds another 2–4 weeks,
because frame timing and artifact fixes take time. These are estimates.

## 6. Code layout

One repo with three components. Each component is its own CMake project, so it can be built
on its own and later shipped as a separate package. Today they are built and shipped together.

```
nitid/
  CMakeLists.txt          combined build (all components)
  libnitid/               engine library → libnitid.so + CMake package "libnitid"
    include/nitid/        public API (the only thing apps may include)
    src/
      core/        pipeline, frame scheduler, A/V clock
      gpu/         Vulkan device, memory (VMA), CUDA & DMA-BUF sharing, timing
      sources/     ffmpeg_file, v4l2_capture, pipewire_screen
      audio/       PipeWire output, sync
      effects/     colorspace, nis, fsr1, cas_sharpen, lut, aa
      nvidia/vfx/  vsr, vfg, truehdr
      nvidia/ngx/  dlss_sr, dlss_fg
      motion/      nvof_vulkan, dis_fallback (ported from Veyra)
      present/     swapchain, pacing, hdr
      sinks/       nvenc_record, v4l2loopback
    shaders/       HLSL → SPIR-V
    cmake/         package config template
  apps/
    nitid/                GUI player: SDL3 window + ImGui overlay, hotkeys
    nitid-cli/            headless: export, benchmarks, virtual camera, tests
  tools/
    spikes/               Phase 0 feasibility programs
                          (plus benchmarks and test images later)
```

**Rules**

- `libnitid` has **no UI dependencies** (no SDL, no ImGui). It exposes settings, stats and a
  per-frame overlay hook. Apps change settings and read stats; the engine never includes app code.
- Apps include **only** `libnitid/include/nitid/` and link the `nitid::nitid` target.
- `libnitid` is a shared library with hidden symbol visibility: only `NITID_EXPORT` symbols are
  public, so the API boundary is explicit and safe to package separately.
- While on 0.x, a minor version bump may break the API (`SameMinorVersion` compatibility).

**Building**

```sh
# Everything together (normal development)
cmake -S . -B build -G Ninja && cmake --build build
#   options: -DNITID_BUILD_GUI=OFF  -DNITID_BUILD_CLI=OFF

# Each component separately
cmake -S libnitid -B build-lib -G Ninja -DCMAKE_INSTALL_PREFIX=$PREFIX
cmake --build build-lib && cmake --install build-lib

cmake -S apps/nitid -B build-gui -G Ninja -DCMAKE_PREFIX_PATH=$PREFIX
cmake --build build-gui

cmake -S apps/nitid-cli -B build-cli -G Ninja -DCMAKE_PREFIX_PATH=$PREFIX
cmake --build build-cli
```

In the combined build the apps link the in-tree engine target directly; standalone, they find
the installed engine with `find_package(libnitid)`. Installed binaries locate `libnitid.so`
via `RUNPATH $ORIGIN/../lib`.

**Packaging:** today one package (`nitid`) installs all three. Later it can be split into
`libnitid`, `nitid` (GUI) and `nitid-cli` without moving code. If a component needs its own
repo, `git subtree split --prefix=libnitid` extracts it with history.

## 7. Risks

| Risk | Impact | Fallback |
|---|---|---|
| DLSS-G doesn't work outside a game's swapchain on Vulkan | No DLSS Frame Gen | VFX frame generation (2–4x) |
| VFX SDK on Linux is labelled Early Access (according to vsr-player) and could change | Builds break on updates | Pin a version and check it on startup |
| HDR on NVIDIA + KDE Wayland is still new | No HDR output | SDR output with tone mapping |
| DLSS on video shows artifacts | Worse picture | Use VFX VSR by default and keep DLSS marked experimental |
| Frame generation adds lag | Bad for fast games | Low-latency mode, with frame generation off for competitive play |
| NVIDIA SDK licenses | Limits on sharing the files | Don't bundle the SDKs; download them during install |
| Engine API changes often while features are added | Apps break between versions | Keep all apps in this repo and ship them together until 1.0 |

## 8. Decisions

**Made**

- Name: **Nitid** (see README).
- Layout: one repo, three components (libnitid, nitid, nitid-cli), each buildable on its own,
  shipped together for now.

**Open**

1. **License:** GPL-3.0 (lets us reuse Veyra's code) or our own license (no Veyra code).
2. **Main use:** capture card (console) or video files first? This decides the order of Phases 1–3.
3. **Capture card model**, if any, so we can test with its formats.

## References

- Veyra (reference design, GPL-3.0): https://github.com/Likely7/Veyra-NRVideo
- NVIDIA DLSS SDK (includes Linux x86_64 libraries): https://github.com/NVIDIA/DLSS
- NVIDIA VFX SDK, Video Super Resolution: https://docs.nvidia.com/maxine/vfx/latest/Filters/VideoSuperResolution.html
- NVIDIA DLSS / Smooth Motion / Reflex on Linux: https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/latest/gaming.html
- vsr-player (VFX SDK on Linux, prior art): https://github.com/zhangmq/vsr-player
