#!/usr/bin/env bash
# M1: Is NVENC (H.264 / HEVC / AV1) faster than real time at 4K60?
# Encodes 10 s (600 frames) of a 4K60 synthetic source with each NVENC codec and
# reports encode speed. Real time = 60 fps.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
out="$root/results/m1-nvenc"
mkdir -p "$out"

src=(-f lavfi -i "testsrc2=s=3840x2160:r=60:d=10")

printf '| Codec | Preset | Frames | Wall time (s) | Encode fps | x real time | Output size | Decodes OK |\n'
printf '|---|---|---|---|---|---|---|---|\n'
for codec in h264_nvenc hevc_nvenc av1_nvenc; do
  for preset in p1 p5; do
    f="$out/${codec}_${preset}.mkv"
    # testsrc2 generation on the CPU can be the bottleneck; time encode with source
    # pre-rendered to raw frames would be fairer, so we measure both below.
    start=$(date +%s.%N)
    ffmpeg -hide_banner -loglevel error -y "${src[@]}" -c:v "$codec" -preset "$preset" -b:v 40M "$f"
    end=$(date +%s.%N)
    t=$(awk "BEGIN{print $end - $start}")
    frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$f")
    fps=$(awk "BEGIN{print $frames / $t}")
    ok=$(ffmpeg -v error -i "$f" -f null - 2>&1 | head -1)
    printf '| %s | %s | %s | %.2f | %.0f | %.1fx | %s | %s |\n' "$codec" "$preset" "$frames" "$t" "$fps" \
      "$(awk "BEGIN{print $fps / 60}")" "$(du -h "$f" | cut -f1)" "$([ -z "$ok" ] && echo yes || echo "NO: $ok")"
  done
done

# Encoder-only throughput: 120 raw 4K frames (~1.5 GB) in RAM (/tmp), looped 5x = 600
# frames, so the CPU-side test pattern generator is not the bottleneck.
raw=$(mktemp /tmp/m1-src-XXXX.nut)
trap 'rm -f "$raw"' EXIT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=s=3840x2160:r=60:d=2" \
  -c:v rawvideo -pix_fmt yuv420p -f nut "$raw"
echo
printf '| Codec (raw input, p5) | Encode fps (ffmpeg reported) |\n|---|---|\n'
for codec in h264_nvenc hevc_nvenc av1_nvenc; do
  speed=$(ffmpeg -hide_banner -y -stream_loop 4 -i "$raw" -c:v "$codec" -preset p5 -b:v 40M -f null - 2>&1 \
          | tr '\r' '\n' | grep -oE 'fps= *[0-9.]+' | tail -1 | tr -d ' ')
  printf '| %s | %s |\n' "$codec" "${speed#fps=}"
done
