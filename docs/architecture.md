# Nitid architecture (0.5)

How the code is organised, how a frame moves through it, and where each planned feature plugs in.
For the overall plan see [PLAN.md](../PLAN.md); for build steps see [building.md](building.md).

## Components

| Component | What it is | Depends on |
|---|---|---|
| `libnitid/` | The engine: `libnitid.so` + CMake package `libnitid` (target `nitid::nitid`) | Vulkan, CUDA driver API, FFmpeg, VFX SDK headers (runtime loaded at run time) |
| `libnitid/dlss-module/` | `libnitid-dlss.so`: all DLSS SDK (NGX) code, loaded by libnitid at run time through the C table `src/dlss/plugin_api.h` | DLSS SDK headers + `libnvsdk_ngx.a` (NVIDIA's license, kept out of libnitid) |
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
                      PTX), SpatialScaler (FSR 1 / NIS), vk_resources (images, command buffer + fence for the
                      pipeline thread's Vulkan work), OpticalFlow (VK_NV_optical_flow -> per-pixel motion vectors)
  kernels/color.cu    NV12/P010 -> RGBA8, luma thumbnail, luma for optical flow, flow grid -> motion vectors
  kernels/scale.cu    FSR 1 EASU + RCAS, NIS NVScaler (CUDA ports of the MIT shaders)
audio/                AudioPlayer (own FFmpeg demuxer + decoder thread, PipeWire stream, follows the picture),
                      av_sync.hpp (the resampling controller)
platform/             PipeWire and libdbus loaded at run time (sound, screen capture, portal)
input/                FrameSource interface (next, recycle, seek; live: try_next); FfmpegFileSource (decoder thread,
                      NVDEC + GPU colour conversion, CPU fallback, frame-exact seek); SceneCutDetector;
                      PipeWireCapture (live video: DMA-BUF via Vulkan on a second queue, or shared memory; newest
                      frame only); ScreenCastPortal (desktop portal over D-Bus, restore token)
vfx/                  VFX runtime loader (dlopen), SuperRes (VSR), FrameGen (VFG, Timestep mode)
dlss/                 Runtime (finds + loads libnitid-dlss.so and NVIDIA's DLSS runtime, starts NGX on the device),
                      SuperRes (DLSS SR: CUDA buffer -> shared buffer -> Vulkan image -> DLSS -> back), FrameGen
                      (DLSS-G: all frames of a pair in one submission, kept until the pair changes)
pipeline/             Pipeline (upload -> [optical flow] -> VSR / DLSS / FSR / NIS or VFX clean-up -> two-frame ring
                      -> VFX or DLSS frame gen; keeps the
                      original next to each frame in compare mode), RenderAhead (window output: the pipeline on its
                      own thread, frames prepared ahead of the display, frame-gen pacing), frame_layout (size maths),
                      frame_clock (display clock, frame/phase selection)
output/               SwapchainPresenter (own high-priority CUDA stream, FIFO, two presents queued, present-wait
                      stats and display latency, compare split blit, app overlay via dynamic rendering, capture hook)
third_party/          NIS_Config.h (MIT), license texts of FSR 1 / NIS
tests/ (libnitid/tests) unit tests for the pure logic (ctest)
```

## Frame flow

```
decoder thread                     render-ahead worker                      render thread (app's loop)       display
──────────────                     ───────────────────                      ──────────────────────────       ───────
FFmpeg/NVDEC ─► NV12 (CUDA) ─►     Pipeline::produce(next grid position)    DisplayClock.tick (one refresh)
  kernel: NV12 → RGBA8             │ upload (+ original in compare mode)     RenderAhead::take(position):
  thumbnail → SceneCutDetector     │ VSR / FSR / NIS / clean-up              newest frame reached
  [queue of 4] ──────────────────► │ ring (cur, next) → frame gen            AudioPlayer::sync(clock time)
                                   ▼                                         SwapchainPresenter::present
                                   copy into pool (6 buffers), queue (4) ──►  CUDA copy (own stream) → Vulkan:
                                   grid: refresh × stride | 1/m | 1 frame      blit (or original | Nitid split)
                                   stride from frame cost (≤ 95 % rule)        → overlay (ImGui) → FIFO ─────► 165 Hz
```

- **Position** (in source frames) comes from `DisplayClock`: whole refreshes, follows the wall clock,
  freezes after stalls > 0.25 s. `pick(position, multiplier)` gives the real frame and the phase
  (rounded to k/multiplier, or exact for "per refresh").
- **Frames that fall behind** are skipped *before* upload/VSR (no GPU time spent on frames never shown).
- **Scene cuts**: a cut between `cur` and `next` makes frame gen hold `cur` instead of morphing.
- **Render-ahead** (window output): the worker owns the pipeline between `start` and `stop` (seek, close);
  settings changes are applied by the worker (models load without freezing the window). When frame generation
  costs more than 95 % of a refresh it prepares one frame every `stride` refreshes (tests/v0.3/01).
- **Headless** (`render_headless`): position = n / multiplier, output copied to the caller's buffer; no worker.
- **DLSS** (0.5): frames stay CUDA buffers; DLSS stages copy them into buffers shared with Vulkan, run NGX in one
  submission on the pipeline thread's own Vulkan queue, and copy back (tests/v0.5/02). Motion vectors come from
  the optical flow queue at the source size (current -> previous, pixels). DLSS-G makes whole steps, so the
  render-ahead grid is 1/m with m the largest step not above the refresh rate (tests/v0.5/04).
- **Live input** (screen capture, `open_pipewire`): no render-ahead (a frame can't be prepared before it
  arrives). Each refresh `Pipeline::produce_live` takes the newest captured frame and either shows it (low
  latency) or generates the moment one frame interval ago between the last two (smooth) (tests/v0.4).
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
10. **The presenter never waits on the pipeline's stream**: it has its own (highest-priority) CUDA stream; the
    pipeline's stream is lowest priority (tests/v0.3/01).
11. **Ignore warm-up frames when measuring cost** (decoder start, first model runs: 30–50 ms) (tests/v0.3/01).
12. **DLSS-G frames of a pair are generated in order, all at once** (NVIDIA's rule: index 1..n per new frame);
    a seek / skipped pair primes it with the older frame and a reset (tests/v0.5/04).
13. **DLSS without motion vectors smears video**; with optical flow only preset M improves, and DLAA gets worse
    with them (tests/v0.5/02, 03). Measure each preset; NVIDIA's defaults are tuned for games.
14. **Pair frames by number with one time base** in quality scripts (`settb=AVTB,setpts=N*40000`), and average
    per-frame PSNR for generated frames (held frames at cuts dominate an MSE average) (tests/v0.5/02, 04).

## Where planned features go

| Feature (PLAN phase) | Where | Notes |
|---|---|---|
| Audio + A/V sync (1) | ✅ 0.2: `libnitid/src/audio/` | the picture leads, the sound follows (resampling) |
| ImGui overlay (1) | ✅ 0.2: `apps/nitid/src/ui/` + `Engine::set_overlay` | dynamic rendering after the blit |
| NIS / FSR 1 (1) | ✅ 0.2: `gpu/spatial_scaler.*`, `kernels/scale.cu` | CUDA ports; NIS ≤ 2x |
| Presets, hotkeys, gamepad (5) | presets: `libnitid` (TOML); input: `apps/nitid/src/key_bindings.*` | key table already single-source |
| TrueHDR / HDR output (2, postponed) | `vfx/true_hdr.*`; HDR10 swapchain in `output/` | Settings gains HDR fields |
| Screen capture (3) | ✅ 0.4: `input/pipewire_capture.*`, `input/screencast_portal.*` | DMA-BUF → Vulkan → CUDA, 0.33 ms per 1440p frame |
| Capture card (3) | `input/v4l2_source.*` implementing `FrameSource` | skipped for now (no card) |
| Low-latency live mode (3) | ✅ 0.4: `Settings::live_mode`, `Pipeline::produce_live` | low latency = newest frame; smooth = frame gen one frame back |
| DLSS SR / DLSS-G + optical flow (4) | ✅ 0.5: `dlss-module/`, `src/dlss/`, `gpu/optical_flow.*` | below VSR / VFX on compressed video; cheap high multipliers |
| DLSS 5 neural rendering (4+) | `dlss-module/` (feature 18) when NVIDIA ships `libnvidia-ngx-dlssnr.so` | spike S10: not on Linux yet |
| VSR / frame gen off the render thread | ✅ 0.3: `pipeline/render_ahead.*` | frame gen HIGH smooth at 165 Hz (stride 2) |
| Side-by-side comparison (2) | ✅ 0.3: pipeline keeps originals, presenter split blit | UI draws the line |
| NVENC recording / export (5) | `output/nvenc_recorder.*`; `nitid-cli export` command | `nitid-cli`'s ffmpeg-pipe writer is a stopgap |
| Virtual camera (5) | `output/v4l2loopback_sink.*`; `nitid-cli virtual-camera` | spike M5 pending |
| More CLI commands | `apps/nitid-cli/src/commands/<name>.cpp` + register in `commands.hpp` | one file per subcommand |
