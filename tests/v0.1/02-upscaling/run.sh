#!/usr/bin/env bash
# 0.1 step 2: AI upscaling (VFX VSR) in the player.
#  A. Right frame: the window image at t = 3.00 s must be frame 72 (24 fps), with VSR off and on.
#  B. Quality: 540p / 720p versions of the clip upscaled into the 2048x1152 window, compared with the real
#     1080p frame (Lanczos-scaled to 2048x1152). Plain blit (VSR off) vs VSR HIGH / ULTRA.
#  C. Speed: VSR time per source frame and display stats for 540p / 720p / 1080p sources.
# Window 2048x1152, frame generation off.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.1/02"; mkdir -p "$R"
win=(--width 2048 --height 1152 --frame-gen off)

for n in 71 72 73; do frame "$media/src_1080p.mp4" $n "$R/ref_$n.ppm" 2048x1152; done

echo "### A. Right frame (window image at t = 3.00 s vs real frames 71 / 72 / 73, PSNR dB)"
echo
echo "| Upscaling | vs 71 | vs 72 (expected) | vs 73 |"
echo "|---|---|---|---|"
for u in off high; do
  play 5 "$media/src_1080p.mp4" "${win[@]}" --upscale $u --capture-at 3.0 --capture-prefix "$R/a_$u" >/dev/null
  img="$R/a_${u}_3.00s.ppm"
  echo "| $u | $(psnr "$img" "$R/ref_71.ppm") | **$(psnr "$img" "$R/ref_72.ppm")** | $(psnr "$img" "$R/ref_73.ppm") |"
done

echo
echo "### B. Quality vs the real 1080p frame 72 (PSNR dB, higher is better)"
echo
echo "| Source | Plain blit (VSR off) | VSR HIGH | VSR ULTRA |"
echo "|---|---|---|---|"
for src in lq_540p lq_720p vlq_540p; do
  row="| $src"
  for u in off high ultra; do
    play 5 "$media/$src.mp4" "${win[@]}" --upscale $u --capture-at 3.0 --capture-prefix "$R/b_${src}_$u" >/dev/null
    row="$row | $(psnr "$R/b_${src}_${u}_3.00s.ppm" "$R/ref_72.ppm")"
  done
  echo "$row |"
done

echo
echo "### C. Speed (8 s each)"
echo
echo "| Source | VSR | Work size | VSR ms per source frame | Output fps | Missed refreshes | Skipped frames |"
echo "|---|---|---|---|---|---|---|"
for src in lq_540p lq_720p src_1080p; do
  for u in high ultra; do
    play 8 "$media/$src.mp4" "${win[@]}" --upscale $u >/dev/null
    echo "| $src | $u | $(field 'Work:\s+\K[0-9x]+') | $(field 'upscale \K[0-9.]+') | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+)(?= missed refreshes)') | $(field '([0-9]+)(?= skipped)') |"
  done
done
