# Building and running Nitid

## Requirements (Arch / CachyOS)

- NVIDIA RTX GPU (frame generation: RTX 40/50) and the proprietary driver (tested: 620.73.04)
- `cmake ninja gcc` (C++23), `vulkan-headers vulkan-icd-loader`, `cuda` (driver API + `nvcc` for the
  kernels), `ffmpeg` (with NVDEC), `sdl3`, `cli11`, `pipewire` and `dbus` (headers; both libraries are loaded at
  run time, for sound and screen capture), `gst-plugin-pipewire` (only for the live-input tests)
- Network on the first configure: Dear ImGui 1.93.0 is downloaded by CMake (pinned hash). Offline:
  `-DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui-1.93.0`
- NVIDIA VFX SDK:
  - C headers: `scripts/fetch-sdks.sh` (into `third_party/Maxine-VFX-SDK/nvvfx/include`)
  - runtime: `tools/spikes/vfx-env/setup.sh` (NVIDIA's `nvidia-vfx` wheel; the libraries in
    `third_party/vfx-venv/.../nvvfx/libs`), or `packaging/linux/nitid-setup-vfx` (NVIDIA's wheel, unpacked
    into `~/.local/share/nitid/vfx`). libnitid searches `$NITID_VFX_DIR`, then
    `$XDG_DATA_HOME/nitid/vfx/nvvfx/libs`, `<libnitid dir>/nitid/vfx/nvvfx/libs`, then the venv folder.
    Without it Nitid still plays video, with AI effects reported as unavailable.
- NVIDIA DLSS SDK (optional; DLSS upscaling and frame generation): `scripts/fetch-sdks.sh` (into
  `third_party/DLSS`). With it, the build also makes the DLSS module `libnitid-dlss.so` next to libnitid, and
  developer builds use the SDK's own runtime libraries. Without it the module is skipped and DLSS is unavailable.
  The runtime is searched in `$NITID_DLSS_DIR` (only there, if set), `$XDG_DATA_HOME/nitid/dlss` (what
  `packaging/linux/nitid-setup-dlss` installs), `<libnitid dir>/nitid/dlss`, then the SDK folder.
  `NITID_NGX_LOG=1` turns on NVIDIA's NGX log.

## Build

```sh
cmake -S . -B build -G Ninja          # RelWithDebInfo by default
cmake --build build
ctest --test-dir build                # unit tests (no GPU needed)
```

Options: `-DNITID_BUILD_GUI=OFF`, `-DNITID_BUILD_CLI=OFF`, `-DNITID_BUILD_TESTS=OFF`,
`-DNITID_VFX_INCLUDE_DIR=...`, `-DNITID_VFX_RUNTIME_DIR=...`, `-DNITID_DLSS_SDK_DIR=...`,
`-DNITID_DLSS_RUNTIME_DIR=...`, `-DNITID_PTX_ARCH=compute_75`.
Building components separately (engine installed, apps against it) is described in PLAN.md section 6.

## Run

```sh
build/apps/nitid/nitid video.mp4                       # window, VSR HIGH, frame gen MEDIUM per refresh, sound
build/apps/nitid/nitid                                 # no file: open one from the window (or drop it on it)
build/apps/nitid/nitid video.mp4 --fullscreen --frame-gen low --multiplier 2 --upscale fsr --start 60
build/apps/nitid/nitid --capture-screen               # screen capture (the desktop asks which screen / window)
build/apps/nitid/nitid --help                          # all options; F1 in the window lists the keys

build/apps/nitid-cli/nitid-cli info [video.mp4]
build/apps/nitid-cli/nitid-cli bench video.mp4 --size 2560x1440 --frame-gen medium -o out.mkv
```

Player: move the mouse for the control bar (seek bar, volume, Settings / Stats panels). Keys: Space pause,
Left/Right ±5 s, PgDn/PgUp ±60 s, Home start, Up/Down volume, M mute, `,`/`.` A/V offset, V upscaler
(VSR / FSR 1 / NIS / off), B VSR quality, G frame gen on/off, H frame gen quality, 0/2/3/4 frames per source
frame (0 = per refresh), C scene cuts, L live mode (low latency / smooth), X compare original | Nitid, P settings, I stats, O open, Tab hide controls, F/F11 or double-click
fullscreen, F1 keys, Esc/Q quit.

Environment: `NITID_LOG=debug` (more log, e.g. each scene cut), `NITID_VFX_DIR=/path/to/vfx/libs`.

## Release builds

`packaging/linux/docker-build.sh` builds the release tarball (bundled FFmpeg/SDL3) in Docker, like GitHub
Actions does. See [releasing.md](releasing.md).

## Tests

- Unit tests: `ctest --test-dir build`
- Feature tests (open windows, need test media from `scripts/fetch-media.sh`):
  [tests/v0.1](../tests/v0.1/README.md), [v0.2](../tests/v0.2/README.md), [v0.3](../tests/v0.3/README.md),
  [v0.4](../tests/v0.4/README.md)
- Release tarball on a GPU machine: [tests/release/README.md](../tests/release/README.md)
- Phase 0 feasibility spikes: [tools/spikes/README.md](../tools/spikes/README.md)
