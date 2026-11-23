#!/usr/bin/env bash
# V1: Does VFX Video Super Resolution run on this PC, and is it fast enough for real time?
#  1. NVIDIA's own sample, end to end (decode -> VSR -> HEVC encode) on the test clips.
#  2. bench_vsr.py: GPU-only time per frame for several resolutions and modes.
# Needs: scripts/fetch-sdks.sh, scripts/fetch-media.sh, tools/spikes/vfx-env/setup.sh
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
py="$root/third_party/vfx-venv/bin/python"
samples="$root/third_party/nvidia-vfx-python-samples"
m="$root/results/media"
out="$root/results/v1-vfx-vsr"
mkdir -p "$out"

echo "## 1. NVIDIA sample, end to end"
echo
echo "| Input | Mode | Output | Frames | Sample-reported fps (incl. CPU decode + encode) |"
echo "|---|---|---|---|---|"
for clip in drift_360p60 lq_540p lq_720p lq_1080p; do
  for q in HIGH ULTRA STREAMING_ULTRA; do
    o="$out/${clip}_${q}.mp4"
    log=$(cd "$samples" && "$py" video_super_resolution.py -i "$m/$clip.mp4" -o "$o" --scale 2 --quality "$q" 2>&1) || {
      echo "| $clip | $q | FAILED | – | $(tail -1 <<<"$log") |"; continue; }
    frames=$(sed -n 's/.*Frames processed: *//p' <<<"$log")
    fps=$(sed -n 's/.*Processing FPS: *//p' <<<"$log")
    res=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of csv=p=0 "$o")
    echo "| $clip | $q | $res | $frames | $fps |"
  done
done
echo
echo "## 2. GPU-only benchmark"
echo
"$py" "$(dirname "$0")/bench_vsr.py" "$m/src_1080p.mp4" 100
