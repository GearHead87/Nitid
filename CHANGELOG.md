# Changelog

All notable changes to Nitid. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
versions follow [Semantic Versioning](https://semver.org/) (0.x: minor versions may change behaviour).

How releases use this file: `scripts/bump-version.sh X.Y.Z` turns "Unreleased" into a version section, and the
release workflow puts that section at the top of the GitHub release, followed by the list of commits.

## [Unreleased]

## [0.4.0] - 2026-11-26

### Added
- **Live input from PipeWire** (`nitid --pipewire <node>`): any PipeWire video node (the path screen capture uses),
  upscaled and shown at the display rate. `--live-mode low-latency` (default: the newest frame, ~28 ms from
  arrival to screen with a 60 fps source) or `smooth` (frame generation between captured frames, one frame later).
  DMA-BUF frames are imported through Vulkan; shared memory works too.
- **Screen capture** (`nitid --capture-screen`, or "Capture the screen..." in the window): the desktop's portal asks
  which screen or window to share and remembers the choice. Zero-copy DMA-BUF from the compositor (0.33 ms per
  1440p frame), 165 fps, ~22 ms from capture to screen in low-latency mode. `L` switches low latency / smooth.

### Changed
- Engine API: `open_pipewire`, `open_screen_capture` (`ScreenCaptureOptions`), `Settings::live_mode`,
  `SourceInfo::live`; `Stats` gained capture status, arrival rate, capture → screen latency, dropped frames.
- The Vulkan device uses a second queue (capture) and enables DMA-BUF import when available.

### Known limits
- No capture-card input and no sound capture. Screen capture needs a Wayland desktop with xdg-desktop-portal
  (tested: KDE Plasma 6); put Nitid on another screen than the one it captures.
- Frame generation HIGH still misses some refreshes now and then (see 0.3.0).

## [0.3.0] - 2026-11-26

Smooth frame generation at every quality, a side-by-side compare view, and optional clean-up. Test results:
[tests/v0.3](https://github.com/GearHead87/Nitid/blob/main/tests/v0.3/README.md).

### Added
- **Render-ahead:** upscaling and frame generation run on their own thread, preparing frames for the coming
  refreshes. Frame generation HIGH is now smooth at 165 Hz: when a frame takes longer than a refresh, Nitid makes
  one every 2nd (3rd, 4th) refresh, evenly paced (0.2.0: ~100 fps with 1000+ missed refreshes in 12 s). Changing
  settings while playing no longer freezes the window while AI models load.
- Stats: late frames, frame-gen stride.
- **Clean-up** (`--cleanup denoise|deblur[-strength]`, Settings → Clean-up): VFX VSR's same-size denoise / deblur
  models when the video is not upscaled. Off by default: denoise changes little, deblur makes the picture crisper
  (higher VMAF, lower PSNR).
- **Compare view** (`X`, Settings → View, `--compare`): the original on the left, Nitid on the right; drag on the
  picture to move the line.

### Changed
- Engine API: `Settings::compare`, `Settings::cleanup` / `cleanup_strength`, `Engine::set_compare_split`,
  `parse_cleanup`; `Stats` gained `late_frames`, `frame_gen_stride`, `cleanup_active`, the picture rectangle.
- The window's CUDA copies run on their own high-priority stream; the AI pipeline's stream is low priority.

### Known limits
- Frame generation HIGH still misses some refreshes now and then (0–50 in 12 s at 165 Hz) when a ~10 ms
  generation burst holds the GPU at the wrong moment; MEDIUM is clean.
- No HDR output and no screen capture yet.

## [0.2.0] - 2026-11-26

Nitid becomes a complete video player: sound in sync, seeking, on-screen controls, and non-AI upscalers for any
NVIDIA GPU. Test results: [tests/v0.2](https://github.com/GearHead87/Nitid/blob/main/tests/v0.2/README.md).

### Added
- **FSR 1 and NIS upscaling** (`--upscale fsr|nis`, `--sharpness 0..1`; `V` cycles VSR / FSR 1 / NIS / off):
  AMD FidelityFX FSR 1 (EASU + RCAS) and NVIDIA Image Scaling, ported to CUDA. Work on any NVIDIA GPU without
  the VFX runtime; 0.06–0.08 ms per 1080p frame. NIS scales up to 2x.
- **Seeking**: Left / Right ±5 s, PgDn / PgUp ±60 s, Home; `--start` in both apps. Frame-exact; the first frame
  after a seek takes 11–38 ms at 1080p.
- **Sound** through PipeWire, in sync with the picture: the picture stays locked to the display and the sound
  follows it (resampled by at most 1 %, resync after seeks / pauses). Up / Down volume, M mute, `,` / `.` move
  the sound by 10 ms; `--volume`, `--audio-delay`, `--no-audio`. Works with Bluetooth latency (235 ms tested).
- **On-screen controls** (Dear ImGui): control bar with seek bar and volume (hides while playing), settings
  panel (P) for upscaling / frame generation / A/V offset, stats panel (I), key list (F1), Tab hides all.
  Double-click for fullscreen.
- **Open files from the window**: `nitid` without a file shows an open screen (file dialog, or drop a file).

### Changed
- At the end of a video the player stays on the last frame (Replay) instead of quitting; `--exit-at-end` quits.
- `V` cycles the upscaler (VSR / FSR 1 / NIS / off) instead of switching VSR on and off.
- F1 shows the keys in the window instead of printing them.
- Engine API: `seek`, `position`, sound controls, `set_overlay` / `vulkan_device` for app overlays; the Vulkan
  device enables dynamic rendering.

### Fixed
- The display clock started before the first frame after a seek was decoded, so the picture jumped ahead by the
  decoding time (11–38 ms at 1080p) right after a seek.

### Known limits
- Frame generation HIGH is still too slow for one frame per refresh at 165 Hz (next version).
- No HDR output; no screen capture yet. Sound is played only in the window (not by `nitid-cli`).
- A/V sync is measured by Nitid (PipeWire's reported speaker delay vs. the measured display delay); if a device
  reports its delay wrongly, `,` / `.` adjust it.

## [0.1.0] - 2026-11-26

The first runnable Nitid: play a video file with NVIDIA AI upscaling and AI frame generation, paced to the
display, on Linux. Test results: [tests/v0.1](https://github.com/GearHead87/Nitid/blob/main/tests/v0.1/README.md).

### Added
- **`nitid` player** (SDL3 window): plays a video file through AI upscaling (NVIDIA VFX Video Super
  Resolution) and AI frame generation (NVIDIA VFX Video Frame Generation), presented through Vulkan with FIFO
  pacing. Runs at the display's full refresh rate (tested at 165 Hz).
  - Frame generation makes one frame per display refresh by default, or a fixed 2x/3x/4x multiplier.
  - Upscaling quality low/medium/high/ultra; frame generation quality low/medium/high.
  - Keys to switch everything while playing (V/B upscaling, G/H frame gen, 0/2/3/4 multiplier, C scene cuts,
    Space pause, F fullscreen, S/R stats, F1 help).
- **`nitid-cli`**: `info` (GPU, AI effects available, file details) and `bench` (runs the same pipeline
  without a window, reports per-stage timings, can write the result to a video file).
- **Scene-cut detection**: frame generation is not asked to blend across a cut (found 7 of 7 real cuts in the
  Sintel trailer, no false cuts on fades).
- **GPU video path**: NVDEC hardware decoding into Nitid's own CUDA context, GPU colour conversion
  (NV12/P010 to RGBA), and zero-copy sharing between CUDA and Vulkan. 4K60 video plays at 165 fps.
- **Linux release tarball** (`nitid-X.Y.Z-linux-x86_64.tar.gz`) with bundled FFmpeg and SDL3, and
  `nitid-setup-vfx`, which installs NVIDIA's VFX runtime from NVIDIA after you accept its license.
- **Engine library `libnitid`** with a small public C++ API, usable on its own (installs a CMake package).

### Measured (RTX 5080, 165 Hz display)
- AI upscaling: +2 to +9 VMAF over bicubic scaling, 1 to 3.6 ms per source frame.
- AI frame generation: +5 to +10.7 VMAF over frame blending; LOW and MEDIUM keep 165 fps.
- Decoding + colour conversion 8 to 11 times faster on the GPU than on the CPU, VMAF 99.6 against the source.

### Known limits
- Frame generation HIGH takes about 10 ms per frame, too slow for one frame per refresh at 165 Hz (use MEDIUM,
  the default, or a fixed multiplier).
- No audio, no seeking, no on-screen menu and no HDR output yet.
- Needs an NVIDIA RTX GPU; frame generation needs RTX 40 or 50.

[Unreleased]: https://github.com/GearHead87/Nitid/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/GearHead87/Nitid/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/GearHead87/Nitid/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/GearHead87/Nitid/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/GearHead87/Nitid/releases/tag/v0.1.0
