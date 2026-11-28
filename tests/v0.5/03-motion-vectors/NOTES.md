# 0.5 step 3: motion vectors from optical flow for DLSS Super Resolution

- **Result:** ✅ works, ⚠️ **helps only preset M, and DLSS still doesn't beat VSR.** Hardware optical flow
  (`VK_NV_optical_flow`, 4x4 grid, expanded bilinearly to one vector per pixel) costs 0.53 ms per 540p frame,
  0.7 ms at 720p, 1.1 ms at 1080p. With it, DLSS preset M gains +0.6 to +1.3 dB PSNR and +1.9 to +3.0 VMAF on all
  three clips and becomes the best DLSS preset; J, K and L do not gain (some lose). The best DLSS (M + motion) is
  about bicubic in PSNR (−0.1 to −0.3 dB) with similar or slightly better VMAF, and stays well below VSR HIGH
  (−1.5 to −2.7 dB). DLAA gets worse with motion vectors, so it runs without them.
- **Date:** 2026-11-28
- **Versions:** driver 620.73.04, DLSS 310.9.16, RTX 5080
- **How to run:** `tests/v0.5/03-motion-vectors/run.sh` (~4 min)
- **Output:** [output-2026-11-28.md](output-2026-11-28.md)

## How it works

`gpu::OpticalFlow` (libnitid/src/gpu/optical_flow.*), per source frame, when DLSS upscales:
CUDA makes 8-bit luma → the pipeline thread's queue copies it into one of two luma images (they take turns as
current / previous) → the optical flow queue measures current → previous on a 4x4 grid (MEDIUM, temporal hints
after the first pair) → the grid comes back through a shared buffer → a CUDA kernel expands it bilinearly to
R16G16 half-float vectors per pixel (the nearest-block expansion of spike F1 tore edges into tiles). DLSS's own
submission copies the vectors into its motion image. Flow is reset at seeks and scene cuts (zero motion for the
first frame of a shot, when DLSS resets anyway).

## Results

### Sign / scale check (lq_540p, preset M, PSNR / VMAF; temporary debug switch, since removed)

| Vectors × | −1 | 0 (step 2) | 0.5 | **1** | 2 |
|---|---|---|---|---|---|
| PSNR / VMAF | 38.56 / 79.7 | 39.07 / 79.2 | 39.86 / 81.6 | **39.93 / 81.1** | 39.50 / 80.9 |

+1 (pixels, current → previous, as for DLSS-G in F1) is right.

### A. Quality to 1080p: without (step 2) → with motion vectors, PSNR / VMAF

| Source | DLSS J | DLSS K | DLSS L | **DLSS M** | bicubic in Nitid | VSR HIGH |
|---|---|---|---|---|---|---|
| 540p (2x) | 37.77 → 37.58 / 75.1 → 70.5 | 37.60 → 37.25 / 74.3 → 68.9 | 39.97 → 37.44 / 80.3 → 75.4 | 39.07 → **39.93** / 79.2 → **81.1** | 40.11 / 79.9 | **42.61 / 90.1** |
| 720p (1.5x) | 39.04 → 38.68 / 80.8 → 80.0 | 38.55 → 37.94 / 79.5 → 78.3 | 40.10 → 39.31 / 84.2 → 84.0 | 38.60 → **39.93** / 81.7 → **84.7** | 40.26 / 86.3 | **41.42 / 91.6** |
| compressed 540p | 35.53 → 35.62 / 51.0 → 49.4 | 35.39 → 35.39 / 50.5 → 48.5 | 36.55 → 35.62 / 54.7 → 52.2 | 35.78 → **36.41** / 52.6 → **55.2** | 36.52 / 53.3 | **37.44 / 62.9** |

### B. DLAA (compressed 1080p at 1080p)

| | PSNR | VMAF |
|---|---|---|
| plain (step 2) | **42.30** | **94.4** |
| DLAA preset K, no motion (step 2; NVIDIA's DLAA default) | 40.45 | 88.3 |
| DLAA preset K, with motion | 39.18 | 85.2 |
| DLAA preset M, no motion | 39.18 | 87.5 |

DLAA does not clean up compressed video; it softens it. It stays available (anti-aliasing for sharp, aliased
sources), without motion vectors and with preset K.

### C. Cost (bench)

| Input → output | DLSS | Optical flow | Throughput |
|---|---|---|---|
| 540p → 1080p | 0.77 ms | 0.53 ms | 767 fps |
| 720p → 1080p | 0.81 ms | 0.68 ms | 670 fps |
| 1080p → 4K | 2.28 ms | 1.11 ms | 295 fps |

### D. Window (540p in 1920x1080, DLSS + frame gen MEDIUM per refresh, 5 s, twice)

165 fps, 0 missed refreshes both times.

## Decisions

- **Auto preset = M when upscaling, K for DLAA** (data above; NVIDIA's defaults are K at 1.5x, M at 2x, L at 3x,
  tuned for games with jitter and exact motion).
- **DLAA runs without optical flow** (better and cheaper).
- **VSR stays the default upscaler.** On real, compressed video DLSS without camera jitter and real depth does
  not add detail the way it does in games (spike S2's +6 dB needed 32 jittered renders of a still frame).
  DLSS's advantages here: cost (0.8 ms vs VSR's 1.3 ms at 720p → 1080p) and up to 3x.
- Possible later improvement (not tried): a "bias current colour" mask from optical flow confidence so DLSS drops
  its history where the flow is unreliable (occlusions, fast motion).
