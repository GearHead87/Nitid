# 0.2 test 04: on-screen controls (Dear ImGui)

**Question:** do the on-screen controls draw correctly on top of the video, without costing frames, and does
the player behave like a player (start without a file, stay open at the end)?

**Run:** `tests/v0.2/04-ui/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).
Pictures are the player's own window images (`--capture-at`), not desktop screenshots.

## Result: ✅

- **Drawn correctly:** [controls-settings-stats.jpg](controls-settings-stats.jpg) (control bar, settings and
  stats panels while playing with VSR + frame generation at 166 fps) and [help.jpg](help.jpg) (key list).
- **No measurable cost:** 3 runs each with the controls and two panels shown vs hidden: 165.0 fps in all six;
  missed refreshes 4–24 shown, 0–16 hidden (desktop noise, see tests 01 and 02).
- **Player behaviour:** `nitid` without a file shows an "open a file" screen (file dialog through the desktop
  portal, or drop a file on the window); at the end of a video it stays on the last frame with a Replay button
  instead of quitting (`--exit-at-end` for the old behaviour; `--quit-after` / `--capture-at` runs still end).

## What is on screen

- **Control bar** (bottom; hides after 2.5 s without mouse movement while playing, the mouse pointer too in
  fullscreen): play / pause, time, seek bar (seeks while dragging, at most every 0.15 s), mute + volume, panel
  buttons, fullscreen.
- **Settings panel** (P): upscaler (VSR / FSR 1 / NIS / off) with quality or sharpness, frame generation on/off,
  quality, frames per refresh or 2x–4x, scene-cut detection, A/V offset. Each section says what is actually
  running ("1920x1080 -> 2048x1152, 2.87 ms per frame") or why not.
- **Stats panel** (I): source, sizes, display rate and misses, per-stage times, present-to-screen time, A/V sync.
- **Keys panel** (F1). Tab hides everything; double-click toggles fullscreen.

## How it works

- The engine got an **overlay hook**: while recording each window frame it begins dynamic rendering (Vulkan 1.3)
  on the swapchain image after the video blit and calls the app's callback. The app draws ImGui with
  ImGui's own Vulkan backend on the engine's device and queue (`Engine::vulkan_device()`). libnitid stays free
  of UI code.
- Dear ImGui 1.93.0 is downloaded at configure time (pinned URL + SHA-256) and compiled into `nitid`.
- `--ui on|off|auto` (auto = off with `--capture-at`, so test images stay video-only); `--test-panel` opens
  panels for tests.

## Found while testing

- **A frame kept for pause / end-of-video could point at freed memory** after a window resize reallocated the
  pipeline's buffers. The pipeline now has a generation counter and the engine drops a stale frame.
- (Test harness) A background job of a non-interactive shell ignores SIGINT, so "Ctrl+C" tests must use SIGTERM
  (SDL turns both into a quit).
