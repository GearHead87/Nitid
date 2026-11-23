#!/usr/bin/env bash
# S7: opens a window (~4 s per present mode, slow low-contrast colour, no flashing) and measures the
# interval between displayed frames with VK_KHR_present_id + VK_KHR_present_wait.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$root/results/s7-present-timing"
timeout 120 "$root/build-spikes/s7-present-timing/s7-present-timing" "${1:-4}" | tee "$root/results/s7-present-timing/run.log"
