#!/usr/bin/env bash
# V3: Does VFX Video Frame Generation work, is it real-time, and how good are the generated frames?
#  1. NVIDIA's sample end to end (2x and 4x) on the 1080p24 Sintel clip and the 360p60 drift clip.
#  2. vfg_eval.py: GPU-only speed at 1080p/1440p/2160p, and quality by rebuilding dropped real frames.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
py="$root/third_party/vfx-venv/bin/python"
samples="$root/third_party/nvidia-vfx-python-samples"
m="$root/results/media"
out="$root/results/v3-vfx-frame-gen"
mkdir -p "$out"

echo "## 1. NVIDIA sample, end to end"
echo
echo "| Input | Multiplier | Mode | Output (codec, size, fps) | Output frames | Sample-reported fps |"
echo "|---|---|---|---|---|---|"
for clip in src_1080p drift_360p60; do
  for mult in 2 4; do
    o="$out/${clip}_x${mult}.mp4"
    log=$(cd "$samples" && "$py" video_frame_generation.py --input "$m/$clip.mp4" --output "$o" \
          --multiplier "$mult" --mode MEDIUM 2>&1) || { echo "| $clip | ${mult}x | MEDIUM | FAILED: $(tail -1 <<<"$log") | | |"; continue; }
    info=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate -of csv=p=0 "$o")
    frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$o")
    fps=$(grep -iE 'fps' <<<"$log" | tail -1 | grep -oE '[0-9]+\.[0-9]+' | tail -1)
    echo "| $clip | ${mult}x | MEDIUM | $info | $frames | ${fps:-?} |"
  done
done
echo
echo "## 2. GPU-only evaluation"
echo
for clip in src_1080p drift_360p60; do
  echo "### Clip: $clip"
  echo
  "$py" "$(dirname "$0")/vfg_eval.py" "$m/$clip.mp4" "$out/$clip" 240
  echo
done
