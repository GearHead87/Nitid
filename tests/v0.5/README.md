# Nitid 0.5 tests

Tests of 0.5 (Phase 4: NVIDIA DLSS on video, natively). One folder per test, each with `run.sh`, `NOTES.md` and the
dated output. Helpers: [common.sh](common.sh) (the 0.1 helpers).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | DLSS module + NVIDIA's runtime: loads, reports, installs, fails gracefully? | ✅ | 2026-11-28 | [01-dlss-module](01-dlss-module/NOTES.md): DLSS 310.9.16 SR + FG up to 6x natively; `nitid-setup-dlss` 64 MB; missing runtime / module reported; playback unchanged |
| 02 | DLSS Super Resolution as an upscaler (no motion yet): quality, speed, cuts? | ⚠️ | 2026-11-28 | [02-dlss-upscaling](02-dlss-upscaling/NOTES.md): works, 0.7–0.9 ms to 1080p, 1.9–2.3 ms to 4K; without motion vectors below bicubic (PSNR −0.1 to −2.5 dB); scene-cut reset +4.4 dB after cuts |
| 03 | Optical flow motion vectors for DLSS: cost, does DLSS get better? | ⚠️ | 2026-11-28 | [03-motion-vectors](03-motion-vectors/NOTES.md): flow 0.53 ms (540p) – 1.1 ms (1080p); preset M +0.6–1.3 dB, best DLSS preset; still ≈ bicubic, below VSR; Auto = M (K for DLAA, no flow) |
