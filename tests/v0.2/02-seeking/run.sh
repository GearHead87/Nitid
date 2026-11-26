#!/usr/bin/env bash
# 0.2 step 2: seeking.
#  A. Frame-exact: nitid-cli bench --start T must give exactly the frames a full decode gives at round(T*fps)
#     onwards (10 frames, framemd5 of the lossless output). Several codecs/containers, software decoding,
#     --source-step.
#  B. While playing (window, VSR + frame gen per refresh): 5 seeks forward and back through --test-seek; time to
#     the first frame after each seek, and missed refreshes.
#  C. Same on 4K60 HEVC / AV1 (longer decode from the keyframe).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.2/02"; mkdir -p "$R"

echo "### A. Frame-exact seeking (nitid-cli bench, 10 frames after the seek vs the same frames of a full decode)"
echo
echo "| File | Start (s) | Options | Frame | Same frames? |"
echo "|---|---|---|---|---|"
check() {  # file start [bench options...]
  local f=$1 t=$2; shift 2
  "$nitid_cli" bench "$media/$f" --upscale off --frames 400 --lossless -o "$R/full.mkv" "$@" >/dev/null 2>&1
  "$nitid_cli" bench "$media/$f" --upscale off --start "$t" --frames 10 --lossless -o "$R/seek.mkv" "$@" >/dev/null 2>&1
  local fps idx a b
  fps=$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of csv=p=0 "$R/full.mkv")
  idx=$(python3 -c "from fractions import Fraction as F; print(round($t * float(F('$fps'))))")
  a=$(ffmpeg -v error -i "$R/full.mkv" -vf "select=between(n\,$idx\,$((idx + 9)))" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' | md5sum)
  b=$(ffmpeg -v error -i "$R/seek.mkv" -f framemd5 - | grep -v '^#' | awk -F, '{print $6}' | md5sum)
  echo "| $f | $t | ${*:-} | $idx | $([ "$a" = "$b" ] && echo "✅ identical" || echo "❌ different") |"
}
check src_1080p.mp4 5.0
check src_1080p.mp4 3.37
check sintel_trailer-1080p.mp4 7.5
check src_4k_hevc.mkv 4.2
check src_4k_av1.mkv 2.0
check src_4k_h264.mkv 6.0
check src_1080p.mp4 5.0 --source-step 2
check src_1080p.mp4 5.0 --sw-decode
rm -f "$R/full.mkv" "$R/seek.mkv"

seeks() {  # file -> table rows
  local f=$1; shift
  play 9 "$media/$f" --test-seek 1.5:30 3:10 4.5:45 6:0 7.5:51 "$@" >/dev/null
  local lat; lat=$(grep -oP 'first frame ready after \K[0-9]+' "$log" | paste -sd' ')
  echo "| $f | ${*:-VSR high + FG medium} | $lat | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') |"
}
echo
echo "### B / C. Seeking while playing: 5 seeks (to 30, 10, 45, 0, 51 s), 9 s run"
echo
echo "| File | Settings | Time to first frame after each seek (ms) | Display fps | Missed refreshes |"
echo "|---|---|---|---|---|"
seeks sintel_trailer-1080p.mp4
seeks sintel_trailer-1080p.mp4
seeks src_1080p.mp4 --loop
seeks src_4k_hevc.mkv --loop
seeks src_4k_av1.mkv --loop
seeks src_4k_hevc.mkv --loop --upscale off --frame-gen off
