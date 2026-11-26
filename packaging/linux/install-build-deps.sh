#!/usr/bin/env bash
# Installs the packages needed to build a Nitid release on Ubuntu 24.04.
# Used by both the local Docker image (packaging/linux/Dockerfile) and GitHub Actions (ubuntu-24.04 runner),
# so the two build the same way. Run as root (or with sudo).
#
# Only build tools and headers come from apt. FFmpeg, SDL3 and CLI11 are built from source by build-deps.sh,
# because the release bundles them (distros ship different FFmpeg versions).
set -euo pipefail

CUDA_VERSION=${CUDA_VERSION:-12-9}   # nvcc for the PTX kernels; matches the VFX runtime (CUDA 12.9)

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl git xz-utils file patchelf \
  build-essential gcc-14 g++-14 cmake ninja-build pkg-config nasm \
  libvulkan-dev \
  zlib1g-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libegl-dev \
  libdbus-1-dev libudev-dev libpipewire-0.3-dev libpulse-dev libasound2-dev

# NVIDIA's CUDA apt repository: only nvcc and the driver API headers + link stub (not the whole toolkit).
if ! dpkg -s cuda-keyring >/dev/null 2>&1; then
  curl -fsSL -o /tmp/cuda-keyring.deb \
    https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
  dpkg -i /tmp/cuda-keyring.deb
  rm /tmp/cuda-keyring.deb
  apt-get update
fi
apt-get install -y --no-install-recommends \
  cuda-nvcc-${CUDA_VERSION} cuda-driver-dev-${CUDA_VERSION} cuda-cudart-dev-${CUDA_VERSION}

# build-release.sh uses gcc-14/g++-14 by name; make them the default too (the Docker image).
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 140 --slave /usr/bin/g++ g++ /usr/bin/g++-14 || true
apt-get clean
rm -rf /var/lib/apt/lists/*
