# 0.3 test 03: optional VFX clean-up (denoise / deblur at the video's own size)

**Question:** when no upscaling is needed (the window is not larger than the video), do VSR's same-size denoise /
deblur models make the picture closer to the original, and what do they cost?

**Run:** `tests/v0.3/03-cleanup/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).

## Result: ✅ works, ⚠️ little measurable benefit (kept as an option, off by default)

Same finding as spike V2 (NVIDIA's sample), now measured through Nitid:

| Clip vs original 1080p | PSNR | VMAF |
|---|---|---|
| vlq_1080p (600 kbit/s, blocky), off | 32.88 | 45.6 |
| vlq_1080p, denoise high | 32.91 (+0.03) | 44.8 |
| vlq_1080p, deblur high | 32.28 (−0.6) | **55.0** (+9.4) |
| lq_1080p (~3.2 Mbit/s), off | 42.30 | 94.4 |
| lq_1080p, denoise high | 41.98 (−0.3) | 92.9 |
| lq_1080p, deblur low | 39.07 (−3.2) | **99.2** (+4.7) |

- **Denoise** changes almost nothing on these clips (±0.1 dB on the blocky one, slightly worse on the clean one).
- **Deblur** sharpens: VMAF rises a lot (it rewards sharpness) while PSNR / SSIM fall, i.e. it adds edge
  contrast rather than recovering detail. On the blocky clip it also sharpens the blocks (seen in V2).
- **Speed:** 1080p denoise medium 1.5 ms, ultra 5.7 ms, deblur high 3.3 ms; 4K denoise medium 6.0 ms, ultra
  22.5 ms, deblur high 12.8 ms. In the window, 4K60 with denoise medium + frame gen MEDIUM per refresh runs at
  165 fps (render-ahead keeps it smooth).

## Use

Settings → "Clean-up (video at its own size)": Off / Denoise / Deblur + strength; `--cleanup denoise-high` etc.
in both apps. It runs only when nothing upscales (e.g. a 4K video in a smaller window, or 1080p video on a 1080p
screen); while upscaling, the setting says why it is not running (VSR's upscaling models already clean up).
It is a matter of taste: deblur looks crisper, which is what VMAF shows; it is not "more accurate".
