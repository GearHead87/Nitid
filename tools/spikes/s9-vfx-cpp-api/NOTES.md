# S9: Do VFX upscaling (to any size) and VFX frame generation work from C++?

- **Result:** ✅ pass: both work from C++ with plain RGBA8 GPU images. Frame generation output is
  **bit-identical** to NVIDIA's Python wrapper. Upscaling to a non-integer size (1080p → 1440p) works.
- **Date:** 2026-11-25
- **Versions:** driver 620.73.04, VFX SDK 1.3.0 (runtime from the `nvidia-vfx` wheel, C headers from
  `NVIDIA-Maxine/Maxine-VFX-SDK`), CUDA driver API, RTX 5080
- **How to run:** build spikes, then `tools/spikes/s9-vfx-cpp-api/run.sh`
- **Full output:** [output-2026-11-26.md](output-2026-11-26.md) (corrected rerun);
  [output-2026-11-25.md](output-2026-11-25.md) (first run: VSR times wrong, see the correction below)
- **Why:** Nitid 0.1 needs both from C++. V3 only tested frame generation through Python, and the monitor
  here is 2560x1440, so 1080p video needs a 1.33x upscale, not 2x.

## Results

| Case | VSR HIGH → 2560x1440 (ms) | VFG one frame (ms) |
|---|---|---|
| 1080p input, VFG LOW | 2.66 | 0.41 |
| 1080p input, VFG MEDIUM | 2.66 | 1.35 |
| 1080p input, VFG HIGH | 2.66 | 8.90 |
| 1440p input, VFG MEDIUM | 4.57 (1:1) | 1.42 |

Times are single-frame wall-clock including a stream sync (median of 50), from the 2026-11-26 rerun.

> **Correction (2026-11-26):** the first run reported VSR at 0.39 ms. That was wrong: the spike used a
> *non-blocking* CUDA stream, and the VFX SDK does part of its work on CUDA's legacy default stream, so
> `cuStreamSynchronize` returned before VSR had finished. With a blocking stream (`CU_STREAM_DEFAULT`) VSR
> to 1440p takes 2.66 ms, about the same as to 4K (V1: 2.91 ms). Frame generation times were not affected.

| Check (PSNR, dB) | Value |
|---|---|
| C++ VFG MEDIUM vs NVIDIA Python VFG, same settings | **∞ (identical)** |
| VFG LOW / MEDIUM / HIGH (t=0.5) vs real middle frame | 23.7 / 24.6 / 25.3 |
| 50/50 blend vs real middle frame | 21.0 |
| Repeat previous frame vs real middle frame | 18.8 |
| t=0.25 closer to previous, t=0.75 closer to next | 22.0 vs 17.8 / 17.5 vs 21.8 ✅ |
| `ShotChange=1` output vs next frame | ∞ (returns the current frame unchanged) |
| VSR 2560x1440 vs Lanczos 2560x1440 | 43.9 (same picture, sharper) |

This pair has a fast camera pan, so absolute PSNR is low for every method. VFG still beats the blend by
3.6 dB.

## How to call it (C API)

- **VideoSuperRes:** `ImageEncodingMode = 0`, input and output `NvCVImage` RGBA / U8 / CHUNKY / GPU. The
  output size is whatever the output image is: any size, not only 2x/3x/4x.
- **VideoFrameGeneration:**
  - Inputs `SrcImage0` (previous) and `SrcImage1` (current), output `DstImage0`, all RGBA / U8 / CHUNKY / GPU.
  - Set `InputWidth`, `InputHeight`, `Mode` (0 LOW, 1 MEDIUM, 2 HIGH) and
    `AutomaticShotChangeDetectionEnabled = 0`, then call `NvVFX_Load`.
  - Per frame, set `FrameMultiplier = 0` with `Timestep` (0..1) for any time position, or
    `FrameMultiplier = N` with `FrameIndex = k`. Set `ShotChange = 1` on a cut, then call `NvVFX_Run`.
  - Other parameter found in the library: `ShotDetectionThreshold` (untested).

## Surprises / gotchas

- **VFG has no `ImageEncodingMode` parameter.** Setting it fails with -5 ("selector not valid"), unlike VSR.
  The Python wrapper's encoding switch only changes how it converts tensors.
- **Use a blocking CUDA stream (`CU_STREAM_DEFAULT`) with the VFX SDK, never `CU_STREAM_NON_BLOCKING`.**
  With a non-blocking stream VSR's output is not ordered with the caller's work. Found in Nitid 0.1
  (tests/v0.1/02-upscaling): the window showed the *previous* frame, timings were too low, and syncing the
  whole context did not help. A blocking stream or stream 0 fixes both.
- VSR to 1440p costs about the same as to 4K (2.66 vs 2.91 ms): the cost follows the input size (V1).
- **`Timestep` mode means frame gen does not have to be a whole multiplier.** Nitid can generate the frame
  for the exact moment of each display refresh. For example, 60 fps → 165 Hz becomes smooth without judder.

## What this means for PLAN.md

Nitid 0.1 can use both effects directly from C++ through the CUDA driver API. Use timestep-based frame
generation so frames line up with the display refresh (S7: FIFO pacing). Pipeline order: upscale to the
window size first, then generate frames at that size (1080p → 1440p: 2.7 ms per real frame + 1.4 ms per
generated frame).
