# 0.1 step 1: Does plain playback work (file → window, paced to the display)?

- **Result:** ✅ pass up to 1080p60. ⚠️ 4K60 is at the limit of 0.1's CPU colour conversion (planned fix below).
- **Date:** 2026-11-26
- **Setup:** RTX 5080, driver 620.73.04, KDE Plasma Wayland, monitor 2560x1440 @ 165 Hz, FFmpeg 8 (NVDEC),
  window 2048x1152. Upscaling and frame generation off.
- **How to run:** build (`cmake -S . -B build -G Ninja && cmake --build build`), then
  `tests/v0.1/01-playback/run.sh` (opens a window for ~80 s in total)
- **Full output:** [output-2026-11-26.md](output-2026-11-26.md)

## Results (8 s per clip, each clip played twice)

| Clip | Output fps | Missed refreshes (run 1 / 2) | Skipped frames | Decode + convert (ms) |
|---|---|---|---|---|
| synth 1080p60 | 165.0 | 0 / 0 | 0 / 0 | 3.3 |
| drift 360p60 | 165.0 | 0 / 10 | 0 / 0 | 0.6 |
| Sintel 1080p24 | 164.9 | 34 / 2 | 0 / 0 | 3.7 |
| 4K60 HEVC | 165.0 | 39 / 39 | 6 / 3 | 12.4 |
| 4K60 AV1 | 118.0 | 164 / 557 | 2 / 2 | 11.8 |

- Every file plays at the right speed with NVDEC, the window refreshes at the full 165 Hz, and no frames
  are skipped up to 1080p.
- "Missed refreshes" = a new image reached the screen more than 1.5 refreshes after the previous one. At
  1080p they come in **bursts in one of the two runs** (the stats show the whole loop pausing for ~0.3 s),
  which points to the busy desktop rather than steady load. Steady state is 0.
- At 4K, decoding is fast (NVDEC), but **copying each 4K frame to the CPU, converting it to RGBA and
  uploading it again takes ~14 ms** of the 16.7 ms a 60 fps frame has, competing with the desktop.

## Problems found and fixed

1. **Lock at 2/3 of 165 Hz (110 fps)** in about half the runs. One present was queued ahead (as in S7), and
   the "frame shown" event on Wayland arrives late, so the next frame was sometimes submitted too close to
   KWin's deadline. **Fix: two presents queued ahead** (4 swapchain images). Steady 165 fps since; it adds
   one refresh (6 ms) of delay, fine for video.
2. **VFX effects "not available"** (-2 "not yet implemented") when loading the runtime with `dlopen`:
   `libVideoFX` opens the effect libraries by name. **Fix:** preload `libnvVFX*.so` and
   `libnvidia-ngx-*.so*` too, as NVIDIA's Python loader does. No `LD_LIBRARY_PATH` needed any more.
3. **4K AV1 played at 6 fps:** FFmpeg's default AV1 decoder is `libdav1d` (software, no NVDEC).
   **Fix:** pick a decoder for the codec that supports NVDEC (FFmpeg's own `av1`).
4. The C++ `NvCVImage` class in NVIDIA's header calls `NvCVImage_Alloc/Dealloc`. libnitid doesn't link
   the runtime, so it forwards those two calls to it.

## What this means

- Playback, pacing and the Vulkan output path are confirmed for the next steps (upscaling, frame gen).
- **Next fix for 4K:** keep NVDEC frames on the GPU and convert NV12 → RGBA there (FFmpeg on CUDA's
  primary context, a small CUDA kernel). This removes the CPU copy, the conversion and the upload.
  **Done in [05-gpu-colour](../05-gpu-colour/NOTES.md): 4K60 now plays at 165 fps with 0–6 missed refreshes.**
