# Nitid: Test Plan (Phase 0)

Goal: prove that every module in [PLAN.md](../../PLAN.md) works on the target Linux setup **before**
building on it. Every test answers one yes/no question. Results go into the table in
[README.md](README.md).

The work is in four stages: **prepare → check with no code → check with existing tools → small C++ test programs.**

## Who does what

- **Claude writes** all scripts and test programs, and runs the tests that don't need a screen:
  builds, timings, image output.
- **The user runs** anything that needs eyes or a screen (games, HDR viewing, OBS, judging
  picture quality) and anything that needs `sudo`.
- Every test saves its output to `results/<test-id>/` (git-ignored) and its conclusion to the
  test's `NOTES.md` and the README table (committed).

## Test machine

| Item | Value |
|---|---|
| OS | Arch Linux, kernel 7.3.5, KDE Plasma on Wayland |
| CPU / RAM | AMD Ryzen 9 9950X / 32 GB |
| GPU | NVIDIA GeForce RTX 5080, 16 GB (Blackwell) |
| Driver | `nvidia-open-dkms` 620.73.04 |
| CUDA | 13.5 |
| Monitor | DP-6, KDE reports "HDR: disabled" (so probably HDR-capable, but turned off) |
| Capture card | None connected yet |

## Baseline check (already done, no code)

Ran on 2026-11-23 with `vulkaninfo`, `ffmpeg`, `systemctl`, `kscreen-doctor` and `pacman`.

| Check | Result |
|---|---|
| Vulkan: `VK_NV_optical_flow` | ✅ |
| Vulkan: `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd` (CUDA ↔ Vulkan) | ✅ |
| Vulkan: `VK_EXT_external_memory_dma_buf`, `VK_EXT_image_drm_format_modifier` (capture import) | ✅ |
| Vulkan: `VK_KHR_video_decode_queue` + H.264 / H.265 / AV1 | ✅ |
| Vulkan: `VK_KHR_present_wait`, `VK_KHR_present_id`, `VK_EXT_present_timing`, `VK_EXT_swapchain_maintenance1` | ✅ |
| Vulkan: `VK_NV_low_latency2` (Reflex) | ✅ |
| Vulkan: `VK_EXT_hdr_metadata`, `VK_EXT_swapchain_colorspace`, `VK_KHR_wayland_surface` | ✅ |
| Vulkan: `VK_KHR_timeline_semaphore` | ✅ |
| Driver ships NGX: `libnvidia-ngx.so` (includes FrameGeneration entry points), Wine `nvngx_dlssg.dll` | ✅ |
| CUDA 13.5 (`nvcc`) | ✅ |
| FFmpeg: NVENC `h264_nvenc`, `hevc_nvenc`, `av1_nvenc`; decoding via `cuda`, `vulkan` | ✅ |
| PipeWire, WirePlumber, xdg-desktop-portal (+ KDE backend) running | ✅ |
| `v4l2loopback` 0.15.4 (virtual camera) | ✅ installed |
| `libportal`, `libpng`, `imagemagick`, `jq`, `mangohud`, `mpv`, OBS, SDL3, glslang | ✅ installed |
| Monitor HDR | ⚠️ Disabled in KDE (not "incapable") |
| Capture card | ⚠️ None connected |
| Build packages | ⚠️ Missing, see Stage 0 |

---

## Stage 0: Preparation (about 20 minutes)

### Packages to install

```bash
sudo pacman -S --needed \
  vulkan-headers volk vulkan-memory-allocator directx-shader-compiler \
  vulkan-validation-layers spdlog cli11 tomlplusplus stb \
  git-lfs uv v4l2loopback-utils nvtop yt-dlp
```

- `vulkan-memory-allocator` comes from chaotic-aur (already enabled). Everything else is in `extra`.
- `uv` creates a Python 3.12 environment. NVIDIA's samples pin PyTorch 2.11 (CUDA 12.8), and the
  system Python is 3.14, which may be too new for it. The system Python is not changed.
- `git-lfs` is needed for the sample videos in NVIDIA's VFX samples repo.

### Scripts to write

| Script | What it does |
|---|---|
| `scripts/sysinfo.sh` | Saves driver, kernel, Vulkan features, Proton and dxvk-nvapi versions, and monitor HDR status to `results/sysinfo.txt`. Run before every test session. |
| `scripts/fetch-sdks.sh` | Downloads the **DLSS SDK at tag `v310.9.16`**, but only `include/`, `lib/Linux_x86_64/` and `doc/` (the full repo is about 880 MB). Also downloads [NVIDIA-Maxine/nvidia-vfx-python-samples](https://github.com/NVIDIA-Maxine/nvidia-vfx-python-samples) with its test videos (git-lfs). Everything goes into `third_party/` (git-ignored). |
| `scripts/fetch-media.sh` | Makes the test videos: NVIDIA's sample clip (`Drift_RUN_Master_Custom.mp4`), plus versions shrunk to 1080p, 720p and 540p with heavy compression (to imitate a stream or capture), synthetic patterns (moving text, scrolling, scene cuts), and an HDR10 (PQ / BT.2020) test pattern. |

---

## Stage 1: Proton games (no code, the user plays, about 1 hour)

**Purpose:** confirm the driver's DLSS paths work, compare the installed Proton builds, and see how
good in-game DLSS looks as a reference.

Installed Proton builds:

| Proton build | dxvk-nvapi |
|---|---|
| **Proton Experimental** (experimental-11.0-20261112) | **0.9.3** |
| **GE-Proton11-12** | **0.9.3** |
| cachyos-11.0-20261015-slr | 0.9.2 |
| Proton 11.0 | 0.9.2 |
| GE-Proton10-34 | 0.9.1 |
| Proton 10.0 | 0.9.0 |
| Proton Hotfix | unknown |

Use **single-player games only.** Skip FC 26 and Deadlock because of their anti-cheat.

| ID | Test | Game | Launch options | Pass if |
|---|---|---|---|---|
| **G1** | DLSS upscaling + Frame Gen 2x | Cyberpunk 2077 | `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=1024,DLSSGIndicator=2 mangohud %command%` | The DLSS indicator appears, DLSS-G shows as active, and MangoHud FPS roughly doubles |
| **G2** | Multi Frame Gen 4x | Cyberpunk 2077 | G1 + `PROTON_ENABLE_NGX_UPDATER=1 DXVK_NVAPI_DRS_NGX_DLSS_FG_OVERRIDE=on DXVK_NVAPI_DRS_NGX_DLSSG_MULTI_FRAME_COUNT=3` | The indicator shows 4x. **Run it on Experimental, GE-Proton11-12, CachyOS and Proton 11.0** to see which builds support it. |
| **G3** | Newest DLSS model | Cyberpunk 2077 or RE Requiem | `DXVK_NVAPI_DRS_NGX_DLSS_SR_OVERRIDE=on DXVK_NVAPI_DRS_NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION=render_preset_latest` | The indicator shows a newer preset than the game's own |
| **G4** | Smooth Motion (frame gen in the driver) | RDR2 with DLSS off, or Max Payne 3 / Sleeping Dogs | `NVPRESENT_ENABLE_SMOOTH_MOTION=1 mangohud %command%` | Motion looks clearly smoother with no obvious artifacts |
| G5 | Lag feel (optional) | Same as G2 and G4 | With and without frame gen | User's judgment, written down |

Notes:

- `MULTI_FRAME_COUNT` is the number of generated frames per real frame: `3` = 4x, `2` = 3x.
- Do not use native DLSS Frame Gen and Smooth Motion together (NVIDIA: lower performance and artifacts).
- The indicator setting stays saved for that game. Clear it once afterwards with
  `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=0,DLSSGIndicator=0`.
- Dynamic Multi Frame Gen is known to be blocked in vkd3d-proton
  ([issue #3292](https://github.com/HansKristian-Work/vkd3d-proton/issues/3292)) and is not tested.

**What it tells us:** whether DLSS-G and Multi Frame Gen work in this driver, and which Proton to
prefer. If G1 fails, check the driver first, because the later tests depend on it.

---

## Stage 2: Video, input and output with existing tools (bash, about 1 hour)

| ID | Test | How | Pass if |
|---|---|---|---|
| **M1** | NVENC recording | `ffmpeg -f lavfi -i testsrc2=s=3840x2160:r=60 -t 10 -c:v {h264,hevc,av1}_nvenc` | All three files play, and encoding is faster than real time |
| **M2** | GPU decoding | `ffmpeg -hwaccel vulkan` and `-hwaccel cuda` on H.264, HEVC and AV1 clips (`-f null -`) | At least 120 fps decoding at 4K with both methods |
| **M3** | **HDR on screen** | The user turns on HDR in KDE Display Settings. Then `mpv --vo=gpu-next --gpu-api=vulkan --target-colorspace-hint` plays the HDR pattern | KDE shows HDR on, and the mpv log shows PQ/BT.2020 output. The highlights look clearly brighter. |
| **M4** | Screen capture | OBS → "Screen Capture (PipeWire)" | The KDE permission dialog appears and the capture shows up |
| **M5** | Virtual camera | `sudo modprobe v4l2loopback exclusive_caps=1`, send a test pattern to it with FFmpeg, open it in OBS | The test pattern appears in OBS |
| M6 | Capture card (only if one is available) | `v4l2-ctl --list-formats-ext`, then `mpv av://v4l2:/dev/videoN` | Lists the formats and shows live video. Note the format and resolution. |

---

## Stage 3: NVIDIA VFX SDK in Python (no C++, about 1–2 hours)

Tests **the main video features with NVIDIA's own sample code.** Set up with `uv` (Python 3.12)
using the samples' `requirements.txt` (`nvidia-vfx==0.2.0.0`, `torch==2.11.0+cu128`, `av`, `numpy`).

VFX SDK requirements: driver 570.190+, 580.82+ or 590.44+ on Linux. Video Frame Generation needs an
Ada or Blackwell GPU. NVIDIA lists Ubuntu, Debian and RHEL as supported; Arch is untested.

| ID | Test | Command | Pass if |
|---|---|---|---|
| **V1** | VSR upscaling | `video_super_resolution.py --scale 2 --quality HIGH`, then `ULTRA` and `STREAMING_ULTRA`, on the 540p, 720p and 1080p clips | The output plays. **Speed: 1080p→4K at 60 fps or faster** in HIGH mode. |
| **V2** | Denoise and deblur | `--scale 1 --quality DENOISE_HIGH` / `DEBLUR_HIGH` | Visibly cleaner on the heavily compressed clip |
| **V3** | **Video frame generation** | `video_frame_generation.py --multiplier 2`, then `4` | Smooth output, frame count correct. Speed: 1080p 2x keeps up with real time. |
| **V4** | TrueHDR | `true_hdr.py` → HEVC Main10 | The output is BT.2020/PQ and **looks correct in mpv with HDR on** (reuses M3) |
| V5 | Quality comparison | Side-by-side clips (original / bicubic / VSR) and cropped close-ups made with FFmpeg | User's judgment |

**What it tells us:** if V1 and V3 pass, the core of Phases 2 and 3 works on this PC. If the Python
samples don't install on Arch, try the VFX C API the way
[vsr-player](https://github.com/zhangmq/vsr-player) does (loading the libraries from the package directly).

---

## Stage 4: C++ test programs (`tools/spikes/`, the main risks)

Each test is a standalone program with no window (except H1), built with `cmake -S tools/spikes`.
Each writes PNGs and timings to `results/`. Read `doc/DLSS-FG Programming Guide.pdf` and
`doc/DLSS_Programming_Guide_Release.pdf` from the DLSS SDK before S2/S3.

| ID | Program | What it does | Pass if | Risk |
|---|---|---|---|---|
| **S1** | `01-ngx-caps` | Starts Vulkan with the extensions NGX needs, starts NGX (`NVSDK_NGX_VULKAN_Init`), asks what's supported | Prints `SuperSampling.Available=1`, `FrameGeneration.Available=1` and the maximum multi-frame count | Low |
| **S2** | `02-dlss-sr-image` | Repeats one image for about 30 frames with jitter, zero motion and flat depth, through DLSS SR (presets K and M, Quality and Performance) | Valid upscaled PNG, no NGX errors, time per frame. Better than bicubic in a crop. | Medium |
| **S3** | **`03-dlssg-standalone`** | Gives DLSS-G two real frames with zero motion, flat depth and a fake camera (Veyra's approach), using `nvsdk_ngx_helpers_dlssg_vk.h`. Generates 1 frame, then 3 (4x) and 5 (6x). | **Generated frames exist and look like real in-between frames** | **High: decides whether Phase 4 is possible** |
| **S4** | `04-nvof-pair` | Runs `VK_NV_optical_flow` on two frames and saves the motion as a color image | The motion image makes sense (pans and moving objects show up). Under 2 ms at 1080p. | Low |
| **S5** | `05-cuda-vk-vsr` | Vulkan image → shared with CUDA (`VK_KHR_external_memory_fd` + `cudaImportExternalMemory`) → VFX VSR → back to Vulkan, all on the GPU | Correct PNG, nothing copied through the CPU, VSR time similar to V1 | Medium |
| **S6** | `06-pipewire-dmabuf` | libportal screen capture → PipeWire → passes **DMA-BUF** frames straight into Vulkan → saves a PNG | Frame captured through DMA-BUF, not a slower shared-memory fallback | **Medium–high:** passing frames this way between KDE and NVIDIA is known to be tricky |
| **H1** | `07-hdr-swapchain` | SDL3 window + Vulkan, lists the color spaces on offer, shows an HDR10 gradient with HDR metadata | `HDR10_ST2084` is offered and shows correctly (user checks by eye) | Medium |
| S7 | `08-present-timing` | Shows frames at exact intervals using `VK_EXT_present_timing` / `present_wait` and measures how uneven the timing is | Timing within ±0.5 ms. Needed for frame gen to look smooth. | Medium |
| S8 | `09-v4l2-import` (needs a capture card) | Passes capture card frames straight into Vulkan (`VIDIOC_EXPBUF` → DMA-BUF), or uses a fallback upload | One of the two works at the card's resolution and frame rate. Note: USB (UVC) cards often can't export DMA-BUF; a CPU upload of 1080p60 is fine. | Medium |

### Final combined test (only if S3 and S4 pass)

| ID | Test | Pass if |
|---|---|---|
| **F1** | **Veyra's approach end to end:** a real clip → optical flow (S4) → motion vectors into DLSS-G (S3) → generated frames saved as a video | The user judges the quality against V3 (VFX frame generation) |

### Backup if S3 fails: use the Proton builds

| ID | Test | Why |
|---|---|---|
| S3b | Run **Veyra's Windows release** (`Veyra-2.2.0-NVIDIA-win64-portable.zip`) **under GE-Proton11-12 / Experimental**: play a video file with DLSS FG on | If Veyra's DLSS-G on video works through Proton on this driver, the driver can do it, and we fix our Vulkan code instead of dropping the idea. Capture and driver VSR won't work there, which is expected. |

---

## Order and decisions

| Day | Tests | If something fails |
|---|---|---|
| 1 | Stage 0, `sysinfo`, **G1–G4**, **M1–M5** | G1 fails → fix the driver/Proton before going on. M3 fails → plan for SDR output first. |
| 1–2 | **V1–V5** | V1/V3 fail → try the VFX C API. If that fails too, Phases 2 and 3 use DLSS and NIS/FSR only. |
| 2 | **S1, S2** | S1 reports no FG → Phase 4 keeps DLSS SR only |
| 3–4 | **S3** (+ S3b if needed), **S4** | S3 fails after S3b → **drop DLSS-G** and use VFX frame generation (PLAN.md already has this backup) |
| 5 | **S5, S6, H1** | S6 fails → shared-memory capture (slower but works). H1 fails → SDR only for now. |
| 6 | S7, S8, **F1** | F1 looks bad → DLSS frame gen stays an experimental option |

**At the end:** a filled-in results table in [README.md](README.md), an updated PLAN.md with any
changes from the failures, and a start on Phase 1.

## Open items before starting

1. Install the Stage 0 packages.
2. Capture card: available or not (M6, S8)?
3. Monitor: confirm it is HDR-capable (M3, V4, H1).

## References

- NVIDIA DLSS SDK (tag `v310.9.16`, Linux libraries in `lib/Linux_x86_64/`): https://github.com/NVIDIA/DLSS
- NVIDIA VFX Python samples: https://github.com/NVIDIA-Maxine/nvidia-vfx-python-samples
- NVIDIA VFX SDK, Video Super Resolution: https://docs.nvidia.com/maxine/vfx/latest/Filters/VideoSuperResolution.html
- NVIDIA VFX SDK 1.3 Linux performance: https://docs.nvidia.com/maxine/vfx/1.3.0/LinuxVFXSDK/PerformanceReference.html
- NVIDIA DLSS / Smooth Motion / Reflex on Linux: https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/latest/gaming.html
- dxvk-nvapi (DRS settings): https://github.com/jp7677/dxvk-nvapi
- NVAPI driver settings header (`NGX_DLSSG_MULTI_FRAME_COUNT` values): https://github.com/NVIDIA/nvapi/blob/main/NvApiDriverSettings.h
- vkd3d-proton issue #3292 (Dynamic Multi Frame Gen): https://github.com/HansKristian-Work/vkd3d-proton/issues/3292
- vsr-player (VFX SDK on Linux, prior art): https://github.com/zhangmq/vsr-player
- Veyra (reference design, GPL-3.0): https://github.com/Likely7/Veyra-NRVideo
