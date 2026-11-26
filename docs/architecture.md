# Nitid architecture (0.2)

How the code is organised, how a frame moves through it, and where each planned feature plugs in.
For the overall plan see [PLAN.md](../PLAN.md); for build steps see [building.md](building.md).

## Components

| Component | What it is | Depends on |
|---|---|---|
| `libnitid/` | The engine: `libnitid.so` + CMake package `libnitid` (target `nitid::nitid`) | Vulkan, CUDA driver API, FFmpeg, VFX SDK headers (runtime loaded at run time) |
| `apps/nitid/` | GUI player (`nitid`) | libnitid, SDL3, CLI11, Dear ImGui (downloaded at configure time) |
| `apps/nitid-cli/` | Headless tool (`nitid-cli`) | libnitid, CLI11 |

Each is its own CMake project. The combined build links the in-tree engine; standalone app builds use
`find_package(libnitid)`. Apps see **only** `libnitid/include/nitid/` (hidden symbol visibility).

## Public API (`libnitid/include/nitid/`)

| Header | Contents |
|---|---|
| `nitid.hpp` | Includes everything below |
| `engine.hpp` | `Engine`: create, open a file, seek, sound (volume, mute, A/V offset), settings, stats, window output (`attach_window`, `render_frame`, `set_overlay` + `vulkan_device` for an app-drawn overlay), headless output (`set_headless_output`, `render_headless`), test hook `capture_next_window_frame` |
| `settings.hpp` | `Settings` (upscaler, frame gen, quality, multiplier, scene cuts) + name parsing shared by both apps |
| `stats.hpp` | `Stats` snapshot: sizes, what is active and why not, rates, counters, per-stage times |
| `source.hpp` | `SourceInfo`, `OpenOptions` |
| `window.hpp` | `WindowTarget` (the app creates the surface for the engine's Vulkan instance), `VulkanDevice`, `OverlayFrame` / `OverlayCallback` |
| `error.hpp` | `nitid::Error` (all API failures; message is user-readable) |
| `version.hpp` | runtime library version |

## Inside libnitid (`libnitid/src/`)

```
engine.cpp            Engine::Impl: owns everything below, drives one frame per call
settings.cpp          to_string / parse helpers
core/                 log (NITID_LOG=debug), errors, BoundedQueue, RunningAverage
gpu/                  VulkanContext (NVIDIA GPU picked explicitly, dynamic rendering on), CudaContext (UUID-matched,
                      *blocking* stream), interop (Vulkan buffers/semaphores shared with CUDA), Kernels (embedded
                      PTX), SpatialScaler (FSR 1 / NIS)
  kernels/color.cu    NV12/P010 -> RGBA8, luma thumbnail (compiled to PTX by nvcc at build time)
  kernels/scale.cu    FSR 1 EASU + RCAS, NIS NVScaler (CUDA ports of the MIT shaders)
audio/                AudioPlayer (own FFmpeg demuxer + decoder thread, PipeWire stream, follows the picture),
                      av_sync.hpp (the resampling controller)
platform/             PipeWire client library loaded at run time (sound now, screen capture later)
input/                FrameSource interface (next, recycle, seek); FfmpegFileSource (decoder thread, NVDEC + GPU
                      colour conversion, CPU fallback, frame-exact seek); SceneCutDetector
vfx/                  VFX runtime loader (dlopen), SuperRes (VSR), FrameGen (VFG, Timestep mode)
pipeline/             Pipeline (upload -> VSR / FSR / NIS -> two-frame ring -> frame gen), frame_layout (size maths),
                      frame_clock (display clock, frame/phase selection)
output/               SwapchainPresenter (FIFO, two presents queued, present-wait stats and display latency,
                      app overlay via dynamic rendering, capture hook)
third_party/          NIS_Config.h (MIT), license texts of FSR 1 / NIS
tests/ (libnitid/tests) unit tests for the pure logic (ctest)
```

## Frame flow

```
decoder thread                          render thread (app's loop)                         display
──────────────                          ──────────────────────────                         ───────
FFmpeg/NVDEC ─► NV12 (CUDA, our ctx) ─► [queue of 4] ─► Pipeline::produce(position)
   kernel: NV12 → RGBA8 (GPU buffer)        │  copy into ring / VSR input (device copy)
   kernel: luma thumbnail → CPU             │  VSR (once per source frame) → ring[next]
   SceneCutDetector                         │  frame gen(ring[cur], ring[next], phase) if between frames
                                            ▼
                                     AudioPlayer::sync(media time, when it will be on screen)
                                     SwapchainPresenter::present
                                        CUDA copy → shared buffer → signal semaphore
                                        Vulkan: wait → copy to image → blit (fit) → app overlay (ImGui)
                                                → present (FIFO)
                                        wait until the present from 2 refreshes ago is on screen ─► 165 Hz
```

- **Position** (in source frames) comes from `DisplayClock`: whole refreshes, follows the wall clock,
  freezes after stalls > 0.25 s. `pick(position, multiplier)` gives the real frame and the phase
  (rounded to k/multiplier, or exact for "per refresh").
- **Frames that fall behind** are skipped *before* upload/VSR (no GPU time spent on frames never shown).
- **Scene cuts**: a cut between `cur` and `next` makes frame gen hold `cur` instead of morphing.
- **Headless** (`render_headless`): position = n / multiplier, output copied to the caller's buffer.
- **Sound follows the picture**: the picture is locked to the display; each frame tells the AudioPlayer when it
  will be on screen (measured present-to-screen time), the PipeWire callback knows when its buffer will be
  heard, and the player resamples by ≤ 1 % or resyncs on jumps (tests/v0.2/03).
- **Seek**: the decoder seeks to the keyframe before the target and drops earlier frames before colour
  conversion; the display clock restarts when the first frame is ready (tests/v0.2/02).

## Rules learned the hard way (keep them)

1. **VFX SDK: use a blocking CUDA stream** (`CU_STREAM_DEFAULT`). With a non-blocking stream, VSR returned
   the previous frame and timings were too low (tests/v0.1/02).
2. **Test with changing frames.** A constant test image hides ordering bugs (spike S5 missed the above).
3. **FFmpeg decodes into our CUDA context and stream** (`av_hwdevice_ctx_alloc` + `cuda_ctx` + `stream`),
   so kernels are ordered after FFmpeg's copies (tests/v0.1/05).
4. **Two presents queued ahead** on KDE Wayland; one locked into 2/3 of the refresh rate (tests/v0.1/01).
5. **Load the VFX runtime with all its libraries preloaded** (dependencies, `libnvVFX*`, `libnvidia-ngx-*`),
   like NVIDIA's Python loader; then no `LD_LIBRARY_PATH` is needed.
6. **Score videos by frame number**, not timestamp (Matroska stores whole milliseconds; tests/v0.1/04).
7. **Compare upscalers against bicubic *inside* Nitid**: the 8-bit RGB working format alone caps PSNR at
   ~44.8 dB against the YUV source (tests/v0.2/01).
8. **Start clocks when the first frame is ready**, not when a seek is asked for (tests/v0.2/03).
9. **Run-time loaded libraries** (VFX, PipeWire) are only used through function tables; libnitid links with
   `--no-undefined` to catch slips.

## Where planned features go

| Feature (PLAN phase) | Where | Notes |
|---|---|---|
| Audio + A/V sync (1) | ✅ 0.2: `libnitid/src/audio/` | the picture leads, the sound follows (resampling) |
| ImGui overlay (1) | ✅ 0.2: `apps/nitid/src/ui/` + `Engine::set_overlay` | dynamic rendering after the blit |
| NIS / FSR 1 (1) | ✅ 0.2: `gpu/spatial_scaler.*`, `kernels/scale.cu` | CUDA ports; NIS ≤ 2x |
| Presets, hotkeys, gamepad (5) | presets: `libnitid` (TOML); input: `apps/nitid/src/key_bindings.*` | key table already single-source |
| TrueHDR / HDR output (2, postponed) | `vfx/true_hdr.*`; HDR10 swapchain in `output/` | Settings gains HDR fields |
| Screen capture (3) | `input/pipewire_source.*` implementing `FrameSource` | spike S6 code; DMA-BUF → Vulkan → CUDA |
| Capture card (3) | `input/v4l2_source.*` implementing `FrameSource` | skipped for now (no card) |
| Low-latency live mode (3) | `pipeline/` option: no lookahead frame, frame gen off or extrapolation | live inputs can't wait for `next` |
| DLSS SR / DLSS-G lite + optical flow (4) | `ngx/` + `motion/` modules; new `Upscaler` / `FrameGen` enum values | spikes S2, S3, S4, F1 |
| VSR / frame gen off the render thread | `pipeline/` worker + queue of generated frames | needed for frame gen HIGH at 165 Hz (tests/v0.1/03) |
| NVENC recording / export (5) | `output/nvenc_recorder.*`; `nitid-cli export` command | `nitid-cli`'s ffmpeg-pipe writer is a stopgap |
| Virtual camera (5) | `output/v4l2loopback_sink.*`; `nitid-cli virtual-camera` | spike M5 pending |
| More CLI commands | `apps/nitid-cli/src/commands/<name>.cpp` + register in `commands.hpp` | one file per subcommand |
