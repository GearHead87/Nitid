#!/usr/bin/env bash
# Build the test clips used by the spikes into results/media/ (git-ignored).
# Needs scripts/fetch-sdks.sh first (uses NVIDIA's sample clip as the real-world source).
#
#   src_1080p.mp4      NVIDIA sample clip, normalised to 1080p H.264, 10 s
#   lq_1080p.mp4       1080p, heavy compression (stream-like)
#   lq_720p.mp4        720p,  heavy compression
#   lq_540p.mp4        540p,  heavy compression
#   synth_1080p.mp4    synthetic: moving text, scrolling pattern, scene cut (10 s, 60 fps)
#   src_4k_{h264,hevc,av1}.mkv   4K60 encodes for decode tests (NVENC)
#   frames/            PNG frame pairs for the C++ spikes
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
m="$root/results/media"
mkdir -p "$m/frames"
sample="$root/third_party/nvidia-vfx-python-samples/assets/Drift_RUN_Master_Custom.mp4"
ff=(ffmpeg -hide_banner -loglevel error -y)

[ -f "$sample" ] || { echo "missing $sample (run scripts/fetch-sdks.sh)"; exit 1; }

echo ">> source clip"
"${ff[@]}" -i "$sample" -t 10 -vf "scale=1920:1080:flags=lanczos,format=yuv420p" \
  -c:v libx264 -crf 14 -preset slow -an "$m/src_1080p.mp4"

echo ">> low-quality variants"
for r in 1080 720 540; do
  w=$(( r * 16 / 9 ))
  "${ff[@]}" -i "$m/src_1080p.mp4" -vf "scale=$w:$r:flags=bicubic" \
    -c:v libx264 -b:v $(( r * 3 ))k -maxrate $(( r * 3 ))k -bufsize $(( r * 3 ))k -preset veryfast -an \
    "$m/lq_${r}p.mp4"
done

echo ">> synthetic clip"
"${ff[@]}" -f lavfi -i "testsrc2=s=1920x1080:r=60:d=5" \
  -f lavfi -i "smptehdbars=s=1920x1080:r=60:d=5" \
  -filter_complex "[0:v]drawtext=text='Nitid moving text %{n}':fontsize=72:fontcolor=white:x=mod(n*12\,w):y=h/2[a];\
[1:v]scroll=horizontal=0.004[b];[a][b]concat=n=2:v=1[out]" \
  -map "[out]" -c:v libx264 -crf 16 -pix_fmt yuv420p "$m/synth_1080p.mp4"

echo ">> 4K60 decode-test encodes"
"${ff[@]}" -stream_loop -1 -i "$m/src_1080p.mp4" -t 10 -vf "scale=3840:2160:flags=lanczos,fps=60" \
  -c:v h264_nvenc -preset p5 -b:v 40M "$m/src_4k_h264.mkv"
"${ff[@]}" -stream_loop -1 -i "$m/src_1080p.mp4" -t 10 -vf "scale=3840:2160:flags=lanczos,fps=60" \
  -c:v hevc_nvenc -preset p5 -b:v 30M "$m/src_4k_hevc.mkv"
"${ff[@]}" -stream_loop -1 -i "$m/src_1080p.mp4" -t 10 -vf "scale=3840:2160:flags=lanczos,fps=60" \
  -c:v av1_nvenc -preset p5 -b:v 25M "$m/src_4k_av1.mkv"

echo ">> frame pairs"
"${ff[@]}" -ss 3 -i "$m/src_1080p.mp4" -frames:v 2 "$m/frames/pair_1080p_%d.png"
"${ff[@]}" -ss 3 -i "$m/lq_540p.mp4" -frames:v 30 "$m/frames/seq_540p_%02d.png"

ls -la "$m" "$m/frames" | sed 's/^/  /'
