# 0.2 test 01: FSR 1 and NIS spatial upscalers

**Question:** do the new non-AI upscalers (AMD FSR 1 = EASU + RCAS, NVIDIA Image Scaling) work, how do they
compare with bicubic and VFX VSR, and are they cheap enough to run on any NVIDIA GPU?

**Run:** `tests/v0.2/01-spatial-upscalers/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).
**Machine:** RTX 5080, driver 620, 165 Hz.

## Result: ✅

- **Correct:** the crop ([crop_bicubic-fsr-nis-vsr-real.jpg](crop_bicubic-fsr-nis-vsr-real.jpg):
  bicubic | FSR | NIS | VSR | real 1080p) shows no artifacts, and the scores match what each method is known for.
- **Quality** (whole clip vs the real 1080p video; the fair baseline is bicubic *inside Nitid*):

  | 540p → 1080p | PSNR | VMAF |
  |---|---|---|
  | bicubic in Nitid | 40.11 | 79.9 |
  | FSR 1, sharpness 0.5 | 39.85 | 81.9 |
  | NIS, sharpness 0.5 | 40.29 | **90.0** |
  | VSR HIGH | **42.61** | **90.1** |

  On all three clips: FSR ≈ bicubic (+0.8–2 VMAF), NIS +7.5–10 VMAF from its sharpening at about the same
  PSNR, VSR the best PSNR (it really restores detail; NIS only sharpens what is there). VMAF likes sharpening,
  so NIS's VMAF overstates it; on the heavily compressed clip (vlq_540p) NIS also sharpens the blocking.
- **Speed:** FSR 0.08 ms and NIS 0.06 ms per frame at 1080p; 0.31 / 0.21 ms at 4K (VSR HIGH: 0.76 / 2.8 ms).
  10–13x cheaper than VSR.
- **Window:** 165 fps with each, 0–3 missed refreshes in 5 s (one FSR run had 31 while the desktop was busy; 3
  re-runs had 0).

## Findings

- **Nitid's 8-bit RGB working format costs PSNR.** Decoding to RGB and writing back to YUV alone gives 44.8 dB
  against the source (VMAF 99.5). FFmpeg's bicubic baseline scales in YUV and skips that, so it scores up to
  2 dB higher than any method inside Nitid at 720p. The 0.1 test 04 compared VSR with FFmpeg's bicubic, which
  made VSR at 720p look worse than bicubic; against bicubic inside Nitid, VSR HIGH is +1.2 dB / +5.3 VMAF.
  Scoring through RGB matters only for PSNR at high quality; it does not change any ranking here.
- **NIS only scales 1x to 2x.** Nitid caps the NIS output at 2x the source and lets the window blit do the
  rest (e.g. 540p in a 4K window: NIS to 1080p, then bilinear).
- **Ported, not wrapped:** both are CUDA ports of the vendors' MIT-licensed shader code
  (`libnitid/src/gpu/kernels/scale.cu`), so they run in the same CUDA pipeline as VSR and frame generation
  without a Vulkan compute path. NIS's configuration and filter tables come from NVIDIA's unchanged
  `NIS_Config.h` (`libnitid/third_party/nis/`).

## Use

`nitid video.mp4 --upscale fsr|nis [--sharpness 0..1]`, `V` cycles VSR → FSR → NIS → off while playing. Same
options for `nitid-cli bench`. Default stays VSR HIGH (best picture); FSR / NIS are for GPUs or setups
without the VFX runtime, or to save GPU time for frame generation.
