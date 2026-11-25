#!/usr/bin/env bash
# 0.1 follow-up: colour conversion on the GPU (NVDEC NV12/P010 -> RGBA8 with Nitid's CUDA kernel, no CPU copy).
#  A. Accuracy: whole clip passed through (no AI), GPU vs CPU conversion, both vs the source.
#  B. Speed: decode + convert per frame and throughput (headless), GPU path vs CPU path (--sw-decode).
#  C. Window playback of the 4K60 clips that failed in step 1 (8 s each, twice).
#  D. Scene cuts from the GPU luma thumbnail on the Sintel trailer (7 real cuts).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.1/05"; mkdir -p "$R"

echo "### A. Accuracy (whole 10 s clip, lossless output vs the source)"
echo
echo "| Conversion | PSNR | SSIM | VMAF |"
echo "|---|---|---|---|"
for mode in gpu cpu; do
  extra=(); [ $mode = cpu ] && extra=(--sw-decode)
  "$nitid_cli" bench "$media/src_1080p.mp4" --upscale off --lossless -o "$R/pass_$mode.mkv" "${extra[@]}" >/dev/null 2>&1
  read -r p s v <<<"$("$root/scripts/quality.sh" "$R/pass_$mode.mkv" "$media/src_1080p.mp4")"
  echo "| $mode | $p | $s | $v |"
done
read -r p s v <<<"$("$root/scripts/quality.sh" "$R/pass_gpu.mkv" "$R/pass_cpu.mkv")"
echo "| gpu vs cpu | $p | $s | $v |"
rm -f "$R"/pass_*.mkv

echo
echo "### B. Speed (headless, no AI)"
echo
echo "| Clip | Path | Decode + convert ms per frame | Throughput |"
echo "|---|---|---|---|"
for f in src_1080p.mp4 src_4k_h264.mkv src_4k_hevc.mkv src_4k_av1.mkv; do
  for mode in gpu cpu; do
    extra=(); [ $mode = cpu ] && extra=(--sw-decode)
    o=$("$nitid_cli" bench "$media/$f" --upscale off "${extra[@]}" 2>/dev/null)
    echo "| $f | $(grep -oP 'decoder \K.*(?=\)$)' <<<"$o") | $(grep -oP 'Decode \+ convert[^|]*\| \K[0-9.]+' <<<"$o") | $(grep -oP 'Throughput \| \K[^|]+' <<<"$o")|"
  done
done

echo
echo "### C. Window playback, 4K60 (8 s, twice; window 2048x1152, no AI)"
echo
echo "| Clip | Output fps | Missed refreshes (run 1 / 2) | Skipped frames (run 1 / 2) | Decode + convert ms |"
echo "|---|---|---|---|---|"
for c in src_4k_hevc.mkv src_4k_av1.mkv; do
  play 8 "$media/$c" --upscale off --frame-gen off >/dev/null
  m1=$(field '([0-9]+)(?= missed refreshes)'); s1=$(field '([0-9]+)(?= skipped)')
  play 8 "$media/$c" --upscale off --frame-gen off >/dev/null
  echo "| $c | $(field 'Display:\s+\K[0-9.]+') | $m1 / $(field '([0-9]+)(?= missed refreshes)') | $s1 / $(field '([0-9]+)(?= skipped)') | $(field 'decode\+convert \K[0-9.]+') |"
done

echo
echo "### D. Scene cuts on the Sintel trailer (every 2nd frame, GPU luma thumbnail)"
echo
cuts=$("$nitid_cli" bench "$media/sintel_trailer-1080p.mp4" --upscale off --source-step 2 -v 2>&1 \
  | grep -oP "scene cut at frame \K[0-9]+" | awk '{printf "%d ", $1*2}')
echo "Detected (original frame numbers): $cuts"
echo
echo "Real cuts (checked by eye in 04): 396 420 572 622 658 672 680"
