# V2: Do VFX VSR's denoise / deblur modes clean up compressed video?

- **Result:** ⚠️ partial: both modes run fast and denoise softens blocking a little, but **neither
  measurably restores the original** on these clips. Deblur makes blocking worse.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, VFX SDK 1.3.0, RTX 5080
- **How to run:** `tools/spikes/v2-vfx-denoise-deblur/run.sh` (uses `scripts/quality.sh`)

## Method

Inputs are the 1080p Sintel reference re-encoded **at the same resolution**, so the original is a
true reference (higher PSNR / SSIM / VMAF = closer to the original):

- `vlq_1080p`: 600 kbit/s H.264, heavily blocky (made by this script)
- `lq_1080p`: ~3.2 Mbit/s H.264, already good (VMAF ~95)

Each was run through NVIDIA's sample with `--scale 1` and each mode; the sample writes HEVC at
16 Mbit/s (close to transparent). Frame alignment was checked: outputs match inputs at offset 0.

## Quality vs the original (higher is better)

| Input | Processing | PSNR (dB) | SSIM (Y) | VMAF |
|---|---|---|---|---|
| vlq_1080p | none (input) | 33.10 | 0.9114 | 46.0 |
| vlq_1080p | DENOISE_LOW | 33.05 | 0.9140 | 43.4 |
| vlq_1080p | DENOISE_MEDIUM | 33.03 | 0.9130 | 44.0 |
| vlq_1080p | DENOISE_HIGH | 33.05 | 0.9127 | 44.4 |
| vlq_1080p | DENOISE_ULTRA | 33.04 | 0.9127 | 44.0 |
| vlq_1080p | DEBLUR_LOW | 32.35 | 0.8905 | 53.2 |
| vlq_1080p | DEBLUR_HIGH | 32.44 | 0.8908 | 54.5 |
| lq_1080p | none (input) | 45.82 | 0.9874 | 94.7 |
| lq_1080p | DENOISE_LOW | 43.34 | 0.9840 | 89.9 |
| lq_1080p | DENOISE_MEDIUM | 43.85 | 0.9855 | 91.3 |
| lq_1080p | DENOISE_HIGH | 44.12 | 0.9857 | 91.9 |
| lq_1080p | DENOISE_ULTRA | 44.04 | 0.9859 | 91.5 |
| lq_1080p | DEBLUR_LOW | 40.44 | 0.9720 | 98.8 |
| lq_1080p | DEBLUR_HIGH | 41.44 | 0.9768 | 98.2 |

## GPU-only time per 1080p frame

| Mode | ms / frame | Max fps |
|---|---|---|
| DENOISE_LOW | 1.00 | 999 |
| DENOISE_MEDIUM | 1.56 | 642 |
| DENOISE_HIGH | 3.21 | 312 |
| DENOISE_ULTRA | 5.62 | 178 |
| DEBLUR_LOW | 1.00 | 1001 |
| DEBLUR_MEDIUM | 1.72 | 582 |
| DEBLUR_HIGH | 3.25 | 308 |
| DEBLUR_ULTRA | 5.64 | 177 |

## Visual check (by Claude; user to confirm)

Crops (640x360 from the centre). Top: original | 600 kbit/s input. Bottom: DENOISE_HIGH | DEBLUR_HIGH.

- [grid_f150.jpg](grid_f150.jpg): fast motion. All processed versions keep the 16x16 blocks; no
  detail comes back.
- [grid_f225.jpg](grid_f225.jpg): smooth sky gradient. **DENOISE_HIGH visibly softens the block
  edges**; DEBLUR_HIGH makes the blocks *more* visible.

## Surprises / gotchas

- Denoise does not raise PSNR/SSIM/VMAF on either input; on the good input it slightly lowers them
  (it removes some real detail along with noise).
- **Deblur raises VMAF but lowers PSNR/SSIM**: VMAF is known to reward sharpening; the visual check
  shows deblur emphasises compression blocks. Don't trust VMAF alone for sharpening effects.
- My first "heavily compressed" clips (`lq_*` from fetch-media.sh) turned out to be good quality
  (VMAF ~95); fetch-media.sh comments were corrected and this test makes its own 600 kbit/s clip.
- The first crop attempt used time-based seeking and a black frame; crops now use frame indices
  150 and 225.

## What this means for PLAN.md

- Denoise/deblur are **not** a fix for badly compressed streams. Offer DENOISE_LOW/MEDIUM as an
  optional "smooth blocking" toggle; keep DEBLUR off by default.
- Artifact reduction for low-bitrate input should come from the **upscaling** modes (which NVIDIA
  says include artifact reduction); V5 checks that.
- Use PSNR + SSIM + visual crops for quality decisions, not VMAF alone.
