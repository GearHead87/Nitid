#!/usr/bin/env bash
# Build the test clips used by the spikes into results/media/ (git-ignored).
# Needs scripts/fetch-sdks.sh first (for NVIDIA's sample clip).
#
# Sources
#   sintel_trailer-1080p.mp4   Blender Foundation "Sintel" trailer, 1080p24, CC-BY 3.0 (real 1080p source)
#   Drift_RUN_Master_Custom.mp4  NVIDIA VFX sample clip, 640x360 60 fps (native low-res input)
#
# Outputs
#   src_1080p.mp4      Sintel, 10 s of action, 1080p H.264 near-lossless (reference)
#   lq_1080p.mp4       1080p, heavy compression (stream-like)
#   lq_720p.mp4        720p,  heavy compression
#   lq_540p.mp4        540p,  heavy compression
#   drift_360p60.mp4   NVIDIA clip, 10 s, native 360p 60 fps
#   synth_1080p.mp4    synthetic: moving text, then scrolling bars (10 s, 60 fps)
#   src_4k_{h264,hevc,av1}.mkv   4K60 encodes for decode tests (NVENC)
#   frames/            PNG frame pairs / sequences for the C++ spikes
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
m="$root/results/media"
mkdir -p "$m/frames"
ff=(ffmpeg -hide_banner -loglevel error -y)

sintel="$m/sintel_trailer-1080p.mp4"
drift="$root/third_party/nvidia-vfx-python-samples/assets/Drift_RUN_Master_Custom.mp4"
[ -f "$drift" ] || { echo "missing $drift (run scripts/fetch-sdks.sh)"; exit 1; }
[ -f "$sintel" ] || curl -fL --retry 3 -o "$sintel" \
  https://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4

echo ">> reference clip (Sintel 1080p, 10 s from 0:20)"
"${ff[@]}" -ss 20 -i "$sintel" -t 10 -vf "format=yuv420p" \
  -c:v libx264 -crf 12 -preset slow -an "$m/src_1080p.mp4"

echo ">> low-quality variants"
for r in 1080 720 540; do
  w=$(( r * 16 / 9 ))
  "${ff[@]}" -i "$m/src_1080p.mp4" -vf "scale=$w:$r:flags=bicubic" \
    -c:v libx264 -b:v $(( r * 3 ))k -maxrate $(( r * 3 ))k -bufsize $(( r * 3 ))k -preset veryfast -an \
    "$m/lq_${r}p.mp4"
done

echo ">> NVIDIA sample clip, native 360p60"
"${ff[@]}" -i "$drift" -t 10 -c:v libx264 -crf 16 -preset slow -an "$m/drift_360p60.mp4"

echo ">> synthetic clip"
"${ff[@]}" -f lavfi -i "testsrc2=s=1920x1080:r=60:d=5" \
  -f lavfi -i "smptehdbars=s=1920x1080:r=60:d=5" \
  -filter_complex "[0:v]drawtext=text='Nitid moving text %{n}':fontsize=72:fontcolor=white:x=mod(n*12\,w):y=h/2[a];\
[1:v]scroll=horizontal=0.004[b];[a][b]concat=n=2:v=1[out]" \
  -map "[out]" -c:v libx264 -crf 16 -pix_fmt yuv420p "$m/synth_1080p.mp4"

echo ">> 4K60 decode-test encodes"
for c in h264:40M hevc:30M av1:25M; do
  "${ff[@]}" -stream_loop -1 -i "$m/src_1080p.mp4" -t 10 -vf "scale=3840:2160:flags=lanczos,fps=60" \
    -c:v "${c%%:*}_nvenc" -preset p5 -b:v "${c##*:}" "$m/src_4k_${c%%:*}.mkv"
done

echo ">> frame pairs / sequences"
rm -f "$m"/frames/*.png
"${ff[@]}" -ss 3 -i "$m/src_1080p.mp4" -frames:v 2 "$m/frames/pair_1080p_%d.png"
"${ff[@]}" -ss 3 -i "$m/lq_540p.mp4" -frames:v 30 "$m/frames/seq_540p_%02d.png"
"${ff[@]}" -ss 3 -i "$m/drift_360p60.mp4" -frames:v 2 "$m/frames/pair_drift360_%d.png"

for f in "$m"/*.mp4 "$m"/*.mkv; do
  printf '  %-26s %s\n' "$(basename "$f")" \
    "$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate -of csv=p=0 "$f")"
done
echo "  frames: $(ls "$m/frames" | wc -l) PNGs"
