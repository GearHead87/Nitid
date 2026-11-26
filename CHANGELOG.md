# Changelog

All notable changes to Nitid. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
versions follow [Semantic Versioning](https://semver.org/) (0.x: minor versions may change behaviour).

How releases use this file: `scripts/bump-version.sh X.Y.Z` turns "Unreleased" into a version section, and the
release workflow puts that section at the top of the GitHub release, followed by the list of commits.

## [Unreleased]

### Added
- **FSR 1 and NIS upscaling** (`--upscale fsr|nis`, `--sharpness 0..1`; `V` cycles VSR / FSR 1 / NIS / off):
  AMD FidelityFX FSR 1 (EASU + RCAS) and NVIDIA Image Scaling, ported to CUDA. Work on any NVIDIA GPU without
  the VFX runtime; 0.06–0.08 ms per 1080p frame. NIS scales up to 2x.
- **Seeking**: Left / Right ±5 s, PgDn / PgUp ±60 s, Home; `--start` in both apps. Frame-exact; the first frame
  after a seek takes 11–38 ms at 1080p.
- **Sound** through PipeWire, in sync with the picture: the picture stays locked to the display and the sound
  follows it (resampled by at most 1 %, resync after seeks / pauses). Up / Down volume, M mute, `,` / `.` move
  the sound by 10 ms; `--volume`, `--audio-delay`, `--no-audio`. Works with Bluetooth latency (235 ms tested).

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

[Unreleased]: https://github.com/GearHead87/Nitid/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/GearHead87/Nitid/releases/tag/v0.1.0
