# 0.1 follow-up: Does GPU colour conversion fix 4K60 playback, without changing the picture?

- **Result:** ✅ pass: 4K60 now plays at 165 fps in the window (steps 1's problem), decode + convert is
  8–11x faster, and the picture matches the CPU conversion (VMAF 99.6 vs the source).
- **Date:** 2026-11-26
- **Setup:** RTX 5080, driver 620.73.04, CUDA 13.5 (nvcc for PTX only), FFmpeg 8 NVDEC, KDE Plasma Wayland,
  2560x1440 @ 165 Hz
- **How to run:** `tests/v0.1/05-gpu-colour/run.sh` (~1.5 min, two windows)
- **Full output:** [output-2026-11-26.md](output-2026-11-26.md)

## What changed

Before (0.1 shortcut): NVDEC frame → copy to CPU → swscale to RGBA → upload to GPU.
Now: FFmpeg decodes into **Nitid's own CUDA context on a dedicated decoder stream**, and a small CUDA kernel
converts NV12 (8-bit) / P010 (10-bit) to RGBA8 straight into a pooled GPU buffer (BT.601 / 709 / 2020,
limited or full range, bilinear chroma). The kernels are compiled to PTX by `nvcc` at build time and
embedded in libnitid; the driver compiles them for the GPU at run time (no CUDA runtime dependency).
Scene-cut detection uses a 1/4-size luma thumbnail made on the GPU, the only data copied to the CPU.
Software decoding and other pixel formats still use the CPU path.

## A. Same picture? (whole clip, no AI, vs the source)

| Conversion | PSNR | SSIM | VMAF |
|---|---|---|---|
| GPU (new) | 44.84 | 0.9990 | 99.56 |
| CPU (swscale) | 45.58 | 0.9978 | 99.04 |
| GPU vs CPU | 46.63 | 0.9983 | 99.65 |

Both include an RGB → YUV round trip for scoring. The small differences come from chroma interpolation.

## B. Speed (headless, no AI)

| Clip | Decode + convert, GPU / CPU (ms per frame) | Throughput, GPU / CPU |
|---|---|---|
| 1080p24 H.264 | **0.30** / 3.26 | **2348** / 306 fps |
| 4K60 H.264 | **1.40** / 13.05 | **690** / 77 fps |
| 4K60 HEVC | **1.45** / 12.90 | **668** / 76 fps |
| 4K60 AV1 | **1.45** / 13.47 | **312** / 75 fps |

## C. 4K60 in the window (step 1 failed here)

| Clip | Output fps | Missed refreshes (run 1 / 2) | Before (step 1) |
|---|---|---|---|
| 4K60 HEVC | 165.0 | 0 / 0 | 39 / 39 missed |
| 4K60 AV1 | 165.0 | 6 / 2 | 164 / 557 missed, 118 fps |

## D. Scene cuts from the GPU thumbnail

Sintel trailer: detects all 7 real cuts (396, 420, 572, 622, 658, 672, 680) plus 528 (a dark scene
brightening at once, borderline). The title fade-in at 18 that the CPU path flagged is no longer flagged:
4x4 averaging gives a cleaner thumbnail than sampling every 4th pixel.

## Gotchas

- FFmpeg's CUDA device is created with `av_hwdevice_ctx_alloc` + our `cuda_ctx` and `stream`, then
  `av_hwdevice_ctx_init`. With FFmpeg's own context, its frames live in another context; with its default
  stream, our kernel would not be ordered after FFmpeg's copy (the step 2 lesson about streams).
- The decoder stream is non-blocking on purpose: it must not wait for the render thread's VFX work on the
  legacy default stream.
- `struct AVFrame*` inside a namespaced class declares a *new* type; forward-declare `struct AVFrame;` at
  global scope.
