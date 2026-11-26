# Nitid 0.4 tests

Tests of 0.4 (Phase 3: live input: screen capture through PipeWire, low-latency mode). One folder per test, each
with `run.sh`, `NOTES.md` and the dated output. Helpers: [common.sh](common.sh) (0.1 helpers + a synthetic PipeWire
source made with GStreamer, so most tests capture nothing from the desktop).

| Step | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | Live input from PipeWire: right picture, low latency, smooth mode? | ✅ | 2026-11-27 | [01-live-input](01-live-input/NOTES.md): bit-exact colour bars (BGRx, RGBx); 165 fps; capture → screen 28 ms (60 fps source) low latency, 43 ms smooth with frame generation |
