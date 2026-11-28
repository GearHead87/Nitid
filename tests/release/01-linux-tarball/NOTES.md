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

**Tarball built by GitHub Actions** (release dry run 45791817755, 2026-11-26), on the same machine: checksum OK,
VFX effects available, Sintel trailer for 5 s with VSR + FG MEDIUM per refresh: 165.0 fps, 0 skipped,
1 missed refresh out of 786 frames. That is start-up jitter; the local runs had 0.

## 0.2.0 (2026-11-26)

Same test with the 0.2 additions ([output-2026-11-26-v0.2.0.md](output-2026-11-26-v0.2.0.md)): tarball 12 MB,
dependency check passes (PipeWire is loaded at run time, so it is not a link dependency). On the GPU: sound
through PipeWire in sync (A/V +2.3 ms, Bluetooth headphones 225 ms), seek to 30 s in 46 ms, FSR 1 0.15 ms, the
open-file screen runs, 165 fps. Release vs developer build: **identical frames** for VSR + frame gen 2x and for
FSR 1 after a seek. (lq_540p had 26 missed refreshes in its single 6 s run; the others 0.)

## 0.3.0 (2026-11-26)

[output-2026-11-26-v0.3.0.md](output-2026-11-26-v0.3.0.md): tarball 12 MB, checks pass. On the GPU: frame gen
HIGH + compare view with render-ahead pacing (one frame every 2 refreshes, 165 fps, 2 missed refreshes in 8 s),
sound in sync (A/V −0.7 ms), seek + FSR 1, open screen. Release vs developer build: identical frames (the same
checksums as 0.2.0: the headless pipeline did not change).

## 0.4.0 (2026-11-27)

[output-2026-11-27-v0.4.0.md](output-2026-11-27-v0.4.0.md): tarball 12 MB, checks pass (libdbus and PipeWire are
loaded at run time, so still no new link dependencies). On the GPU: live input from a synthetic PipeWire source
(60 fps in, 165 fps out, 29 ms capture → screen), video playback, frame gen HIGH pacing, compare, seek + FSR 1.
Release vs developer build: identical frames. (Desktop screen capture is test v0.4/02; not repeated here.)

## 0.5.0 (2026-11-28)

[output-2026-11-28-v0.5.0.md](output-2026-11-28-v0.5.0.md): tarball 12 MB; the build now fetches the pinned DLSS SDK
(headers + `libnvsdk_ngx.a`, SHA-256 checked) and ships the DLSS module `lib/libnitid-dlss.so`, `bin/nitid-setup-dlss`
and NVIDIA's RTX SDKs license; dependency check and smoke test pass (the module needs only libvulkan / libstdc++ /
libc). From the tarball: `nitid-setup-dlss` installs 64 MB; `nitid-cli info` reports DLSS 310.9.16 (SR, FG up to 6x)
and optical flow; DLSS upscaling + DLSS frame generation 6x per refresh and DLSS + lite 4x in a large window both
165 fps, 0 missed refreshes. Release vs developer build: identical frames for VSR, FSR and the new DLSS + DLSS
frame generation 4x job (`6e633958…`).

The first run ([output-2026-11-28-v0.5.0-run1.md](output-2026-11-28-v0.5.0-run1.md)) had one bad row: the large-window
DLSS run at 29 fps with 138 missed refreshes, right after the live-input row that also had 101 missed refreshes. The
same command repeated twice with the release build and once with the developer build ran at 165 fps; the full re-run
above is clean. The desktop was busy, as seen before (memory: missed-refresh counts are noisy here).

Found while building: Ubuntu 24.04's Vulkan headers only know the old name of the optical flow format
(`VK_FORMAT_R16G16_S10_5_NV`, an alias of `..._SFIXED5_NV` in newer headers); fixed before the release.

## Findings

- **The DLSS runtime comes from NVIDIA's GitHub SDK repository** (raw files at a pinned tag, SHA-256 checked by
  `nitid-setup-dlss`); only NVIDIA's tiny static loader is compiled into Nitid's separate DLSS module.
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
