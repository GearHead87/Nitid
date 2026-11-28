# 0.5 step 2: DLSS Super Resolution as an upscaler (no motion vectors yet)

- **Result:** ✅ works, ⚠️ **not better than bicubic without motion vectors.** DLSS runs in Nitid's pipeline as a
  fourth upscaler (presets Auto/J/K/L/M, up to 3x, DLAA at the same size, history reset at scene cuts) at
  0.7–0.9 ms per frame to 1080p and 1.9–2.3 ms to 4K, a little cheaper than VSR. But with zero motion vectors it
  scores **below bicubic** (PSNR −0.1 to −2.5 dB, VMAF −0 to −6): DLSS is a temporal upscaler and, told that nothing
  moves, it blends earlier frames into moving content. Step 3 gives it motion from optical flow.
- **Date:** 2026-11-28
- **Versions:** driver 620.73.04, DLSS 310.9.16, VFX 1.3.0, RTX 5080, KDE Wayland 165 Hz
- **How to run:** `tests/v0.5/02-dlss-upscaling/run.sh` (~6 min)
- **Output:** [output-2026-11-28.md](output-2026-11-28.md); crop of lq_540p frame 100, bicubic | VSR HIGH | DLSS M |
  real 1080p: [crop_bicubic-vsr-dlss-real.jpg](crop_bicubic-vsr-dlss-real.jpg); settings panel (Nitid's own window
  image): [ui_settings_dlss.jpg](ui_settings_dlss.jpg)

## How it works

`dlss::SuperRes` (libnitid/src/dlss/super_res.*): the decoded frame is copied into a buffer shared between CUDA and
Vulkan; one Vulkan submission on the pipeline thread's queue copies it into the colour image, runs DLSS (zero jitter,
flat depth, zero motion for now) and copies the result into a second shared buffer, which CUDA copies into the
frame ring. The quality mode follows the actual scale (≤1.55x Quality, ≤1.8x Balanced, ≤2.05x Performance, else
Ultra Performance, 1x DLAA); the preset is the user's choice or NVIDIA's default for that mode. The history is
reset on the first frame, after seeks and at Nitid's detected scene cuts. Settings that don't change DLSS's sizes
or preset (e.g. frame generation) keep its history.

## Results

### A. Quality, 10 s clips upscaled to 1080p vs the real 1080p video

| Source | bicubic in Nitid | NIS 0.5 | VSR HIGH | DLSS J | DLSS K | DLSS L | DLSS M |
|---|---|---|---|---|---|---|---|
| 540p (2x), PSNR / VMAF | 40.11 / 79.9 | 40.29 / **90.0** | **42.61** / **90.1** | 37.77 / 75.1 | 37.60 / 74.3 | 39.97 / 80.3 | 39.07 / 79.2 |
| 720p (1.5x), PSNR / VMAF | 40.26 / 86.3 | 39.93 / **95.6** | **41.42** / 91.6 | 39.04 / 80.8 | 38.55 / 79.5 | 40.10 / 84.2 | 38.60 / 81.7 |
| compressed 540p, PSNR / VMAF | 36.52 / 53.3 | 36.62 / **63.4** | **37.44** / 62.9 | 35.53 / 51.0 | 35.39 / 50.5 | 36.55 / 54.7 | 35.78 / 52.6 |

Preset L (the heaviest, NVIDIA's Ultra Performance default) is the least bad: about bicubic. The crop shows the
same: DLSS M is softer than VSR, with motion smear on the moving parts.

### B. Same size (DLAA): compressed 1080p shown at 1080p

| Method | PSNR | VMAF | ms |
|---|---|---|---|
| plain | **42.30** | **94.4** | 0 |
| VFX denoise medium | 41.94 | 92.4 | 1.57 |
| DLAA | 40.45 | 88.3 | 0.95 |

Without motion vectors DLAA blurs moving content too.

### C. Scene cuts (Sintel trailer, 540p → 1080p, preset M; 10 cuts)

| DLSS history at cuts | PSNR, 4 frames after each cut | Whole trailer |
|---|---|---|
| **reset** (Nitid) | **43.12** | **39.51** |
| kept (`--no-scene-cuts`) | 38.69 | 39.33 |

The reset matters: without it the old shot bleeds into the first frames of the new one (−4.4 dB there).

### D. Speed (bench, per source frame, CUDA ↔ Vulkan copies included)

| Input → output | VSR HIGH | DLSS (mode) |
|---|---|---|
| 540p → 1080p | 0.76 ms | **0.72 ms** (Performance 2x) |
| 720p → 1080p | 1.26 ms | **0.77 ms** (Quality 1.5x) |
| 1080p → 4K | 2.82 ms | **2.27 ms** (Performance 2x) |
| 720p → 4K | **1.45 ms** | 1.93 ms (Ultra Performance 3x) |
| 1080p → 1080p | – | 0.82 ms (DLAA) |

### E. Window (540p in 1920x1080, frame gen MEDIUM per refresh, 5 s, twice each)

VSR HIGH and DLSS both 165 fps, 0 missed refreshes (one DLSS run had 12: the usual desktop noise), 0 skipped.

## Surprises / gotchas

- **The script's first version paired frames by timestamp across two time bases** (`setpts=N` on a 1/1000 and a
  1/12288 stream) and reported a nonsense 22 dB for section C. Fixed to the `quality.sh` pairing
  (`settb=AVTB,setpts=N*40000`) and PSNR from the mean MSE (a mean of per-frame PSNR is dominated by black title
  frames); section C in the output is that re-run.
- DLSS's mode name is only a tuning hint: sizes are explicit, so any window size works (up to 3x; larger windows get
  the rest from the display blit, like NIS above 2x).
- Stats: new `Stats::upscaler_mode` ("Performance 2.00x", "DLAA", ...), shown in the settings panel, the stats panel
  and the bench report.

## What this means

DLSS SR needs motion vectors to be useful on video; step 3 adds optical flow and re-measures. Until then VSR stays
the default and DLSS is opt-in.
