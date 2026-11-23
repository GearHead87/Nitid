#!/usr/bin/env bash
# M2: Does GPU decoding (Vulkan Video and CUDA/NVDEC) reach >= 120 fps at 4K?
# Decodes the 10 s 4K60 test encodes (600 frames, made by scripts/fetch-media.sh)
# with each method, keeping frames on the GPU (no download), and reports fps.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
m="$root/results/media"
for c in h264 hevc av1; do
  [ -f "$m/src_4k_$c.mkv" ] || { echo "missing $m/src_4k_$c.mkv (run scripts/fetch-media.sh)"; exit 1; }
done

run() { # $1 = label, rest = ffmpeg input options; prints "fps frames errors"
  local f=$1; shift
  local start end log frames
  start=$(date +%s.%N)
  log=$(ffmpeg -hide_banner -nostdin -y "$@" -i "$f" -f null - 2>&1 || true)
  end=$(date +%s.%N)
  frames=$(tr '\r' '\n' <<<"$log" | grep -oE 'frame= *[0-9]+' | tail -1 | grep -oE '[0-9]+')
  local errs
  errs=$(grep -ciE 'error|failed|not supported' <<<"$log" || true)
  awk -v s="$start" -v e="$end" -v n="${frames:-0}" -v x="$errs" \
    'BEGIN{printf "%.0f %d %d\n", n/(e-s), n, x}'
}

printf '| Codec | Method | Frames | Decode fps | Errors in log |\n|---|---|---|---|---|\n'
for c in h264 hevc av1; do
  f="$m/src_4k_$c.mkv"
  read -r fps n e < <(run "$f" -hwaccel vulkan -hwaccel_output_format vulkan)
  printf '| %s | Vulkan Video | %s | %s | %s |\n' "$c" "$n" "$fps" "$e"
  read -r fps n e < <(run "$f" -hwaccel cuda -hwaccel_output_format cuda)
  printf '| %s | CUDA (NVDEC) | %s | %s | %s |\n' "$c" "$n" "$fps" "$e"
  read -r fps n e < <(run "$f" -threads 0)
  printf '| %s | CPU (reference) | %s | %s | %s |\n' "$c" "$n" "$fps" "$e"
done

echo
echo "GPU used by Vulkan decoding (this PC also has an AMD iGPU):"
ffmpeg -hide_banner -nostdin -v verbose -hwaccel vulkan -hwaccel_output_format vulkan \
  -i "$m/src_4k_hevc.mkv" -frames:v 10 -f null - 2>&1 | grep -E 'Device [0-9]+ selected'
