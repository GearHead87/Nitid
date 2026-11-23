# M2: Does GPU decoding (Vulkan Video and CUDA/NVDEC) reach ≥ 120 fps at 4K?

- **Result:** ✅ pass, all three codecs with both GPU methods
- **Date:** 2026-11-23
- **Driver / tools:** 620.73.04, FFmpeg (`ffmpeg-obs` 9.1), RTX 5080
- **How to run:** `scripts/fetch-media.sh` (once), then `tools/spikes/m2-decode/run.sh`
- **What it does:** decodes 600 frames of 3840x2160 @ 60 fps (Sintel trailer upscaled, NVENC-encoded
  at 25–40 Mbit/s) with Vulkan Video and with CUDA/NVDEC, keeping frames on the GPU
  (`-hwaccel_output_format vulkan|cuda`), plus CPU decoding for reference.

## Output

| Codec | Method | Frames | Decode fps | Errors in log |
|---|---|---|---|---|
| h264 | Vulkan Video | 600 | 378 | 0 |
| h264 | CUDA (NVDEC) | 600 | 519 | 0 |
| h264 | CPU (reference) | 600 | 653 | 0 |
| hevc | Vulkan Video | 600 | 405 | 0 |
| hevc | CUDA (NVDEC) | 600 | 517 | 0 |
| hevc | CPU (reference) | 600 | 515 | 0 |
| av1 | Vulkan Video | 600 | 390 | 0 |
| av1 | CUDA (NVDEC) | 600 | 456 | 0 |
| av1 | CPU (reference) | 600 | 286 | 0 |

Vulkan decoding selected: `Device 0 selected: NVIDIA GeForce RTX 5080 (discrete)`.

## Surprises / gotchas

- fps is measured over the whole ffmpeg run (including startup), so real decode speed is a bit higher.
- **This PC also has an AMD Radeon 610M iGPU** (`amdgpu` loaded). FFmpeg picked the RTX 5080 on its
  own, but Nitid must **choose the NVIDIA Vulkan device explicitly** (by vendor ID / name), never "device 0".
- CUDA/NVDEC was faster than Vulkan Video here (~460–520 vs ~380–405 fps). Both are far above need.
- The 9950X decodes these 4K clips fast on the CPU too, but CPU decoding means a copy to the GPU per frame.

## What this means for PLAN.md

Decoding is not a bottleneck. Use **Vulkan Video** to keep frames in Vulkan with no interop; keep
CUDA/NVDEC as a fallback. Add "pick the NVIDIA device explicitly" to the GPU module.
