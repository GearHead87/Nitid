# S4: Does `VK_NV_optical_flow` give sensible motion vectors fast?

- **Result:** ✅ pass: accurate (sub-pixel on a known shift), useful on real frames (+10.7 dB warp gain on
  Sintel), and **< 1 ms at 1080p** with a 4x4 grid (0.75 ms MEDIUM / 0.54 ms FAST)
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, Vulkan 1.4.357, RTX 5080
- **How to run:** build spikes, then `tools/spikes/s4-nvof-pair/run.sh`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Capabilities (RTX 5080)

| Item | Value |
|---|---|
| Output grid sizes | 1x1, 2x2, 4x4 |
| Hints / cost / bidirectional / global flow | all supported |
| Size range | 32x32 – 8192x8192, up to 32 regions of interest |
| Input formats | `R8_UNORM`, `B8G8R8A8_UNORM` (44), `G8_B8R8_2PLANE_420_UNORM` (NV12) |
| Flow format | `R16G16_SFIXED5_NV` (S10.5 fixed point, value / 32 = pixels) |
| Queue | separate queue family **5** (transfer + sparse + optical flow); main graphics/compute family 0 |

Images for optical flow are created with `VkOpticalFlowImageFormatInfoNV` in the `VkImageCreateInfo`
`pNext` chain; here they are shared between both queue families (`VK_SHARING_MODE_CONCURRENT`).

## Accuracy: reference = input shifted by exactly (+12, −5) px

| Clip | Grid | Mean flow | Median error | Within 0.5 px |
|---|---|---|---|---|
| Sintel 1080p | 1x1 | (11.69, −4.83) | 0.03 px | 92.9 % |
| Sintel 1080p | 4x4 | (10.76, −5.03) | 0.00 px | 85.3 % |
| Drift 360p | 1x1 / 4x4 | (12.00, −5.00) | 0.00 px | 100 % |

Sintel's misses are in the flat black letterbox bars (nothing to track). Convention confirmed: for input
pixel `p`, the reference content is at `p + f(p)`. With input = current frame and reference = previous
frame this is exactly DLSS's "current → previous, in pixels" motion vector.

## Usefulness: warp the previous frame onto the current one (luma PSNR vs the current frame)

| Clip (max motion) | No warp | 1x1 SLOW | 1x1 FAST | 4x4 SLOW | 4x4 FAST |
|---|---|---|---|---|---|
| Sintel 1080p (105 px) | 19.50 | **30.18** | 29.35 | 29.30 | 28.13 |
| Drift 360p (7 px) | 28.15 | **35.16** | 34.80 | 32.63 | 32.99 |

## Speed: GPU ms per execution (median of 20, optical-flow queue)

| Resolution | Grid | SLOW | MEDIUM | FAST |
|---|---|---|---|---|
| 1920x1080 | 1x1 | 8.89 | 8.03 | 3.39 |
| 1920x1080 | 2x2 | 4.16 | 2.50 | 0.95 |
| 1920x1080 | **4x4** | 1.52 | **0.75** | **0.54** |
| 2560x1440 | 1x1 | 24.40 | 14.12 | 6.21 |
| 2560x1440 | 4x4 | 3.35 | 1.38 | 0.94 |
| 3840x2160 | 1x1 | 35.65 | 31.49 | 14.27 |
| 3840x2160 | 4x4 | 6.04 | 2.94 | 2.12 |

It runs on its own hardware block and queue, so it can overlap with graphics/compute work.

## Visual check

- [sintel_frame_and_flow.jpg](sintel_frame_and_flow.jpg): frame | flow colours (white = still; hue =
  direction; saturation = speed). The character is cleanly separated from the panning background;
  only the black bars are noisy.
- [drift_frame_and_flow.jpg](drift_frame_and_flow.jpg)

## Surprises / gotchas

- Per-pixel (1x1) flow is expensive (3–9 ms at 1080p, up to 36 ms at 4K). **4x4 at the input
  resolution** keeps most of the quality (−0.9 dB on Sintel) for < 1 ms. Run flow at the *source*
  resolution (before upscaling): DLSS-G accepts low-res motion vectors.
- Accuracy is best on textured content; flat areas (black bars, sky) give noisy vectors. A confidence
  mask (cost output, supported) or forward/backward check should down-weight them.
- Input can be `R8_UNORM` luma or NV12 directly: decoded video frames could go straight in.

## What this means for PLAN.md

The motion-estimation module is confirmed: `VK_NV_optical_flow`, 4x4 grid, MEDIUM/FAST, on its own
queue, input = current frame, reference = previous frame. Next: F1 feeds these vectors into DLSS-G.
