#!/usr/bin/env bash
# Objective quality of a test video against a reference (same frame count).
# The test video is scaled to the reference size with bicubic if sizes differ.
# Frames are paired by number, not timestamp (Matroska stores whole milliseconds, so at 24 fps timestamp
# matching paired some frames with their neighbour; found in tests/v0.1/04-cli-bench).
# Prints: "psnr ssim vmaf" (VMAF 0–100, higher is better).
# Usage: scripts/quality.sh <test.mp4> <reference.mp4>
set -euo pipefail
t=$1 ref=$2
read -r W H < <(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$ref" | tr ',' ' ')
log=$(ffmpeg -hide_banner -nostdin -i "$t" -i "$ref" -filter_complex \
  "[0:v]scale=${W}:${H}:flags=bicubic,format=yuv420p,settb=AVTB,setpts=N*40000,split=3[a1][a2][a3];\
[1:v]format=yuv420p,settb=AVTB,setpts=N*40000,split=3[b1][b2][b3];\
[a1][b1]psnr;[a2][b2]ssim;[a3][b3]libvmaf=n_threads=16" -f null - 2>&1)
psnr=$(grep -oE 'PSNR y:[0-9.]+ u:[0-9.]+ v:[0-9.]+ average:[0-9.inf]+' <<<"$log" | grep -oE 'average:[0-9.inf]+' | cut -d: -f2)
ssim=$(grep -oE 'SSIM Y:[0-9.]+' <<<"$log" | head -1 | cut -d: -f2)
vmaf=$(grep -oE 'VMAF score: [0-9.]+' <<<"$log" | grep -oE '[0-9.]+$')
echo "${psnr:-?} ${ssim:-?} ${vmaf:-?}"
