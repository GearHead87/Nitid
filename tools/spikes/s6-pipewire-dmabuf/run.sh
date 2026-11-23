#!/usr/bin/env bash
# S6: screen capture via xdg-desktop-portal + PipeWire, frames imported into Vulkan as DMA-BUF.
# First run shows KDE's permission dialog (choose a monitor, click Share). The restore token is saved
# in results/s6-pipewire-dmabuf/restore_token.txt (git-ignored), so later runs don't ask again.
# The saved frame (frame_dmabuf.ppm) is a screenshot of your desktop: keep it out of git.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
R="$root/results/s6-pipewire-dmabuf"; mkdir -p "$R"
timeout 120 "$root/build-spikes/s6-pipewire-dmabuf/s6-pipewire-dmabuf" "$R" "${1:-120}" 60 | tee "$R/run.log"
