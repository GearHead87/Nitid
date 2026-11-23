#!/usr/bin/env bash
# S4: VK_NV_optical_flow capabilities, accuracy (known shift), usefulness (warp test) and speed.
# Input = later frame, reference = earlier frame, so the flow is "current -> previous" in pixels,
# which is the motion-vector convention DLSS expects.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/s4-nvof-pair/s4-nvof-pair"
R="$root/results/s4-nvof-pair"; mkdir -p "$R"
for f in pair_1080p_1 pair_1080p_2 pair_drift360_1 pair_drift360_2; do
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/frames/$f.png" "$R/$f.ppm"
done
"$bin" "$R/pair_1080p_2.ppm" "$R/pair_1080p_1.ppm" "$R" sintel1080 | tee "$R/out_sintel.md"
"$bin" "$R/pair_drift360_2.ppm" "$R/pair_drift360_1.ppm" "$R" drift360 | sed -n '/Known shift/,/## Speed/p' | tee "$R/out_drift.md"
