# Building and running Nitid

## Requirements (Arch / CachyOS)

- NVIDIA RTX GPU (frame generation: RTX 40/50) and the proprietary driver (tested: 620.73.04)
- `cmake ninja gcc` (C++23), `vulkan-headers vulkan-icd-loader`, `cuda` (driver API + `nvcc` for the
  kernels), `ffmpeg` (with NVDEC), `sdl3`, `cli11`
- NVIDIA VFX SDK:
  - C headers: `scripts/fetch-sdks.sh` (into `third_party/Maxine-VFX-SDK/nvvfx/include`)
  - runtime: `tools/spikes/vfx-env/setup.sh` (NVIDIA's `nvidia-vfx` wheel; the libraries in
    `third_party/vfx-venv/.../nvvfx/libs`), or `packaging/linux/nitid-setup-vfx` (NVIDIA's wheel, unpacked
    into `~/.local/share/nitid/vfx`). libnitid searches `$NITID_VFX_DIR`, then
    `$XDG_DATA_HOME/nitid/vfx/nvvfx/libs`, `<libnitid dir>/nitid/vfx/nvvfx/libs`, then the venv folder.
    Without it Nitid still plays video, with AI effects reported as unavailable.

## Build

```sh
cmake -S . -B build -G Ninja          # RelWithDebInfo by default
cmake --build build
ctest --test-dir build                # unit tests (no GPU needed)
```

Options: `-DNITID_BUILD_GUI=OFF`, `-DNITID_BUILD_CLI=OFF`, `-DNITID_BUILD_TESTS=OFF`,
`-DNITID_VFX_INCLUDE_DIR=...`, `-DNITID_VFX_RUNTIME_DIR=...`, `-DNITID_PTX_ARCH=compute_75`.
Building components separately (engine installed, apps against it) is described in PLAN.md section 6.

## Run

```sh
build/apps/nitid/nitid video.mp4                       # window, VSR HIGH, frame gen MEDIUM per refresh
build/apps/nitid/nitid video.mp4 --fullscreen --frame-gen low --multiplier 2
build/apps/nitid/nitid --help                          # all options; keys are printed at start (F1)

build/apps/nitid-cli/nitid-cli info [video.mp4]
build/apps/nitid-cli/nitid-cli bench video.mp4 --size 2560x1440 --frame-gen medium -o out.mkv
```

Keys in the player: Space pause, V upscaling on/off, B upscaling quality, G frame gen on/off,
H frame gen quality, 0/2/3/4 frame gen multiplier (0 = per refresh), C scene cuts, F/F11 fullscreen,
S print stats, R reset stats, F1 help, Esc/Q quit.

Environment: `NITID_LOG=debug` (more log, e.g. each scene cut), `NITID_VFX_DIR=/path/to/vfx/libs`.

## Release builds

`packaging/linux/docker-build.sh` builds the release tarball (bundled FFmpeg/SDL3) in Docker, like GitHub
Actions does. See [releasing.md](releasing.md).

## Tests

- Unit tests: `ctest --test-dir build`
- 0.1 feature tests (open windows, need test media from `scripts/fetch-media.sh`):
  [tests/v0.1/README.md](../tests/v0.1/README.md)
- Release tarball on a GPU machine: [tests/release/README.md](../tests/release/README.md)
- Phase 0 feasibility spikes: [tools/spikes/README.md](../tools/spikes/README.md)
