# 0.4 test 01: live input from PipeWire (low latency and smooth modes)

**Question:** can Nitid take live video from PipeWire (the path screen capture uses), show exactly the right
picture, and run it through upscaling and frame generation at the display rate, with low latency?

**Run:** `tests/v0.4/01-live-input/run.sh`. Output: [output-2026-11-27.md](output-2026-11-27.md).
**Source:** a GStreamer test pattern published as a PipeWire node (`videotestsrc ! pipewiresink`), so these tests
need no permission and capture **nothing from the desktop**. The real desktop path (portal, DMA-BUF) is test 02.

## Result: ✅

- **Right picture:** SMPTE colour bars arrive bit-identical (PSNR inf in the bars region) in both byte orders
  (BGRx, RGBx).
- **Low-latency mode** (default for live input): the newest frame, upscaled with VSR HIGH, at 165 fps; **28 ms**
  from a frame arriving to it being on screen with a 60 fps source, 38 ms with 30 fps. Of that, ~19 ms is the
  display path (two presents queued; spike S7 / test 0.1-01) and on average half a refresh is waiting for the next
  refresh; the copy + colour conversion of a 1280x720 frame takes 0.27 ms.
- **Smooth mode** (frame generation between the last two captured frames): 30 fps → 165 fps with 904–924 of
  ~1280 shown frames generated; 60 fps → 728–735 generated. Costs one frame interval of latency: 59 ms (30 fps
  source) / 43 ms (60 fps), as designed.
- 0–1 missed refreshes in most runs (one smooth run had 49 while the desktop was busy). "Dropped" frames (6–36
  in 8 s) are frames replaced by a newer one before the next refresh took them: a live picture always shows the
  newest.

## How it works

- `input/pipewire_capture.*`: a PipeWire stream on its own thread. Formats offered: BGRx / BGRA / RGBx / RGBA,
  first with the DRM modifiers Vulkan can import (DMA-BUF, zero-copy from the compositor; spike S6), then as
  shared memory. DMA-BUF frames are imported into Vulkan (cached per buffer), copied on a second Vulkan queue into
  a buffer CUDA can read; shared-memory frames are uploaded. A CUDA kernel makes RGBA8. Only the newest frame is
  kept.
- `Pipeline::produce_live`: takes the newest frame, upscales it into the two-frame ring with its arrival time;
  low latency returns it, smooth generates the moment one frame interval (+ a refresh) ago between the two.
  Runs on the render thread (no render-ahead: a live frame can't be prepared before it arrives).
- `Engine::open_pipewire(target)` (node id / name / serial) and `nitid --pipewire <target>`; `--live-mode
  low-latency|smooth`. Stats report the capture status, arrival rate, capture → screen latency, dropped frames.

## Notes

- The source frame rate is measured from arrival times (a desktop sends frames only when something changes).
- Frames from a still desktop arrive seconds apart; smooth mode does not interpolate across gaps over 0.25 s.
