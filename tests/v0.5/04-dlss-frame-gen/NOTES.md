# 0.5 step 4: DLSS Frame Generation (full and lite, 2x–6x)

- **Result:** ✅ works natively at 2x–6x in files, windows and live capture; ⚠️ **VFX frame generation stays the
  better picture.** DLSS full scores 0.2–1.4 dB below VFX MEDIUM on generated frames; DLSS lite 0.6–2.8 dB below
  (but above full at 1080p 4x/6x). DLSS's strength is cost at high multipliers: **lite 6x = 0.32 ms per generated
  frame at 1080p** (VFX MEDIUM 1.41, HIGH 9.7) and 0.86 ms at 4K, plus ~1.1 ms of optical flow per real frame.
  In a window, 24 fps video gets 6x (144 fps distinct frames on a 165 Hz screen) with 0 missed refreshes.
- **Date:** 2026-11-28
- **Versions:** driver 620.73.04, DLSS 310.9.16, VFX 1.3.0, RTX 5080, KDE Wayland 165 Hz
- **How to run:** `tests/v0.5/04-dlss-frame-gen/run.sh` (~10 min)
- **Output:** [output-2026-11-28.md](output-2026-11-28.md); crop of a generated frame (src_1080p, 4x, frame 150):
  top real | VFX MEDIUM, bottom DLSS full | DLSS lite: [crop_4x_real-vfx-dlss-lite.jpg](crop_4x_real-vfx-dlss-lite.jpg)

## How it works

`dlss::FrameGen` (libnitid/src/dlss/frame_gen.*). NVIDIA's guide requires every in-between frame of a pair to be
generated in order (index 1..n) for each new real frame, so Nitid generates **all frames of a pair in one Vulkan
submission** and keeps them until the pair changes; the pipeline returns the one for the moment asked. If the
previous real frame wasn't the last one DLSS-G saw (start, seek, cut, skipped pair), it is fed first with a reset.
Stationary synthetic camera, flat depth; motion vectors: **lite** gets optical flow (source size, current →
previous; DLSS-G scales them from `RenderWidth`), **full** gets zero vectors (it finds motion itself).

Pacing: DLSS-G makes whole steps (1/m), so the render-ahead thread uses a fixed multiplier. "Per refresh" picks the
largest step that does not exceed the refresh rate (24 fps at 165 Hz → 6x = 144 fps; 60 fps → 2x), so no generated
frame is made and never shown. RTX 40 is limited to 2x (MultiFrameCountMax 1). Live smooth mode uses the largest
step and the nearest generated frame to the moment.

UI / CLI: settings panel "Frame gen: VFX / DLSS / Off", DLSS model Full / Lite, Frames up to 6x (greyed above the
GPU's limit); keys **D** (VFX ↔ DLSS), **H** (DLSS: full ↔ lite), **6** (6x); `--frame-gen dlss|dlss-lite`.

## Results

### A. Quality: rebuild dropped frames (mean luma PSNR of generated frames / VMAF of the whole clip)

| Clip | x | VFX MEDIUM | VFX HIGH | DLSS full | DLSS lite |
|---|---|---|---|---|---|
| Drift 360p60 | 2 | **32.66** / 81.2 | 32.63 / 81.2 | 32.48 / 80.8 | 31.58 / 80.3 |
| | 4 | **30.93** / 68.2 | 30.93 / 68.3 | 30.62 / 67.4 | 30.02 / 67.0 |
| | 6 | **29.58** / 61.1 | 29.58 / 61.3 | 29.17 / 59.9 | 28.78 / 59.7 |
| src 1080p24 | 2 | 36.03 / 82.6 | **36.15** / 82.9 | 35.40 / 80.7 | 35.09 / 81.0 |
| | 4 | **33.05** / 63.0 | 32.94 / 63.3 | 31.85 / 58.5 | 32.24 / 61.7 |
| | 6 | 30.10 / 50.3 | **30.22** / 51.4 | 28.75 / 45.5 | 29.22 / 50.9 |
| Sintel trailer 1080p24 | 2 | **47.87** / 89.5 | 47.38 / 89.4 | 47.07 / 88.8 | 45.05 / 88.1 |
| | 4 | **45.13** / 80.4 | 44.44 / 80.2 | 44.03 / 77.9 | 43.10 / 78.8 |
| | 6 | **42.39** / 73.0 | 41.64 / 73.3 | 41.20 / 69.4 | 40.53 / 72.5 |

Same ranking as spikes S3 / F1. In the crop, DLSS full smears the fast-moving dragon; lite places it right but
tears its edges into small blocks (flow errors at the silhouette); VFX is closest to the real frame.

Full model with optical flow vectors (temporary debug switch, since removed; generated-frame PSNR):
Drift 2x/4x/6x 32.48 → 31.69 / 30.62 → 30.09 / 29.17 → 28.85 (worse), src 1080p 35.40 → 35.43 / 31.85 → 32.22 /
28.75 → 29.03 (better). Mixed, so full keeps zero vectors (as F1 found).

### B. Speed (ms per generated frame)

| Frames | x | VFX MEDIUM | VFX HIGH | DLSS full | DLSS lite | + flow per real frame (lite) |
|---|---|---|---|---|---|---|
| 1080p | 2 | 1.41 | 9.62 | 1.43 | 1.06 | 1.16 |
| | 4 | 1.42 | 9.62 | 0.85 | 0.46 | 1.10 |
| | 6 | 1.41 | 9.72 | 0.71 | **0.32** | 1.10 |
| 4K | 2 | 1.70 | 10.16 | 2.54 | 1.95 | 1.12 |
| | 4 | 1.70 | 10.86 | 1.72 | 1.03 | 1.10 |
| | 6 | 1.70 | 10.30 | 1.53 | **0.86** | 1.12 |

(4K row: 1080p source upscaled with DLSS; flow runs at the source size.)

### C. Window (24 fps 540p in 1920x1080, per refresh, 5 s, twice)

| | Used | Display | Missed | Late |
|---|---|---|---|---|
| VFX MEDIUM | per refresh, 1.4 ms | 165 fps | 31 / 0 | 1 / 2 |
| DLSS full | 6x, 0.8 ms | 165 fps | 0 / 0 | 0 / 1 |
| DLSS lite | 6x, 0.4 ms | 165 fps | 0 / 0 | 1 / 1 |

(The 31 missed refreshes on one VFX run are the usual desktop noise; its repeat had 0.)

### D. Live (synthetic 1280x720 ball at 30 fps, smooth mode, 8 s)

| | Shown (generated) | Capture → screen | Display |
|---|---|---|---|
| VFX MEDIUM | 1270 (922) | 58.9 ms | 165 fps |
| DLSS full | 1280 (865) | 59.3 ms | 165 fps |
| DLSS lite | 1279 (844) | 59.6 ms | 165 fps |

## Surprises / gotchas

- First quality script averaged MSE over generated frames: frames held at scene cuts dominate it and every method
  scored the same (~25 dB on Drift). Switched to the mean of per-frame PSNR, as S3 / F1 did.
- DLSS-G's in-order rule means a seek or skipped pair costs one extra "priming" evaluation (with reset).

## What this means

VFX frame generation stays the default. DLSS frame generation is the cheap option for high multipliers: lite at
4x–6x costs a fraction of VFX HIGH, useful for 4K or slower GPUs (RTX 50; RTX 40 gets 2x only).
