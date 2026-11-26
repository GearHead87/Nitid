# 0.2 test 02: seeking

**Question:** does seeking land on exactly the right frame, and how long does the picture stop while it
happens (window, with VSR + frame generation running)?

**Run:** `tests/v0.2/02-seeking/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).

## Result: ✅

- **Frame-exact:** in 8 cases (H.264 / HEVC / AV1, MP4 and Matroska, NVDEC and software decoding,
  `--source-step 2`), the 10 frames after `--start T` are bit-identical to frames round(T × fps) onwards of a
  full decode.
- **Fast:** first frame after a seek in **11–38 ms** for 1080p, **150–195 ms** for 4K60 HEVC / AV1 (the decoder
  starts at the keyframe before the target; these 4K files have a keyframe every 4 s, so up to ~240 frames are
  decoded and dropped). About **1–2 missed refreshes per seek** (the frame stops for that long), then playback
  is back at 165 fps with frame generation.
- Keys: Left / Right ±5 s, PgDn / PgUp ±60 s, Home = start. `--start T` in both apps.

## How it works

- `FfmpegFileSource::seek` seeks the demuxer to the keyframe before the target, then the decoder thread works
  out which frame it is from the first frame's timestamp, and drops frames before the target *before* colour
  conversion (they cost only NVDEC time). Frame numbers come from the file frame count, so `--source-step` and
  scene-cut detection keep working after a seek.
- `Engine::seek` restarts the pipeline's two-frame ring and the display clock at the target; while paused it
  shows the new frame.

## Findings

- A run once showed ~100 missed refreshes over 5 seeks; two repeats showed 5. Same as test 01: this desktop is
  in use while the tests run, so single runs with many misses are re-run before drawing conclusions.
- Seek time on 4K is set by the keyframe distance of the file, not by Nitid. A faster path for long-GOP files
  (keep showing the old frame and only replace it when the target is ready) is what Nitid already does: the
  picture freezes, it does not go black.
