# 0.3 test 01: pipeline ahead of the display (render-ahead) and frame-gen pacing

**Question:** with upscaling and frame generation on their own thread, preparing frames for the next refreshes,
does frame generation HIGH become smooth at 165 Hz, and does everything else still work (right frames, seeking,
settings changes while playing)?

**Run:** `tests/v0.3/01-render-ahead/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).

## Result: ✅

| 12 s runs | 0.2.0 (pipeline on the render thread) | 0.3 (render-ahead) |
|---|---|---|
| 1080p trailer, FG HIGH per refresh | 95.5 fps, 1089 missed refreshes | 165 fps, one new frame every **2** refreshes (82.5 fps of motion), 3–49 missed, ~20 late |
| 4K60 AV1, FG HIGH per refresh | 103.9 fps, 1043 missed | 165 fps, every 2 refreshes, 0 missed, 30 late |
| FG LOW / MEDIUM per refresh (1080p, 4K) | (165 fps) | 165 fps, every refresh, 0 missed, 1–2 late (start-up) |

- **Even pacing instead of stutter:** 0.2.0 ran HIGH (9.6–10.4 ms per frame, longer than a 6.06 ms refresh)
  on the render thread, so the display waited for it at irregular moments. Now the worker measures what a frame
  costs and prepares one every *stride* refreshes when it doesn't fit (stride = ceil(cost / 95 % of the time),
  held for 90 frames before it may drop again), so motion is smooth at refresh / stride.
- **Settings changes don't freeze the window:** pressing H / B / V / G while playing (each reloads AI models)
  gave 0 missed refreshes; the picture holds for the ~0.15 s a model takes to load ("late frames").
- **Right frame:** the window image after `--start 5` matches FFmpeg's frame 120 (40.4 dB) and not its
  neighbours (24–25 dB).

## How it works

- `pipeline/render_ahead.*`: a worker thread owns the pipeline while playing; it prepares frames for grid positions
  (per refresh × stride, or 1/m source frames for a fixed multiplier, or whole source frames with frame gen off)
  just ahead of the display, up to 4 queued, copied into a pool of 6 buffers. Each refresh the render thread takes
  the newest frame whose position has been reached and presents it (or keeps the previous one).
- After open / seek the old picture stays until the first new frame is ready; the display clock then starts from
  that frame. The sound follows the display clock (not the frame grid).
- The presenter got its own CUDA stream (highest priority); the pipeline's stream is lowest priority.

## Findings

- **90 % flapped, 95 % holds:** with a 90 % budget HIGH at 1080p (10.6–11 ms per frame including its share of VSR)
  switched between stride 2 and 3; at 95 % it stays at 2.
- **The first frames after start / seek are slow** (decoder start, model warm-up: 30–50 ms) and pushed the stride
  to 4 at MEDIUM; the first 12 frames are now ignored for pacing.
- **Sound at start:** the picture can still shift by a few frames right after the first one; the sound now lines
  up once more if it is still > 12 ms off ~30 frames after a resync (start-up gap went from −30 ms to −3…−7 ms).
- **HIGH still misses some refreshes** (bimodal between runs: 0 or ~40–50 in 12 s with 2x / stride 2): each
  frame-generation burst occupies the GPU ~10 ms and sometimes delays the presenter's blit past the refresh. CUDA
  stream priorities (presenter high, pipeline low) made half of the runs clean (before: all 4 runs had 38–105);
  the rest is GPU time-slicing between CUDA and Vulkan work. 1–4 % of refreshes vs 1000+ before.
- **Desktop noise:** one full run had 200–864 missed refreshes on rows that are clean on repeat, with 173–177 fps
  measured on a 165 Hz display (the compositor itself was irregular). Rows like that are re-run (see output).
