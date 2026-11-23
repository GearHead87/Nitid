# S3: Can DLSS Frame Generation (DLSS-G) make frames outside a game, in our own Vulkan app?

- **Result:** ✅ **yes**: DLSS-G creates and generates frames at **2x, 4x and 6x** in a standalone native
  Vulkan app (no game, no Proton, no Streamline, no swapchain), using Veyra's approach.
  With *zero* motion vectors its quality is similar to VFX VFG LOW/MEDIUM at 2x and 1–3 dB below
  VFX VFG HIGH at 4x/6x. Real motion vectors (S4 → F1) are the next step.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, DLSS SDK v310.9.16 (`libnvidia-ngx-dlssg.so.310.9.16`), RTX 5080
- **How to run:** build spikes, then `tools/spikes/s3-dlssg-standalone/run.sh`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Method

- Inputs: 97 real frames each from the Sintel 1080p24 clip and the Drift 360p60 clip (raw RGBA8).
- Every `step`-th real frame is given to DLSS-G as the "backbuffer"; DLSS-G generates `step-1` frames in
  between (`multiFrameCount = step-1`, `multiFrameIndex = 1..step-1`), which are compared with the real
  frames that were dropped (luma PSNR). Steps 2, 4, 6 = 2x, 4x, 6x.
- Synthetic "game" data, written from scratch after Veyra's idea: stationary pinhole camera (90° FOV,
  identity clip→prev-clip), flat depth 0.5, **zero motion vectors**, `cameraMotionIncluded = true`,
  frame IDs increasing, resources declared always/never provided. No HUD/UI inputs.
- Baselines: repeat the previous real frame; linear blend. Comparison: VFX Video Frame Generation on the
  same frames (`compare_vfx.py`, same PSNR formula, 8-bit output).
- Timing: a second pass batches 8 real frames per command buffer (no read-back) with a timestamp around
  every `EvaluateFeature`.
- `lite=1` sets the create-time parameter **`DLSSG.LiteCandidateBlendingEnabled`** (present in the Linux
  310.9.16 library; found via Veyra's notes as a "performance mode").

## Quality: mean luma PSNR (dB) of rebuilt frames

**Sintel 1080p24 (film, camera moves, fast objects)**

| Multiplier | Repeat | Blend | DLSS-G | DLSS-G lite | VFX LOW | VFX MEDIUM | VFX HIGH |
|---|---|---|---|---|---|---|---|
| 2x | 33.56 | 36.92 | 39.78 | 36.49 | 39.93 | 39.73 | **40.95** |
| 4x | 30.03 | 33.48 | 35.76 | 33.49 | 36.64 | 37.02 | **37.52** |
| 6x | 26.94 | 29.55 | 31.28 | 29.61 | 32.70 | 33.65 | **34.18** |

**Drift 360p60 (racing footage)**

| Multiplier | Repeat | Blend | DLSS-G | DLSS-G lite | VFX LOW | VFX MEDIUM | VFX HIGH |
|---|---|---|---|---|---|---|---|
| 2x | 23.96 | 25.99 | 29.09 | 25.99 | 28.88 | **29.26** | 29.25 |
| 4x | 21.95 | 24.10 | 27.16 | 24.10 | 27.04 | **27.51** | 27.47 |
| 6x | 21.13 | 23.18 | 25.74 | 23.18 | 25.95 | **26.51** | 26.50 |

(VFX shot-change detection off, except: VFX MEDIUM with detection on scored 37.84 / 35.71 / 32.84 on
Sintel and 29.58 / 27.82 / 27.29 on Drift; detection helps on Drift, hurts on Sintel; see V3.)

## Speed: GPU ms per generated frame (batched pass)

| Clip | Multiplier | DLSS-G median | DLSS-G wall / gen. frame | Lite median |
|---|---|---|---|---|
| Sintel 1080p | 2x | 1.13 | 4.36 (incl. 8 MB upload + outliers) | 0.77 |
| Sintel 1080p | 4x | 0.42 | 0.67 | 0.12 |
| Sintel 1080p | 6x | 0.41 | 0.57 | 0.11 |
| Drift 360p | 2x | 0.77 | 0.83 | 0.32 |
| Drift 360p | 4x | 0.38 | 0.54 | 0.02 |

Separate repeat runs (1080p, 2x): median 1.0–1.24 ms, lite 0.64 ms. The first generated frame of each
group costs ~1 ms at 1080p; extra frames in multi-frame mode ~0.4 ms each.

## Visual check (by Claude; user to confirm)

Crops: real dropped frame | DLSS-G (zero motion) | linear blend.

- [real-dlssg-blend_f094_x4.jpg](real-dlssg-blend_f094_x4.jpg) (4x, middle frame): DLSS-G moves the
  background correctly with the camera pan (blend shows doubled towers), but the fast red dragon becomes a
  faint smear.
- [real-dlssg-blend_f071_x2.jpg](real-dlssg-blend_f071_x2.jpg) (2x, very fast whip-pan): DLSS-G warps the
  face into an in-between position; blend shows two faces.
- [real-dlssg-blend_f083_x2.jpg](real-dlssg-blend_f083_x2.jpg) (2x).

## Surprises / gotchas

- **It just works:** `NGX_VK_CREATE_DLSSG` / `NGX_VK_EVALUATE_DLSSG` with plain RGBA8 images, no
  swapchain, no Reflex, no Streamline. The DLSS-FG guide confirms the app owns presentation with raw NGX.
- **With zero motion vectors DLSS-G still finds camera motion itself** (internal optical flow), but
  misses fast objects. VFX VFG, built for video, is equal or better without game data.
- **Lite mode = plain blend without motion vectors** (identical PSNR to the blend baseline). It is only
  useful with real motion vectors. Retest in F1.
- **Timing is noisy.** Submitting and waiting per frame showed 5–9 ms "per evaluation" (GPU idles and
  clocks drop between submits); batched, it is ~1 ms. Occasional single evaluations take 5–10 ms (p90
  outliers), probably other GPU work (desktop compositor). S7 / a real pipeline must check this.
- Sintel's first 97 frames include a dark fade; picked bright frames for the images.

## What this means for PLAN.md

- The biggest risk is cleared: **DLSS-G (including 6x) works natively on Linux in our own app.**
- Without good motion vectors it is not better than VFX VFG, so Phase 4's value depends on F1
  (estimated motion vectors from S4). Until then, **VFX VFG stays the default** frame generator.
- DLSS-G is cheap (~1 ms at 1080p for 2x, ~0.4 ms per extra frame), so 4x–6x is affordable if F1 shows
  better quality.
