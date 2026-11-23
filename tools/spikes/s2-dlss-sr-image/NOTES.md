# S2: Can our own Vulkan app upscale with DLSS Super Resolution?

- **Result:** ✅ pass: DLSS SR runs in a standalone Vulkan app, **~0.44–0.62 ms per frame** to 1080p,
  and with correct jitter it reconstructs real detail (**+5–6.5 dB PSNR over Lanczos**)
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, DLSS SDK v310.9.16 (`libnvidia-ngx-dlss.so.310.9.16`), RTX 5080
- **How to run:** build spikes, then `tools/spikes/s2-dlss-sr-image/run.sh`
  (uses [`make_jittered.py`](make_jittered.py) with the VFX venv's Python + torch to make inputs)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Method

Like a game rendering for DLSS: 32 low-resolution "renders" of one still 1080p Sintel frame
(frame 150), each sampled at a different sub-pixel offset (Halton 2,3 jitter in [-0.5, 0.5) low-res
pixels). Motion vectors are zero (static scene), depth is a flat 0.5. DLSS SR runs on the frames in
order (it accumulates detail over time), RGBA8 LDR input, `MVLowRes` flag, output RGBA8 1920x1080.
Output after frame 1 and after frame 32 is compared to the true 1080p frame; baselines are ffmpeg
bicubic/Lanczos of one unjittered low-res frame. GPU time per `EvaluateFeature` from Vulkan timestamps.

## Results (PSNR / SSIM vs the true 1080p frame)

| Input | Method | After 1 frame | After 32 frames | GPU ms / eval |
|---|---|---|---|---|
| 540p | ffmpeg Lanczos | 43.42 / 0.9940 | – | – |
| 540p | DLSS preset K, Performance | 42.48 / 0.9932 | **48.16 / 0.9976** | 0.48 |
| 540p | DLSS preset M, Performance | 45.28 / 0.9956 | **49.91 / 0.9977** | 0.44 |
| 720p | ffmpeg Lanczos | 46.18 / 0.9970 | – | – |
| 720p | DLSS preset K, Quality | 44.35 / 0.9958 | **49.26 / 0.9982** | 0.48 |
| 720p | DLSS preset M, Quality | 45.36 / 0.9966 | **50.12 / 0.9980** | 0.62 |

(with the correct jitter sign; see below). First evaluation after creation takes ~12 ms (warm-up).

## Visual check (by Claude; user to confirm)

- [zoom3x_ref_lanczos_dlssM.jpg](zoom3x_ref_lanczos_dlssM.jpg): 3x zoom on the dragon's claws:
  reference | Lanczos | DLSS M. DLSS edges are clearly crisper than Lanczos and close to the reference.
- [grid_540p_ref-lanczos_dlssK-dlssM.jpg](grid_540p_ref-lanczos_dlssK-dlssM.jpg): top reference |
  Lanczos, bottom DLSS K | DLSS M.

## Surprises / gotchas

- **Jitter sign matters.** With `InJitterOffset = -(sample offset from the pixel centre)` DLSS gains
  +5–6.5 dB; with the opposite sign it is *worse* than Lanczos (37–41 dB) because it accumulates
  samples in the wrong place. Convention for Nitid: pass the **negative** of the sampling offset.
- Preset M (the default for Performance mode) beat preset K here, even at Quality (720p).
- **This test is the ideal case** (perfect jitter, static scene, exact zero motion). For video,
  frames are not jittered and motion has to be estimated (S4), so real gains will be much smaller.
  Without jitter, DLSS can only add temporal anti-aliasing / reconstruction from motion, not this
  much detail. F1 must measure that.
- `VK_FORMAT_R8G8B8A8_UNORM` input/output (LDR, no `IsHDR`) worked as-is.

## What this means for PLAN.md

DLSS SR works natively on Linux in our own Vulkan code, and it is cheap (<0.7 ms to 1080p). Whether it
helps on *video* (no jitter, estimated motion) is still open and is tested in F1 against VFX VSR.
