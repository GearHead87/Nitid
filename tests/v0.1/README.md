# Nitid 0.1 tests

Tests of the first runnable version (the "minimum version" from the plan): file → AI upscaling →
AI frame generation → window, plus the `nitid-cli bench` tool. One folder per test, each with `run.sh`,
`NOTES.md` (result and findings) and the dated raw output. Shared helpers: [common.sh](common.sh).
Unit tests for the engine's pure logic are in `libnitid/tests` (`ctest --test-dir build`).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | Plain playback: right speed, paced to the display? | ✅ ≤1080p / ⚠️ 4K60 | 2026-11-26 | [01-playback](01-playback/NOTES.md): 165 fps, 0 skipped up to 1080p; 4K60 limited by CPU colour conversion |
