# Release test 01: Linux tarball

**Question:** does the release tarball, built in Docker on Ubuntu 24.04 the same way GitHub Actions builds it,
work on a real machine the way a user would use it?

**Machine:** Arch (CachyOS), RTX 5080, driver 620.73.04, 165 Hz display.
**Run:** `tests/release/01-linux-tarball/run.sh` (add `--no-build` to reuse `out/`). Output:
[output-2026-11-26.md](output-2026-11-26.md).

## Result: ✅

| Check | Result |
|---|---|
| Build in Docker (`packaging/linux/docker-build.sh`) | Unit tests pass, dependency check passes, both programs start. Tarball 12 MB |
| Host libraries needed | glibc (newest symbol GLIBC_2.38), libstdc++ (GLIBCXX_3.4.32 = GCC 13), libgcc_s, zlib, libvulkan.so.1, libcuda.so.1. Everything else is bundled |
| Before the VFX runtime | `nitid-cli info` reports "VFX runtime not installed (run nitid-setup-vfx...)" and the hint; video still plays without AI effects |
| `nitid-setup-vfx --yes` | Downloads NVIDIA's wheel (443 MB), checks the SHA-256, unpacks 578 MB of libraries. Second run: "already installed". `--uninstall` removes it |
| After | Runtime 1.3.0.0 found automatically in `$XDG_DATA_HOME/nitid/vfx/nvvfx/libs`; upscaling and frame gen available |
| Bundled FFmpeg 9.1 | 4K AV1 decoded with NVDEC + GPU colour conversion |
| Window playback (VSR HIGH + FG MEDIUM per refresh, 6 s each) | 540p, 4K AV1 and the 1080p Sintel trailer: 165.0 fps measured, 0 missed refreshes, 0 skipped |
| Same output as the developer build | `nitid-cli bench` 540p → 1080p, VSR + FG 2x, 96 frames, lossless: **identical frames** (same framemd5, PSNR inf) |

## Findings

- **NVIDIA's `nvidia-vfx` wheel is only on pypi.nvidia.com.** On PyPI there is just a 3.7 KB stub source
  package. `nitid-setup-vfx` pins the wheel URL and its SHA-256.
- **The wheel's libraries need its folder layout.** `libVideoFX.so` and the effect libraries load the CUDA
  runtime through `RPATH $ORIGIN/../../nvidia_vfx.libs`, so the script keeps `nvvfx/libs/` and
  `nvidia_vfx.libs/` side by side.
- **Linking without a GPU driver:** the build machine has only CUDA's link stub `libcuda.so`, but linking the
  programs checks libnitid's own needs (`libcuda.so.1`, and FFmpeg's `libswresample`). The release build gives
  the linker `-rpath-link` to a `libcuda.so.1` made from the stub. The binaries still need the real driver
  library.
- **Older distros:** the binaries need glibc 2.38 and libstdc++ from GCC 13, not GCC 14, although GCC 14
  compiled them. They run on Ubuntu 24.04+, Debian 13+, Fedora 39+ and Arch. CI checks that they start on
  Ubuntu 24.04, Debian 13, Fedora and Arch containers.

## Storage

- Docker image `nitid-build:ubuntu-24.04`: 1.9 GB on `/`, kept for future releases.
- Build volume `nitid-release-work`: 205 MB.
- Test files go to `/tmp` and are removed at the end. Only the tarball is written to `out/` (git-ignored).
