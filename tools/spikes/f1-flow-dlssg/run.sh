#!/usr/bin/env bash
# F1: optical flow (S4) -> motion vectors -> DLSS-G (S3), end to end.
# 1. Convention probe (Sintel, 2x, 4x4 flow): mvecScale 1 ("px") vs 1/size ("norm"), sign +1 / -1,
#    full and lite DLSS-G. Lite mode is a plain blend without usable vectors, so it shows clearly
#    which convention DLSS-G actually reads.
# 2. Matrix with the right convention: motion zero / NVOF 4x4 / NVOF 1x1, full and lite, 2x/4x/6x,
#    Sintel 1080p and Drift 360p (same 97 frames as S3; VFX VFG numbers are in S3's output).
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/f1-flow-dlssg/f1-flow-dlssg"
S3="$root/results/s3-dlssg-standalone"
R="$root/results/f1-flow-dlssg"
mkdir -p "$R/frames"
[ -f "$S3/sintel_1080p_97.rgba" ] || { echo "run tools/spikes/s3-dlssg-standalone/run.sh first (makes the raw frames)"; exit 1; }

echo "## 1. Convention probe (Sintel 1080p, 2x, NVOF 4x4)"
echo
for lite in 0 1; do
  for sc in px norm; do
    for sg in +1 -1; do
      "$bin" "$S3/sintel_1080p_97.rgba" 1920 1080 97 2 "$R/frames" nvof4 "$sg" "$lite" 0 "$sc" 2>/dev/null | grep RESULT
    done
  done
done
echo
echo "## 2. Matrix (mvecScale = 1, sign +1)"
for clip in sintel_1080p:1920:1080 drift_360p:640:360; do
  IFS=: read -r name w h <<<"$clip"
  echo
  echo "### $name"
  for step in 2 4 6; do
    for mv in zero nvof4 nvof1; do
      for lite in 0 1; do
        save=0
        [ "$name" = sintel_1080p ] && [ "$mv" != zero ] && save=6
        "$bin" "$S3/${name}_97.rgba" "$w" "$h" 97 "$step" "$R/frames" "$mv" +1 "$lite" "$save" px 2>/dev/null | grep RESULT
      done
    done
  done
done
