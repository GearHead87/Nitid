# Nitid 0.5 tests

Tests of 0.5 (Phase 4: NVIDIA DLSS on video, natively). One folder per test, each with `run.sh`, `NOTES.md` and the
dated output. Helpers: [common.sh](common.sh) (the 0.1 helpers).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | DLSS module + NVIDIA's runtime: loads, reports, installs, fails gracefully? | ✅ | 2026-11-28 | [01-dlss-module](01-dlss-module/NOTES.md): DLSS 310.9.16 SR + FG up to 6x natively; `nitid-setup-dlss` 64 MB; missing runtime / module reported; playback unchanged |
