# F1: Veyra's approach end to end: optical flow → motion vectors → DLSS-G. Is it good?

- **Result:** ⚠️ **works, but not better.** The full pipeline runs natively (hardware optical flow →
  motion vectors → DLSS-G, 2x/4x/6x). But motion vectors estimated from video **do not improve full
  DLSS-G** (usually slightly worse than zero vectors, with blocky edge artifacts), and **VFX Video Frame
  Generation stays the best quality** on these clips. Estimated vectors *do* make DLSS-G "lite" mode
  useful: about the same quality as full DLSS-G at a fraction of the cost.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, DLSS SDK v310.9.16, VFX SDK 1.3.0, RTX 5080
- **How to run:** build spikes, run `tools/spikes/s3-dlssg-standalone/run.sh` once (raw frames), then
  `tools/spikes/f1-flow-dlssg/run.sh` (~45 min)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Method

Same as S3 (rebuild dropped real frames from 97-frame Sintel 1080p24 / Drift 360p60 sequences, luma
PSNR vs the real frames), plus motion vectors from `VK_NV_optical_flow` (S4): input = current rendered
frame, reference = previous rendered frame (that is, "current → previous" in pixels), 4x4 grid MEDIUM
("nvof4") or 1x1 SLOW ("nvof1"), expanded to a full-resolution `R16G16_SFLOAT` image.

## 1. Motion-vector convention (important for Nitid)

Sintel 2x, NVOF 4x4. Lite mode blends unless it can use the vectors, so it shows clearly what DLSS-G reads:

| `mvecScale` | Sign | Full DLSS-G | Lite DLSS-G |
|---|---|---|---|
| 1 (pixels) | +1 | 39.12 | **39.01** (blend is 36.92) |
| 1 (pixels) | −1 | 37.61 | 34.62 |
| 1/width, 1/height | ±1 | 39.78 / 39.77 (= zero vectors) | 36.49 / 36.49 (= zero vectors) |

**Correct convention: motion vectors in pixels, `mvecScale = 1`, NVOF output used as-is (input = current,
reference = previous).** The header comment ("normalize ... to [-1,1]") is misleading; the DLSS-FG
programming guide is right. With `1/size` the vectors are effectively ignored. S3's zero-vector results
are unaffected.

## 2. Results: mean luma PSNR (dB) of rebuilt frames

**Sintel 1080p24** (VFX VFG on the same frames from S3)

| Multiplier | Blend | DLSS-G zero | DLSS-G + NVOF 4x4 | DLSS-G + NVOF 1x1 | Lite + NVOF 4x4 | Lite + NVOF 1x1 | VFX MEDIUM | **VFX HIGH** |
|---|---|---|---|---|---|---|---|---|
| 2x | 36.92 | 39.78 | 39.12 | 39.08 | 39.01 | 39.12 | 39.73 | **40.95** |
| 4x | 33.48 | 35.76 | 35.54 | 35.31 | 35.82 | 35.63 | 37.02 | **37.52** |
| 6x | 29.55 | 31.28 | 31.75 | 31.49 | 32.30 | 31.99 | 33.65 | **34.18** |

**Drift 360p60**

| Multiplier | Blend | DLSS-G zero | DLSS-G + NVOF 4x4 | DLSS-G + NVOF 1x1 | Lite + NVOF 4x4 | Lite + NVOF 1x1 | **VFX MEDIUM** | VFX HIGH |
|---|---|---|---|---|---|---|---|---|
| 2x | 25.99 | 29.09 | 27.75 | 28.26 | 27.36 | 28.05 | **29.26** | 29.25 |
| 4x | 24.10 | 27.16 | 26.41 | 26.75 | 26.06 | 26.52 | **27.51** | 27.47 |
| 6x | 23.18 | 25.74 | 25.71 | 25.86 | 25.44 | 25.69 | **26.51** | 26.50 |

Optical flow time per real frame (NVOF 4x4 MEDIUM): ~0.8–1.2 ms at 1080p, ~0.12 ms at 360p.
DLSS-G lite costs ~0.1 ms per generated frame at 1080p (S3) vs ~0.4–1.1 ms for full DLSS-G.

## Visual check (by Claude; user to confirm)

[grid_f094_x4_real-zero_nvof4-lite.jpg](grid_f094_x4_real-zero_nvof4-lite.jpg) (Sintel 4x, middle
generated frame). Top: real frame | DLSS-G zero vectors. Bottom: DLSS-G + NVOF 4x4 | lite + NVOF 4x4.

- With estimated vectors the **tower edges and the spear tear into 4x4 blocks**: DLSS-G trusts the
  vectors as if they were exact game data, so blocky/noisy flow becomes visible artifacts.
- Zero vectors give a cleaner background (DLSS-G's own internal flow handles the camera pan).
- The fast dragon is smeared in every DLSS-G variant.

## Why this happens (interpretation)

DLSS-G is built for exact, per-pixel engine motion vectors plus real depth. Video gives neither: optical
flow is blocky (4x4), noisy in flat areas, wrong at occlusions, and the depth is fake (flat). DLSS-G then
"trusts" these vectors. VFX VFG is trained for exactly this case (two video frames, nothing else).

## What this means for PLAN.md

- **Phase 4 (Veyra-style DLSS on video) is technically possible but has low value.** Default frame
  generator for video: **VFX Video Frame Generation** (MEDIUM for 60 fps content, HIGH for films,
  own shot-change detection; see V3).
- Keep **DLSS-G lite + NVOF 4x4** as an optional "max frame rate / lowest cost" mode for high multipliers
  (e.g. 60 → 165 Hz), where its cost (~0.1 ms per generated frame + ~1 ms flow per real frame) beats VFX.
- Possible future improvements (not tested): flow confidence masking (NVOF cost output), forward/backward
  consistency, smoothing vectors before DLSS-G, real depth from a monocular depth model.
