# 0.1 step 4: Does `nitid-cli bench` run the pipeline headless, and how good is the output over whole clips?

- **Result:** ✅ pass. Whole-clip scores confirm the AI effects help: VSR +6.7–9.0 VMAF over bicubic, frame
  generation +5–10.7 VMAF over blending. Found and fixed a scoring bug and a scene-cut detector weakness.
- **Date:** 2026-11-26
- **Setup:** RTX 5080, driver 620.73.04, VFX SDK 1.3.0. Outputs saved lossless (FFV1) and scored with
  `scripts/quality.sh` (PSNR / SSIM / VMAF against the original), then deleted.
- **How to run:** `tests/v0.1/04-cli-bench/run.sh` (~2 min, no window)
- **Full output:** [output-2026-11-26.md](output-2026-11-26.md)

## What `nitid-cli` does now

```
nitid-cli info [file]                      GPU, driver, VFX runtime, which AI effects work; file details
nitid-cli bench <file> [options]           pipeline without a window, as fast as possible
    --upscale off|low|medium|high|ultra    --size WxH | --scale F    (output box)
    --frame-gen off|low|medium|high        --multiplier N            --source-step N
    -o out.mkv [--lossless]                --frames N  --no-scene-cuts  --sw-decode  -v
```

## A. Upscaling to 1080p (240 frames vs the real 1080p video)

| Source | Plain bicubic (PSNR / VMAF) | Nitid VSR HIGH | Nitid VSR ULTRA |
|---|---|---|---|
| 540p | 40.39 / 82.4 | 41.32 / **89.1** | 41.50 / **90.5** |
| 720p | 42.27 / 88.6 | 41.79 / **90.7** | 41.80 / **91.8** |
| 540p very low bitrate | 36.60 / 55.2 | 37.02 / **62.0** | 37.18 / **64.2** |

VMAF (closest to what people see) improves by +2 to +9 points. PSNR goes up from 540p and stays about the
same from 720p, where VSR sharpens detail that PSNR counts as a difference. Matches V5 (+1.4 dB / +7 VMAF
at 540p ULTRA).

## B. Frame generation (every 2nd frame dropped and rebuilt; all frames vs the original)

| Clip | Repeat frames (PSNR / VMAF) | FFmpeg blend | Nitid MEDIUM, cuts on | Nitid MEDIUM, cuts off |
|---|---|---|---|---|
| Sintel 10 s (fast pans) | 26.71 / 70.3 | 28.00 / 71.2 | **29.42 / 81.9** | 29.38 / 81.6 |
| Sintel trailer 52 s (many cuts) | 31.17 / 83.1 | 33.09 / 83.4 | **34.41 / 88.3** | 33.94 / 88.3 |

Nitid's own scene-cut detection is worth +0.5 dB on the trailer: at a cut it shows the old shot until the
new one starts, instead of morphing between two unrelated pictures.

## C. Speed (headless, throughput after the first frame)

| Input | Settings | Output | Throughput | × real time |
|---|---|---|---|---|
| 1080p24 | decode only | 1080p24 | 312 fps | 13.0 |
| 540p24 | VSR HIGH → 1080p | 1080p24 | 1078 fps | 44.9 |
| 1080p24 | VSR HIGH → 1440p | 1440p24 | 306 fps | 12.8 |
| 1080p24 | VSR HIGH → 4K | 4K24 | 307 fps | 12.8 |
| 1080p24 | frame gen MEDIUM 2x | 1080p48 | 624 fps | 13.0 |
| 1080p24 | VSR 1440p + frame gen MEDIUM 4x | 1440p96 | 531 fps | 5.5 |
| 4K60 HEVC | decode only | 4K60 | 79 fps | 1.3 |

1080p is limited by the CPU colour conversion (3.2 ms per frame on the decoder thread), not the GPU. 4K60
decode-only reaches only 1.3x real time for the same reason (12.4 ms per frame): the next fix.

## Problems found and fixed

1. **`scripts/quality.sh` paired the wrong frames.** Matroska stores timestamps in whole milliseconds; at
   24 fps (41.67 ms) FFmpeg's timestamp matching paired some frames with their neighbour. A pass-through
   check (each frame 42 dB vs its source frame) scored only 29.6 dB as a whole clip. Frames are now paired
   by number; the pass-through scores 43.0 dB / VMAF 98 (H.264) as expected. Earlier spike results used MP4
   inputs with exact timestamps and were not affected.
2. **H.264 output made the comparison unfair** (Nitid's output was re-encoded, the bicubic baseline was
   not): outputs are now saved lossless (FFV1) for scoring.
3. **The scene-cut detector flagged whole fades and missed a cut between two similar shots.** Compared with
   FFmpeg's scene detection and checked by eye on the trailer
   ([disputed frames](scene_cuts_disputed.jpg): row 2 is a real cut FFmpeg missed, row 4 a real cut the old
   detector missed):

   | Detector | Cuts found | Real cuts found (of 7) | Extra |
   |---|---|---|---|
   | v1: histogram difference > 0.4 | 25 | 4 | 21 (fades / title cards) |
   | FFmpeg `scene > 0.3` (reference) | 5 | 5 | 0 (missed 572, 680) |
   | v2: + must jump 3x from the previous frame | 7 | 6 | 1 (title fade-in at 18) |
   | **v3: + 32x18 thumbnail difference** | **8** | **7** | 1 (title fade-in at 18) |

   v3 is in libnitid now, with unit tests for fades (no cut) and same-histogram cuts (cut).

## What this means

- The full pipeline is confirmed on whole clips, not just single frames: VSR and frame generation are both
  clearly better than the simple methods, with numbers that match the Phase 0 spikes.
- `nitid-cli bench` gives repeatable numbers for every later change.
