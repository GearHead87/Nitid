# Nitid 0.2 tests

Tests of 0.2 (Phase 1 rest: spatial upscalers, seeking, audio with A/V sync, on-screen controls). One folder per
test, each with `run.sh`, `NOTES.md` and the dated output. Helpers: [common.sh](common.sh) (same as 0.1).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | FSR 1 and NIS: correct, better than bicubic, cheap? | ✅ | 2026-11-26 | [01-spatial-upscalers](01-spatial-upscalers/NOTES.md): FSR ≈ bicubic, NIS +7.5–10 VMAF (sharpening), VSR still best; 0.06–0.08 ms at 1080p, 0.2–0.3 ms at 4K |
