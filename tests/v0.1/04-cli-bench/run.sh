#!/usr/bin/env bash
# 0.1 step 4: nitid-cli bench (same pipeline, no window). Whole clips, scored with scripts/quality.sh
# (PSNR / SSIM / VMAF against the real high-quality video).
#  A. Upscaling: 540p / 720p clips -> 1080p, plain bicubic (VSR off) vs VSR HIGH / ULTRA.
#  B. Frame generation: keep every 2nd frame of a 24 fps clip, rebuild the rest at 2x, compare all frames
#     with the original. Baselines: repeat each frame, FFmpeg blend. Scene-cut detection on / off on the
#     Sintel trailer (many cuts).
#  C. Speed: throughput for typical settings.
# All saved outputs are lossless (FFV1), so scores compare the methods, not an extra encode; each file is
# deleted after scoring (they are large).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.1/04"; mkdir -p "$R"
q() { "$root/scripts/quality.sh" "$1" "$2"; }   # -> "psnr ssim vmaf"
row() { read -r p s v <<<"$(q "$1" "$2")"; echo "| $3 | $p | $s | $v |"; }
rowrm() { row "$@"; rm -f "$1"; }   # score, then delete the (large, lossless) file
lossless=(-c:v ffv1 -level 3 -pix_fmt yuv444p)
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }

echo "### A. Upscaling to 1080p (whole 10 s clip vs the real 1080p video)"
echo
echo "| Source / method | PSNR | SSIM | VMAF |"
echo "|---|---|---|---|"
for src in lq_540p lq_720p vlq_540p; do
  row "$media/$src.mp4" "$media/src_1080p.mp4" "$src, plain bicubic"
  for u in high ultra; do
    bench "$media/$src.mp4" --upscale $u --size 1920x1080 --lossless -o "$R/a_${src}_$u.mkv" >/dev/null
    rowrm "$R/a_${src}_$u.mkv" "$media/src_1080p.mp4" "$src, **nitid VSR $u**"
  done
done

echo
echo "### B. Frame generation: rebuild every 2nd frame (all frames vs the original)"
echo
echo "| Clip / method | PSNR | SSIM | VMAF |"
echo "|---|---|---|---|"
for clip in src_1080p sintel_trailer-1080p; do
  ref="$media/$clip.mp4"
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$ref" -vf "select='not(mod(n,2))',setpts=N/12/TB,fps=24" \
    "${lossless[@]}" "$R/b_${clip}_repeat.mkv"
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$ref" -vf "select='not(mod(n,2))',setpts=N/12/TB,framerate=24" \
    "${lossless[@]}" "$R/b_${clip}_blend.mkv"
  rowrm "$R/b_${clip}_repeat.mkv" "$ref" "$clip, repeat each kept frame"
  rowrm "$R/b_${clip}_blend.mkv" "$ref" "$clip, FFmpeg blend"
  bench "$ref" --upscale off --frame-gen medium --multiplier 2 --source-step 2 --lossless -o "$R/b_${clip}_fg.mkv" >"$R/b_${clip}_fg.txt"
  rowrm "$R/b_${clip}_fg.mkv" "$ref" "$clip, **nitid frame gen MEDIUM, scene cuts on** ($(grep -oP 'Scene cuts detected \| \K[0-9]+' "$R/b_${clip}_fg.txt") cuts)"
  bench "$ref" --upscale off --frame-gen medium --multiplier 2 --source-step 2 --no-scene-cuts --lossless -o "$R/b_${clip}_fg_nocuts.mkv" >/dev/null
  rowrm "$R/b_${clip}_fg_nocuts.mkv" "$ref" "$clip, nitid frame gen MEDIUM, scene cuts off"
done

echo
echo "### C. Speed (no file written; throughput after the first frame)"
echo
echo "| Input | Settings | Output | Throughput (fps) | x real time | Decode+convert ms | Upscale ms | Frame gen ms |"
echo "|---|---|---|---|---|---|---|---|"
speed() {  # file label args...
  local f=$1 label=$2; shift 2
  local out; out=$(bench "$media/$f" "$@")
  echo "| $f | $label | $(grep -oP '^Output\s+\K\S+ @ [0-9.]+' <<<"$out") | $(grep -oP 'Throughput \| \K[0-9.]+' <<<"$out") | $(grep -oP '\(\K[0-9.]+(?=x real time)' <<<"$out") | $(grep -oP 'Decode \+ convert.*\| \K[0-9.]+' <<<"$out") | $(grep -oP 'Upscale \(per source frame\) \| \K[0-9.]+' <<<"$out") | $(grep -oP 'Frame gen \(per generated frame\) \| \K[0-9.]+' <<<"$out") |"
}
speed src_1080p.mp4 "nothing (decode only)" --upscale off
speed lq_540p.mp4 "VSR HIGH to 1080p" --size 1920x1080
speed src_1080p.mp4 "VSR HIGH to 1440p" --size 2560x1440
speed src_1080p.mp4 "VSR HIGH to 4K" --size 3840x2160
speed src_1080p.mp4 "frame gen MEDIUM 2x" --upscale off --frame-gen medium
speed src_1080p.mp4 "VSR HIGH 1440p + frame gen MEDIUM 4x" --size 2560x1440 --frame-gen medium --multiplier 4
speed src_4k_hevc.mkv "nothing (decode only, 4K)" --upscale off
