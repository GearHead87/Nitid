# 0.1 step 3: Does AI frame generation work in the player, and do the new frames look right?

- **Result:** ✅ pass with LOW / MEDIUM (165 fps, real-time, clearly better than blending). ⚠️ HIGH is too
  slow for a 165 Hz display in 0.1 (each frame blocks the render loop for ~10 ms).
- **Date:** 2026-11-26
- **Setup:** RTX 5080, driver 620.73.04, VFX SDK 1.3.0, KDE Plasma Wayland, 2560x1440 @ 165 Hz monitor,
  window 2048x1152. Video is upscaled by VSR HIGH first, then frames are generated at 2048x1152.
- **How to run:** `tests/v0.1/03-frame-gen/run.sh` (~3 min of windows)
- **Full output:** [output-2026-11-26.md](output-2026-11-26.md)

## A. Are the generated frames right? (rebuild dropped frames)

The 24 fps clip is played using only every 2nd (or 4th) frame. Frame generation fills the gaps at 2x (4x),
the window image is captured at the exact moments of dropped frames (`--capture-at`), and compared with the
real dropped frames. Mean PSNR over 5 moments (2, 3, 4, 5, 6 s):

| Test | Position between kept frames | **VFX frame gen (MEDIUM)** | 50/50 blend | Repeat previous |
|---|---|---|---|---|
| 2x | 1/2 | **30.83** | 27.59 | 25.47 |
| 4x | 1/4 | **27.94** | 26.49 | 25.47 |
| 4x | 2/4 | **26.18** | 25.09 | 23.03 |
| 4x | 3/4 | **27.99** | 26.90 | 22.33 |

- Generated frames land at the right time positions (1/4, 2/4, 3/4) and beat blending by 3.2 dB at 2x and
  1.1–1.5 dB at 4x. The gaps here are 83 ms (2x from 12 fps) and 167 ms (4x from 6 fps), much harder than
  normal use (42 ms for 24 fps video), so absolute numbers are low for every method.
- These match V3 and S9 (Python and C++ outside the player): the full chain (decode → VSR → frame gen →
  window) adds no errors.

## B. Speed and smoothness (8 s each)

| Source | Quality | Multiplier | Output fps | Generated / output | Frame gen ms | Missed refreshes |
|---|---|---|---|---|---|---|
| 1080p24 | LOW | per refresh | 165.0 | 1251 / 1282 | 0.48 | 0 |
| 1080p24 | **MEDIUM** | **per refresh** | **165.0** | 1242 / 1273 | 1.41 | 2 |
| 1080p24 | HIGH | per refresh | 95.1 | 722 / 743 | 9.68 | 694 |
| 1080p24 | MEDIUM | 2x | 165.0 | 623 / 1278 | 1.42 | 0 |
| 1080p24 | MEDIUM | 4x | 164.9 | 948 / 1279 | 1.42 | 0 |
| 1080p60 | MEDIUM | per refresh | 165.2 | 1159 / 1278 | 1.40 | 12 |
| 1080p60 | MEDIUM | 2x | 165.0 | 580 / 1279 | 1.41 | 1 |
| 360p60 | MEDIUM | per refresh | 165.0 | 1157 / 1277 | 1.43 | 2 |

- **"Per refresh"** (default): a new frame for every refresh at its exact time (S9's Timestep mode), so 24
  or 60 fps video becomes true 165 fps motion with no uneven cadence (165 / 24 is not a whole number).
  Cost at 2048x1152: VSR 2.9 ms per source frame + 1.4 ms per refresh.
- **2x / 4x** keep the multiplier's spacing (e.g. 48 fps for 24 fps video), shown on the 165 Hz grid.
- **HIGH** takes 9.7 ms per frame, longer than one refresh (6.06 ms), so refreshes are missed even at 2x
  (122.8 fps). Playback speed stays right (9.7 s played in 10 s) thanks to the clock fix below.

## Fixed while testing

1. **Display clock could run slow.** It advanced one refresh per rendered frame unless a frame was 1.5x
   late, so frames that steadily took ~1.4 refreshes (HIGH mode) would have played the video in slow
   motion. Now media time follows the wall clock in whole refreshes and carries the leftover over (unit
   test added). After a stall over 0.25 s (loading, window drag) it continues where it was.
2. **Test runs ended before all captures.** The player now quits once the requested captures are saved,
   and prints the time to the first frame (0.3 s here, including loading both AI models).

## What this means

- Step 3 confirmed: 24/60 fps video plays at 165 fps with VFX frame generation, on the right frames.
- **Defaults: MEDIUM, one generated frame per refresh.** HIGH needs frame generation to run ahead of the
  display (a queue of generated frames on a separate path) instead of inside the render loop. That's the
  next improvement for frame gen, together with VSR on its own path.
