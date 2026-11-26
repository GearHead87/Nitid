#!/usr/bin/env bash
# 0.2 step 1: FSR 1 and NIS spatial upscalers (CUDA ports), next to plain bicubic and VFX VSR.
#  A. Quality: 540p / 720p clips -> 1080p, whole clip vs the real 1080p video (PSNR / SSIM / VMAF).
#     Nitid works in 8-bit RGB, which alone costs PSNR (44.8 dB ceiling against the YUV source), so the fair
#     baseline is bicubic inside Nitid; FFmpeg's bicubic (scaled in YUV) is shown too.
#  B. Speed: nitid-cli bench throughput and per-frame upscale time.
#  C. Window: 5 s playback with each upscaler at the display refresh rate.
# Outputs are lossless (FFV1) and deleted after scoring. Also saves a crop comparison (crop_*.jpg).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
here=$(cd "$(dirname "$0")" && pwd)
R="$root/results/v0.2/01"; mkdir -p "$R"
q() { "$root/scripts/quality.sh" "$1" "$2"; }
row() { read -r p s v <<<"$(q "$1" "$2")"; echo "| $3 | $p | $s | $v |"; }
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }

echo "### A. Quality, upscaled to 1080p (whole 10 s clip vs the real 1080p video)"
echo
echo "| Source / method | PSNR | SSIM | VMAF |"
echo "|---|---|---|---|"
for src in lq_540p lq_720p vlq_540p; do
  row "$media/$src.mp4" "$media/src_1080p.mp4" "$src, FFmpeg bicubic (no RGB round trip)"
  for m in "bicubic 0" "fsr 0" "fsr 0.5" "nis 0" "nis 0.5" "high 0.5"; do
    read -r u sh <<<"$m"
    out="$R/a_${src}_${u}_${sh}.mkv"
    bench "$media/$src.mp4" --upscale "$u" --sharpness "$sh" --size 1920x1080 --lossless -o "$out" >/dev/null
    case $u in
      high) label="$src, **VSR HIGH**" ;;
      bicubic) label="$src, bicubic inside Nitid (VSR's bicubic mode)" ;;
      *) label="$src, **${u^^} sharpness $sh**" ;;
    esac
    row "$out" "$media/src_1080p.mp4" "$label"
    if [ "$src" = lq_540p ] && [ "$sh" = 0.5 ]; then cp "$out" "$R/keep_$u.mkv"; fi
    rm -f "$out"
  done
done

# Crop of frame 100: bicubic | FSR | NIS | VSR | real 1080p
c="select=eq(n\,100),crop=480:300:700:400"
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$media/lq_540p.mp4" -i "$R/keep_fsr.mkv" -i "$R/keep_nis.mkv" \
  -i "$R/keep_high.mkv" -i "$media/src_1080p.mp4" -filter_complex \
  "[0]select=eq(n\,100),scale=1920:1080:flags=bicubic,crop=480:300:700:400[a];[1]$c[b];[2]$c[c];[3]$c[d];[4]$c[e];[a][b][c][d][e]hstack=5" \
  -frames:v 1 -q:v 3 "$here/crop_bicubic-fsr-nis-vsr-real.jpg"
rm -f "$R"/keep_*.mkv

echo
echo "### B. Speed (nitid-cli bench, no file written)"
echo
echo "| Input | Upscaler | Output | Throughput (fps) | Upscale ms per frame |"
echo "|---|---|---|---|---|"
for job in "lq_540p.mp4 1920x1080" "src_1080p.mp4 3840x2160"; do
  read -r f size <<<"$job"
  for u in fsr nis high; do
    out=$(bench "$media/$f" --upscale "$u" --size "$size")
    echo "| $f | $u | $(grep -oP '^Output\s+\K\S+' <<<"$out") | $(grep -oP 'Throughput \| \K[0-9.]+' <<<"$out") | $(grep -oP 'Upscale \(per source frame\) \| \K[0-9.]+' <<<"$out") |"
  done
done

echo
echo "### C. Window playback (5 s, 540p in a 1920x1080 window, frame gen MEDIUM per refresh)"
echo
echo "| Upscaler | Upscale | Display fps | Missed refreshes | Skipped |"
echo "|---|---|---|---|---|"
for u in fsr nis high; do
  play 5 "$media/lq_540p.mp4" --upscale "$u" --width 1920 --height 1080 >/dev/null
  echo "| $u | $(field 'Upscale:\s+\K.*') | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') | $(field '([0-9]+) skipped') |"
done
