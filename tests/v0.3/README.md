# Nitid 0.3 tests

Tests of 0.3 (Phase 2 rest: pipeline ahead of the display with frame-gen pacing, side-by-side comparison, optional
VFX clean-up). One folder per test, each with `run.sh`, `NOTES.md` and the dated output. Helpers:
[common.sh](common.sh) (same as 0.1).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | Render-ahead: frame gen HIGH smooth at 165 Hz, settings changes without freezing? | ✅ | 2026-11-26 | [01-render-ahead](01-render-ahead/NOTES.md): HIGH from ~100 fps / 1000+ missed refreshes (0.2.0) to 165 fps with a new frame every 2nd refresh; settings changes 0 missed; right frame after seek |
| 02 | Compare view: exactly original | exactly Nitid, lined up, free? | ✅ | 2026-11-26 | [02-compare](02-compare/NOTES.md): both halves bit-identical to the plain and the Nitid picture; no measurable cost |
