# V1: Does VFX Video Super Resolution run here, and is it fast enough for real time?

- **Result:** ✅ pass, with a lot of headroom
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, VFX SDK 1.3.0 (`nvidia-vfx` 0.2.0.0), torch 2.11.0+cu128, RTX 5080
- **How to run:** `tools/spikes/v1-vfx-vsr/run.sh` (needs `vfx-env`, `fetch-sdks.sh`, `fetch-media.sh`)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md); videos in `results/v1-vfx-vsr/`

## What was tested

1. NVIDIA's `video_super_resolution.py`, end to end (CPU decode → VSR → HEVC encode), 2x, modes
   HIGH / ULTRA / STREAMING_ULTRA, on the 360p60, 540p, 720p and 1080p test clips. All 12 runs worked.
2. [`bench_vsr.py`](bench_vsr.py): **GPU-only** time per frame with the frame already on the GPU
   (as in Nitid), CUDA-event timed, 100 frames after 10 warm-up frames, real video content.

## Key numbers (GPU-only, ms per frame)

| Case | LOW | MEDIUM | HIGH | ULTRA | HIGHBITRATE_HIGH | STREAMING_MEDIUM | STREAMING_ULTRA |
|---|---|---|---|---|---|---|---|
| 540p → 1080p | 0.41 | 0.47 | 0.77 | 0.98 | 0.93 | 0.57 | 1.58 |
| 720p → 1440p | 0.58 | 0.78 | 1.29 | 1.68 | 1.52 | 0.83 | 2.57 |
| **1080p → 2160p** | 1.26 | 1.52 | **2.91** | 3.84 | 3.62 | 1.81 | 5.85 |
| 720p → 2160p (3x) | 0.87 | 0.98 | 1.59 | 1.96 | 1.80 | 1.12 | 2.95 |
| 540p → 2160p (4x) | 0.84 | 0.80 | 1.15 | 1.37 | 1.27 | 0.91 | 1.93 |

Budget at 60 fps is 16.7 ms per frame; at 165 Hz (this monitor) it is 6.1 ms. **Every mode fits a
1080p → 4K 60 fps pipeline**; HIGH/ULTRA leave ~13 ms for everything else. Model load: ~0.2 s.

End to end with NVIDIA's sample (CPU decode + HEVC encode included): 1080p → 4K ~43–49 fps,
720p → 1440p ~95–109 fps, 540p → 1080p ~157–182 fps. The gap to the GPU-only numbers is the
sample's CPU-side decode/convert/encode, which Nitid avoids by staying on the GPU.

## Surprises / gotchas

- **3x and 4x are cheaper than 2x to the same 4K output** (540p→4K HIGH 1.15 ms vs 1080p→4K HIGH
  2.91 ms): the cost follows the *input* size, so the network likely runs near input resolution.
- The very first sample run (360p HIGH, 140 fps) was slower than the following ULTRA run (395 fps):
  first-run warm-up (model/cuDNN initialisation). Warm up before timing.
- The wheel ships Linux NGX libraries `libnvidia-ngx-vsr.so.1.8.3` and `libnvidia-ngx-truehdr.so.1.2.3`
  (see [vfx-env](../vfx-env/NOTES.md)).
- Quality (does it *look* better?) is judged separately in V5.

## What this means for PLAN.md

VSR is confirmed as the default upscaler for Phases 2–3. Default to **HIGH**; offer ULTRA and the
streaming modes. Even STREAMING_ULTRA at 1080p→4K (5.9 ms) fits 165 Hz output only without frame
generation, so frame-gen budgets must be planned together with VSR mode.
