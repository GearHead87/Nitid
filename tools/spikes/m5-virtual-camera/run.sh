#!/usr/bin/env bash
# M5: Does a v4l2loopback virtual camera work?
# Needs the module loaded first (one-time, as root):
#   sudo modprobe v4l2loopback devices=1 video_nr=42 card_label="Nitid Virtual Camera" exclusive_caps=1
# Then this script writes a 1080p60 test pattern into /dev/video42 and reads it back
# with a second ffmpeg (acting like OBS / Discord), and saves one received frame.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
out="$root/results/m5-virtual-camera"; mkdir -p "$out"
dev=${1:-/dev/video42}

if ! lsmod | grep -q '^v4l2loopback'; then
  echo "v4l2loopback is not loaded. Run:"
  echo '  sudo modprobe v4l2loopback devices=1 video_nr=42 card_label="Nitid Virtual Camera" exclusive_caps=1'
  exit 2
fi
[ -e "$dev" ] || { echo "$dev not found"; ls /dev/video*; exit 2; }

v4l2-ctl -d "$dev" --info | sed -n '1,8p'

# Writer: 8 s of 1080p60 test pattern
ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc2=s=1920x1080:r=60:d=8" \
  -pix_fmt yuyv422 -f v4l2 "$dev" &
w=$!
sleep 2

# Reader: grab 3 s like a consumer app would
ffmpeg -hide_banner -loglevel error -y -f v4l2 -t 3 -i "$dev" -c:v libx264 -preset ultrafast "$out/received.mkv"
ffmpeg -hide_banner -loglevel error -y -i "$out/received.mkv" -frames:v 1 "$out/received_frame.png"
wait $w || true

frames=$(ffprobe -v error -count_frames -select_streams v:0 \
  -show_entries stream=nb_read_frames,width,height -of csv=p=0 "$out/received.mkv")
echo "received (width,height,frames in 3 s): $frames"
echo "frame saved: $out/received_frame.png"
