# M1: Is NVENC (H.264 / HEVC / AV1) faster than real time at 4K60?

- **Result:** ✅ pass, all three codecs
- **Date:** 2026-11-23
- **Driver / tools:** 620.73.04, FFmpeg (`ffmpeg-obs` 9.1), RTX 5080
- **How to run:** `tools/spikes/m1-nvenc/run.sh` (outputs in `results/m1-nvenc/`)
- **What it does:** encodes 600 frames (10 s) of a 3840x2160 @ 60 fps test pattern at 40 Mbit/s with
  each NVENC codec at preset `p1` (fastest) and `p5` (higher quality), checks each file decodes cleanly,
  then repeats with raw frames from RAM so the CPU pattern generator is not the bottleneck.

## Output

Source generated on the fly (includes CPU pattern generation):

| Codec | Preset | Frames | Wall time (s) | Encode fps | x real time | Output size | Decodes OK |
|---|---|---|---|---|---|---|---|
| h264_nvenc | p1 | 600 | 1.93 | 310 | 5.2x | 50M | yes |
| h264_nvenc | p5 | 600 | 7.61 | 79 | 1.3x | 52M | yes |
| hevc_nvenc | p1 | 600 | 1.45 | 415 | 6.9x | 49M | yes |
| hevc_nvenc | p5 | 600 | 4.50 | 133 | 2.2x | 50M | yes |
| av1_nvenc | p1 | 600 | 1.55 | 388 | 6.5x | 51M | yes |
| av1_nvenc | p5 | 600 | 4.64 | 129 | 2.2x | 51M | yes |

Raw 4K frames from RAM, preset p5:

| Codec | Encode fps |
|---|---|
| h264_nvenc | 80 |
| hevc_nvenc | 141 |
| av1_nvenc | 135 |

## Surprises / gotchas

- `bc` is not installed; the script uses `awk` for arithmetic.
- H.264 at `p5` is only 1.3x real time at 4K60. HEVC and AV1 are ~2.2x at `p5` and ~6.5x at `p1`.
- These numbers include copying raw frames from system memory to the GPU. In Nitid the frames are
  already on the GPU, so real throughput should be the same or higher.

## What this means for PLAN.md

Recording the enhanced 4K60 output is fine. Default recorder: **HEVC or AV1 at `p4`/`p5`**; use
H.264 only at `p1`–`p3` for 4K60.
