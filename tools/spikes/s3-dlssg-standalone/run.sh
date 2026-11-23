#!/usr/bin/env bash
# S3: DLSS Frame Generation outside a game (Veyra's approach) in our own Vulkan app.
# Zero motion vectors, flat depth, stationary synthetic camera. For 2x / 4x / 6x it rebuilds real
# frames that were dropped and scores them (luma PSNR) against repeat / blend baselines and against
# VFX Video Frame Generation on exactly the same frames. Also times DLSS-G (normal and "lite").
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/s3-dlssg-standalone/s3-dlssg-standalone"
py="$root/third_party/vfx-venv/bin/python"
R="$root/results/s3-dlssg-standalone"
mkdir -p "$R/frames"
[ -x "$bin" ] || { echo "build first: cmake -S tools/spikes -B build-spikes -G Ninja && cmake --build build-spikes"; exit 1; }

# Raw RGBA inputs: 97 frames so steps 2, 4 and 6 all divide 96.
[ -f "$R/sintel_1080p_97.rgba" ] || ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/src_1080p.mp4" \
  -frames:v 97 -pix_fmt rgba -f rawvideo "$R/sintel_1080p_97.rgba"
[ -f "$R/drift_360p_97.rgba" ] || ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/drift_360p60.mp4" \
  -ss 2 -frames:v 97 -pix_fmt rgba -f rawvideo "$R/drift_360p_97.rgba"

for clip in sintel_1080p:1920:1080 drift_360p:640:360; do
  IFS=: read -r name w h <<<"$clip"
  in="$R/${name}_97.rgba"
  echo "### $name (${w}x${h}, 97 frames)"
  echo
  for step in 2 4 6; do
    for lite in 0 1; do
      "$bin" "$in" "$w" "$h" 97 "$step" "$R/frames" "$([ "$name" = sintel_1080p ] && [ $lite = 0 ] && echo 6 || echo 0)" "$lite" \
        2>"$R/${name}_step${step}_lite${lite}.ngx.log" | grep -E 'create|RESULT|TIMING|fail' | sed "s/^/lite=$lite /"
    done
    "$py" "$(dirname "$0")/compare_vfx.py" "$in" "$w" "$h" 97 "$step"
    echo
  done
done
