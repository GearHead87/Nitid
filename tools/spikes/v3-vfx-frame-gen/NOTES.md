# V3: Does VFX Video Frame Generation work in real time, and how good are the generated frames?

- **Result:** ✅ pass: works at 2x and 4x, real-time at up to 4K in LOW/MEDIUM, clearly better than
  blending. **Important gotcha:** the default MEDIUM mode's automatic shot-change detection
  misfires on fast motion.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, VFX SDK 1.3.0, RTX 5080 (VFG needs Ada or Blackwell)
- **How to run:** `tools/spikes/v3-vfx-frame-gen/run.sh`; follow-ups `medium_check.py`, `crop_examples.py`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## 1. NVIDIA sample, end to end (MEDIUM)

| Input | Multiplier | Output | Output frames | Sample fps (incl. CPU decode + encode) |
|---|---|---|---|---|
| Sintel 1080p24 | 2x | 1080p48 | 479 | 105 |
| Sintel 1080p24 | 4x | 1080p96 | 957 | 52 |
| Drift 360p60 | 2x | 360p120 | 1199 | 436 |
| Drift 360p60 | 4x | 360p240 | 2397 | 191 |

## 2. Speed, GPU-only (ms per generated frame, input pair already on the GPU)

| Resolution | LOW | MEDIUM | HIGH |
|---|---|---|---|
| 1920x1080 | 0.57 | 1.50 | 9.11 |
| 2560x1440 | 0.73 | 1.67 | 9.38 |
| 3840x2160 | 1.31 | 2.25 | 10.34 |

At 4x, three frames are generated per real frame. Example: 1440p60 → 240 fps with MEDIUM needs
3 × 1.67 = 5.0 ms per real frame (budget 16.7 ms). **HIGH (~9–10 ms each) is only practical at 2x
with low source frame rates** (e.g. 24/30 fps films).

## 3. Quality: rebuild real frames that were dropped

Drop frames from a real clip, let VFG rebuild them, compare with the real frames (luma PSNR / SSIM).

**Sintel 1080p24 (film, fast camera moves, several cuts)**

| Test | Method | Mean PSNR (dB) | Worst 5% PSNR | Mean SSIM |
|---|---|---|---|---|
| 2x | repeat previous frame | 29.15 | 16.93 | 0.865 |
| 2x | 50/50 blend | 31.99 | 18.30 | 0.891 |
| 2x | VFG LOW | 35.22 | 21.88 | 0.939 |
| 2x | VFG MEDIUM (default settings) | 32.05 | 17.79 | 0.879 |
| 2x | VFG HIGH | 35.26 | 16.93 | 0.941 |
| 4x | 50/50 blend | 28.98 | 16.61 | 0.863 |
| 4x | VFG LOW | 30.61 | 15.13 | 0.904 |
| 4x | VFG MEDIUM (default settings) | 30.10 | 16.34 | 0.866 |
| 4x | VFG HIGH | 32.01 | 16.34 | 0.910 |

**Drift 360p60 (racing footage, closest to a game)**

| Test | Method | Mean PSNR (dB) | Mean SSIM |
|---|---|---|---|
| 2x | 50/50 blend | 32.22 | 0.869 |
| 2x | VFG LOW / MEDIUM / HIGH | 34.11 / **34.68** / 34.62 | 0.884 / 0.890 / 0.890 |
| 4x | 50/50 blend | 29.43 | 0.848 |
| 4x | VFG LOW / MEDIUM / HIGH | 31.93 / **32.57** / 32.54 | 0.875 / 0.879 / 0.879 |

## 4. Follow-up: the MEDIUM anomaly is shot-change detection

`medium_check.py` (Sintel, 2x, mean PSNR):

| Mode | Shot-change detection on (default) | off |
|---|---|---|
| LOW | 35.22 | 35.22 |
| MEDIUM | **32.05** | **35.55** |
| HIGH | 35.26 | **36.04** |

MEDIUM (and slightly HIGH) treat fast camera motion as a scene cut and fall back to a near-copy of a
neighbouring frame. Worst pairs lose ~10 dB (e.g. frame 99: HIGH 35.7 dB vs MEDIUM 24.7 dB).

Visual: [grid_f99_real-blend_high-medium.jpg](grid_f99_real-blend_high-medium.jpg). Top: real frame |
50/50 blend (double image). Bottom: VFG HIGH, detection off (almost identical to real) | VFG MEDIUM,
detection on (shows a neighbouring camera position).

## Surprises / gotchas

- **Don't trust the default shot-change detection.** Nitid should run its own cut detection
  (e.g. luma-histogram / SAD between frames, or decoder scene info) and pass `shot_change=True`
  itself, with automatic detection off.
- Generated frame count: 240 input frames → 479 at 2x / 957 at 4x (no frame after the last one).
- The sample's MEDIUM-by-default plus automatic detection would hide this problem; measure, don't assume.
- `PIL` is not in the venv; images are written as PPM and converted with ImageMagick.
- My first saved crop (frame 141) was a real scene cut, where holding the previous frame is correct;
  replaced by frame 99.

## What this means for PLAN.md

- VFX frame generation is confirmed for Phase 2. Defaults: **MEDIUM for 60 fps sources, HIGH for
  ≤30 fps films at 2x**, automatic shot-change detection **off**, Nitid's own cut detector on.
- Budget example for 1440p165 output from 1080p60 input: VSR HIGH 2.9 ms + VFG MEDIUM ×2 (3x) ≈ 6.3 ms
  per real frame of 16.7 ms: fits.
- This is the fallback if DLSS-G (S3) does not work, and the comparison baseline for F1.
