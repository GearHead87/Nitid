#!/usr/bin/env bash
# 0.3 step 2: side-by-side comparison (original | Nitid) in the window.
#  A. Right halves: with the line in the middle, the window's right half must be exactly Nitid's normal picture
#     and the left half exactly the plainly scaled original (window images, 540p in a 1920x1080 window, frame gen
#     off so both captures show the same frame).
#  B. Picture: VSR HIGH vs original, crop around the line (saved as JPEG).
#  C. Cost: 12 s with VSR HIGH + frame gen MEDIUM per refresh, compare on vs off.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
here=$(cd "$(dirname "$0")" && pwd)
R="$root/results/v0.3/02"; mkdir -p "$R"
clip="$media/lq_540p.mp4"
cap() {  # name [options]
  local name=$1; shift
  (cd "$R" && rm -f "${name}_3.00s.ppm" && timeout 60 "$nitid" "$clip" --width 1920 --height 1080 --frame-gen off --capture-at 3.0 \
     --capture-prefix "$name" --volume 0.2 "$@" >/dev/null 2>&1) || true
}
half() { ffmpeg -hide_banner -loglevel error -y -i "$R/$1_3.00s.ppm" -vf "crop=iw/2:ih:$([ "$2" = left ] && echo 0 || echo iw/2):0" "$R/$1_$2.png"; }
cap cmp_vsr --compare --upscale high
cap plain_vsr --upscale high
cap plain_off --upscale off
for n in cmp_vsr plain_vsr plain_off; do half $n left; half $n right; done
echo "### A. Halves (PSNR, inf = identical)"
echo
echo "| Compare | Against | PSNR |"
echo "|---|---|---|"
echo "| right half (Nitid side) | right half of Nitid's normal picture (VSR HIGH) | $(psnr "$R/cmp_vsr_right.png" "$R/plain_vsr_right.png") |"
echo "| left half (original side) | left half of the plainly scaled video (upscaling off) | $(psnr "$R/cmp_vsr_left.png" "$R/plain_off_left.png") |"
echo "| left half (original side) | left half of Nitid's picture (should differ) | $(psnr "$R/cmp_vsr_left.png" "$R/plain_vsr_left.png") |"
ffmpeg -hide_banner -loglevel error -y -i "$R/cmp_vsr_3.00s.ppm" -vf "crop=640:400:640:300,scale=1280:-1:flags=neighbor" -q:v 3 "$here/compare-vsr-crop.jpg"
ffmpeg -hide_banner -loglevel error -y -i "$R/cmp_vsr_3.00s.ppm" -vf "scale=1600:-1" -q:v 3 "$here/compare-vsr.jpg"
echo
echo "### B. Pictures: compare-vsr.jpg (whole window), compare-vsr-crop.jpg (around the line, 2x)"
echo
echo "### C. Cost (12 s, 1080p trailer, VSR HIGH + frame gen MEDIUM per refresh)"
echo
echo "| Compare | Display fps | Missed refreshes | Late frames | Copy to window (ms) |"
echo "|---|---|---|---|---|"
for c in "" --compare "" --compare; do
  # shellcheck disable=SC2086
  play 12 "$media/sintel_trailer-1080p.mp4" --volume 0.2 $c >/dev/null
  echo "| ${c:-off} | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') | $(field '([0-9]+) late frames') | $(field 'output \K[0-9.]+') |"
done
