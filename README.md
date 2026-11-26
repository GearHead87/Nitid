# Nitid

**Nitid** (adjective): bright, shining, clear.

From the Latin *nitidus*, "shining, gleaming, polished", from *nitere*, "to shine".

AI upscaling and AI frame generation for video and screen capture on Linux, with NVIDIA RTX GPUs (NVIDIA VFX
Video Super Resolution and Video Frame Generation), paced to your display's refresh rate.

## Install (Linux x86_64)

1. Download `nitid-X.Y.Z-linux-x86_64.tar.gz` from [Releases](https://github.com/GearHead87/Nitid/releases)
   and extract it anywhere.
2. Install NVIDIA's VFX runtime once (about 450 MB, NVIDIA's license): `bin/nitid-setup-vfx`
3. `bin/nitid-cli info` shows what Nitid sees; `bin/nitid video.mp4` plays a video; `bin/nitid --capture-screen`
   enhances a screen or window.

Needs an NVIDIA RTX GPU (frame generation: RTX 40/50) with the proprietary driver, the Vulkan loader, and
glibc 2.38+ (Ubuntu 24.04+, Debian 13+, Fedora 39+, Arch).

## Documents

- [CHANGELOG.md](CHANGELOG.md): what each version changed
- [docs/building.md](docs/building.md): building from source, running
- [docs/releasing.md](docs/releasing.md): how releases are built and published
- [docs/architecture.md](docs/architecture.md): how the engine works
- [PLAN.md](PLAN.md): the plan and findings; tests in [tests/](tests/) and [tools/spikes/](tools/spikes/)
