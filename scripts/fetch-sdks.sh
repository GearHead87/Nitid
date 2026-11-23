#!/usr/bin/env bash
# Download the NVIDIA SDKs used by the spikes into third_party/ (git-ignored).
#   third_party/DLSS                       NVIDIA DLSS SDK, tag v310.9.16 (include/, lib/Linux_x86_64/, doc/ only)
#   third_party/nvidia-vfx-python-samples  NVIDIA VFX Python samples (+ sample videos via git-lfs)
# Re-running is safe: existing checkouts are left alone.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
tp="$root/third_party"
mkdir -p "$tp"

DLSS_TAG=v310.9.16

if [ ! -d "$tp/DLSS/.git" ]; then
  echo ">> DLSS SDK $DLSS_TAG (sparse: include, lib/Linux_x86_64, doc)"
  git clone --depth 1 --branch "$DLSS_TAG" --filter=blob:none --sparse \
    https://github.com/NVIDIA/DLSS.git "$tp/DLSS"
  git -C "$tp/DLSS" sparse-checkout set include lib/Linux_x86_64 doc
else
  echo ">> DLSS SDK already present"
fi

if [ ! -d "$tp/nvidia-vfx-python-samples/.git" ]; then
  echo ">> NVIDIA VFX Python samples"
  git clone --depth 1 https://github.com/NVIDIA-Maxine/nvidia-vfx-python-samples.git \
    "$tp/nvidia-vfx-python-samples"
  git -C "$tp/nvidia-vfx-python-samples" lfs install --local
  git -C "$tp/nvidia-vfx-python-samples" lfs pull
else
  echo ">> VFX samples already present"
fi

echo ">> done"
git -C "$tp/DLSS" describe --tags 2>/dev/null | sed 's/^/DLSS: /'
git -C "$tp/nvidia-vfx-python-samples" log -1 --format='VFX samples: %h %cs' 2>/dev/null
