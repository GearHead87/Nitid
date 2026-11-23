#!/usr/bin/env bash
# V5: Does VFX VSR look better than ordinary scaling?
# Low-resolution copies of the 1080p reference are upscaled back to 1080p with:
#   ffmpeg bicubic, ffmpeg lanczos, and VFX VSR modes (via NVIDIA's sample, HEVC 16 Mbit/s output).
# Baselines are encoded the same way (hevc_nvenc 16 Mbit/s) so the comparison is fair.
# Each result is scored against the original (PSNR / SSIM / VMAF), plus crop grids for viewing.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
py="$root/third_party/vfx-venv/bin/python"
samples="$root/third_party/nvidia-vfx-python-samples"
m="$root/results/media"
out="$root/results/v5-vsr-quality"
mkdir -p "$out/crops"
ref="$m/src_1080p.mp4"
ff=(ffmpeg -hide_banner -loglevel error -nostdin -y)

# Extra input: 540p heavily compressed (400 kbit/s) to test artifact reduction while upscaling.
vlq540="$m/vlq_540p.mp4"
[ -f "$vlq540" ] || "${ff[@]}" -i "$ref" -vf scale=960:540:flags=bicubic -c:v libx264 -preset veryfast \
  -b:v 400k -maxrate 400k -bufsize 400k -an "$vlq540"

score() { "$root/scripts/quality.sh" "$1" "$ref"; }

echo "| Input | Upscaler | PSNR (dB) | SSIM (Y) | VMAF |"
echo "|---|---|---|---|---|"
for in in lq_540p vlq_540p lq_720p; do
  src="$m/$in.mp4"
  for f in bicubic lanczos; do
    o="$out/${in}_${f}.mp4"
    "${ff[@]}" -i "$src" -vf "scale=1920:1080:flags=$f" -c:v hevc_nvenc -b:v 16M -an "$o"
    read -r p s v < <(score "$o"); echo "| $in | ffmpeg $f | $p | $s | $v |"
  done
  for q in BICUBIC LOW HIGH ULTRA HIGHBITRATE_HIGH STREAMING_ULTRA; do
    o="$out/${in}_vsr_${q}.mp4"
    case $in in *720p) sc=0;; *) sc=2;; esac
    if [ $sc = 0 ]; then
      # 720p -> 1080p is 1.5x; the sample only takes integer scales, so do 2x then downscale.
      tmp="$out/${in}_vsr_${q}_1440.mp4"
      (cd "$samples" && "$py" video_super_resolution.py -i "$src" -o "$tmp" --scale 2 --quality "$q" >/dev/null 2>&1) \
        || { echo "| $in | VSR $q | FAILED | | |"; continue; }
      "${ff[@]}" -i "$tmp" -vf scale=1920:1080:flags=lanczos -c:v hevc_nvenc -b:v 16M -an "$o"
      label="VSR $q (2x → lanczos to 1080p)"
    else
      (cd "$samples" && "$py" video_super_resolution.py -i "$src" -o "$o" --scale 2 --quality "$q" >/dev/null 2>&1) \
        || { echo "| $in | VSR $q | FAILED | | |"; continue; }
      label="VSR $q"
    fi
    read -r p s v < <(score "$o"); echo "| $in | $label | $p | $s | $v |"
  done
done

# Crop grids at bright frames: original | lanczos / VSR HIGH | VSR ULTRA
for in in lq_540p vlq_540p; do
  for n in 150 225; do
    for f in "$ref" "$out/${in}_lanczos.mp4" "$out/${in}_vsr_HIGH.mp4" "$out/${in}_vsr_ULTRA.mp4"; do
      "${ff[@]}" -i "$f" -vf "select=eq(n\,$n),crop=640:360:640:300" -frames:v 1 "$out/crops/$(basename "$f" .mp4)_f$n.png"
    done
    "${ff[@]}" -i "$out/crops/src_1080p_f$n.png" -i "$out/crops/${in}_lanczos_f$n.png" \
      -i "$out/crops/${in}_vsr_HIGH_f$n.png" -i "$out/crops/${in}_vsr_ULTRA_f$n.png" \
      -filter_complex "[0][1][2][3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0" "$out/crops/grid_${in}_f$n.png"
  done
done
echo
echo "grids: $out/crops/grid_*.png (top: original | lanczos, bottom: VSR HIGH | VSR ULTRA)"
