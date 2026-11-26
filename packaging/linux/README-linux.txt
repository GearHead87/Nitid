Nitid for Linux (x86_64)
=======================

AI upscaling and frame generation for video, on NVIDIA RTX GPUs.

Requirements
  - NVIDIA RTX GPU (frame generation: RTX 40 or 50 series) and a recent proprietary NVIDIA driver (tested: 620.73)
  - A 64-bit Linux with glibc 2.38+ and libstdc++ from GCC 13+ (Ubuntu 24.04+, Debian 13+, Fedora 39+, Arch, ...)
  - The Vulkan loader (package vulkan-icd-loader / libvulkan1) and zlib
  - For sound: PipeWire (standard on current desktops; without it videos play silently)
  - Optional: ffmpeg on PATH (only for `nitid-cli bench -o` video output)

Start
  1. Extract this folder anywhere, e.g.  tar -xzf nitid-*-linux-x86_64.tar.gz
  2. Install NVIDIA's VFX runtime once (about 450 MB download, NVIDIA's license):
       ./bin/nitid-setup-vfx
  3. Check what Nitid sees:
       ./bin/nitid-cli info
  4. Play a video:
       ./bin/nitid video.mp4        (or start ./bin/nitid and open / drop a file in the window)

In the player, move the mouse for the controls (seek bar, volume, Settings and Stats panels); F1 lists the keys.
Space pause, Left/Right seek, Up/Down volume, M mute, V upscaler (VSR / FSR 1 / NIS / off), G frame gen,
P settings, I stats, Tab hide controls, F or double-click fullscreen, Esc quit.
All options: ./bin/nitid --help   and   ./bin/nitid-cli --help

Files
  bin/            nitid (player), nitid-cli (info, bench), nitid-setup-vfx
  lib/            libnitid and the bundled FFmpeg and SDL3 libraries
  share/doc/nitid CHANGELOG.md, README.md, THIRD-PARTY-NOTICES.md, licenses/

Source, issues and releases: https://github.com/GearHead87/Nitid
