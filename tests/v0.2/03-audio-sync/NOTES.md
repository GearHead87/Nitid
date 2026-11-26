# 0.2 test 03: sound in sync with the picture

**Question:** does Nitid play the file's sound, in sync with the picture, while the picture stays locked to the
display (frame generation per refresh), including after seeks, pauses, loops and stalls?

**Run:** `tests/v0.2/03-audio-sync/run.sh` (plays at volume 0.2). Output: [output-2026-11-26.md](output-2026-11-26.md).
**Output device during the test:** Bluetooth headphones (soundcore R60i NC, AAC), which PipeWire reports as
235 ms behind; the display is 19–21 ms from present() to the screen.

## Result: ✅ (engine measurement) / ⏳ manual check by ear and eye

| Case | A/V at the end | Largest A/V | Resyncs | Underruns |
|---|---|---|---|---|
| Sync clip, VSR + FG per refresh, 20 s | −0.2 ms | 4.2 ms | 1 (start) | 0 |
| Sync clip, upscaling and FG off (3 runs) | 0.0 to +0.5 ms | 7.0–8.6 ms | 1 | 0 |
| 4 seeks + 2 pauses in 20 s | −1.0 ms | 10.3 ms | 6 (start + one per jump) | 0 |
| Loop (wraps once) | −0.2 ms | 28.5 ms (at the wrap) | 2 | 0 |
| Sintel trailer, 25 s | +0.1 ms | 9.0 ms | 1 | 0 |

Speed corrections stay within 0.9998–1.0011 (≤ 0.11 %, inaudible). 165 fps, 0 missed refreshes in steady
play. Files without sound and `--no-audio` play as before.

## How it works

- **The picture leads, the sound follows** (like mpv's display-resample mode). The display clock stays on the
  refresh grid, so frame generation is unaffected. Each rendered frame tells the audio player "this media time
  will be on screen at time T" (T = now + measured present-to-screen time). The PipeWire callback knows when the
  buffer it is filling will be heard (PipeWire's delay to the device), so the player knows the gap.
- Small gaps (< 80 ms): the sound is resampled by up to ±1 % (0.5 % per 10 ms of gap) until the gap is gone.
  Big ones (start, seek, pause, loop, a stalled picture): it skips sound or plays silence once ("resync").
- Decoding: a second FFmpeg demuxer/decoder for the audio stream on its own thread, swresample to 48 kHz stereo
  float, 2 s buffer. PipeWire is loaded at run time (`libpipewire-0.3.so.0`), so a machine without it still plays
  video. Nothing is linked against it (`-Wl,--no-undefined` checks that).

## Bugs found and fixed while testing

- **Sound 10–13 ms late after every seek.** The display clock started when the seek was asked for, but the first
  frame takes 11–38 ms to decode, so the picture jumped ahead by that much right after it. The clock now starts
  when the first frame after a seek is ready (also fixes the picture's first frames after a seek).
- **Loop wrap left a 78 ms gap for a moment.** The wrap used its own restart code and resync waited for the
  smoothed error. It now goes through `seek(0)`, and a raw gap over 100 ms resyncs at once.

## Manual check (to do by the user)

The measured gap is between what PipeWire *says* the speaker delay is and what Vulkan *says* reached the screen.
To check it for real: `build/apps/nitid/nitid results/media/avsync_1080p24.mp4` (white flash + beep every
second). The beep should land on the flash. If it doesn't on some device, `,` / `.` move the sound by 10 ms
(`--audio-delay` on the command line).
