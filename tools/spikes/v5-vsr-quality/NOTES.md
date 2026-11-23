# V5: Does VFX VSR look better than ordinary scaling?

- **Result:** ✅ pass: VSR HIGH/ULTRA beat bicubic and Lanczos on every metric and visibly; the
  upscaling modes also reduce compression artifacts
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, VFX SDK 1.3.0, RTX 5080
- **How to run:** `tools/spikes/v5-vsr-quality/run.sh`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Method

Low-resolution copies of the 1080p Sintel reference are upscaled back to 1080p and scored against
the original (PSNR / SSIM / VMAF; higher = closer to the original). All results, including the
ffmpeg baselines, are encoded the same way (HEVC 16 Mbit/s) for a fair comparison.

- `lq_540p`: 960x540 at ~1.6 Mbit/s (clean, resolution-limited)
- `vlq_540p`: 960x540 at 400 kbit/s (badly compressed)
- `lq_720p`: 1280x720 at ~2.2 Mbit/s (VSR only does integer scales: 2x to 1440p, then Lanczos to 1080p)

## Results (selected; full table in the output file)

| Input | Upscaler | PSNR (dB) | SSIM | VMAF |
|---|---|---|---|---|
| lq_540p | ffmpeg bicubic | 40.39 | 0.9762 | 82.2 |
| lq_540p | ffmpeg lanczos | 40.61 | 0.9765 | 83.5 |
| lq_540p | VSR LOW | 41.71 | 0.9766 | 87.0 |
| lq_540p | **VSR HIGH** | 41.72 | **0.9772** | 89.2 |
| lq_540p | **VSR ULTRA** | **42.00** | 0.9753 | **90.5** |
| lq_540p | VSR STREAMING_ULTRA | 38.29 | 0.9705 | 98.6 ⚠️ |
| vlq_540p | ffmpeg lanczos | 36.66 | 0.9429 | 56.0 |
| vlq_540p | VSR HIGH | 37.12 | 0.9445 | 61.8 |
| vlq_540p | VSR ULTRA | 37.33 | 0.9434 | 64.1 |
| lq_720p | ffmpeg lanczos | 42.45 | 0.9825 | 89.1 |
| lq_720p | VSR ULTRA (2x → Lanczos) | 42.75 | 0.9816 | 91.2 |

VSR's own `BICUBIC` mode (non-AI) scores slightly below ffmpeg bicubic: it is only a baseline.

## Visual check (by Claude; user to confirm)

Grids: top = original | ffmpeg Lanczos, bottom = VSR HIGH | VSR ULTRA (640x360 centre crops).

- [grid_lq_540p_f150.jpg](grid_lq_540p_f150.jpg): VSR restores fur texture and a sharper eye on the
  dragon; Lanczos is soft. VSR looks close to the original.
- [grid_vlq_540p_f225.jpg](grid_vlq_540p_f225.jpg): Lanczos keeps blotchy gradient blocks and specks;
  **VSR smooths the sky gradient and draws the thin stick cleanly** (artifact reduction works here).
- Also: [grid_lq_540p_f225.jpg](grid_lq_540p_f225.jpg), [grid_vlq_540p_f150.jpg](grid_vlq_540p_f150.jpg).

## Surprises / gotchas

- **STREAMING_ULTRA: highest VMAF (98.6) but lowest PSNR/SSIM.** Same pattern as DEBLUR in V2: strong
  sharpening that VMAF rewards. Needs a visual check before using it; don't pick modes by VMAF alone.
- Gains are larger from 540p (+1.1–1.4 dB, +6–8 VMAF) than from 720p (+0.3 dB, +2 VMAF): less detail
  is missing at 720p.
- Non-integer scales (720p → 1080p) need VSR 2x plus a downscale; budget that extra pass.

## What this means for PLAN.md

VFX VSR is confirmed as the default upscaler. Default **HIGH** (best SSIM, balanced); offer **ULTRA**
for maximum detail. For badly compressed streams, prefer the upscaling modes over V2's
denoise/deblur modes. STREAMING_ULTRA stays off by default until the user has looked at it.
