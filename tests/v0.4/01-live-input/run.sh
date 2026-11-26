#!/usr/bin/env bash
# 0.4 step 1: live input from PipeWire (the path screen capture uses), with a synthetic source: a GStreamer test
# pattern published as a PipeWire node. Nothing from the desktop is captured.
#  A. Right picture: SMPTE colour bars (BGRx and RGBx) vs the same pattern rendered by GStreamer, bars region.
#  B. Live modes: moving ball 1280x720 at 30 and 60 fps, low latency vs smooth (frame generation), 8 s, upscaled to
#     the default window with VSR HIGH: frames in / out, generated, capture -> screen latency, missed refreshes.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.4/01"; mkdir -p "$R"
echo "### A. Right picture (window image of the captured colour bars vs GStreamer's own frame)"
echo
echo "| Format | Path | PSNR, bars region |"
echo "|---|---|---|"
for fmt in BGRx RGBx; do
  gst_source "nitid-bars-$fmt" 20 smpte 1280x720 60 $fmt
  (cd "$R" && rm -f "bars_${fmt}_2.00s.ppm" && timeout 30 "$nitid" --pipewire "nitid-bars-$fmt" --width 1280 --height 720 \
     --capture-at 2.0 --capture-prefix "bars_$fmt" >"bars_$fmt.log" 2>&1) || true
  timeout 10 gst-launch-1.0 -q videotestsrc pattern=smpte num-buffers=1 ! video/x-raw,format=RGB,width=1280,height=720 \
    ! pngenc ! filesink location="$R/ref.png"
  p=$(ffmpeg -hide_banner -nostdin -i "$R/bars_${fmt}_2.00s.ppm" -i "$R/ref.png" -lavfi \
      "[0]format=rgb24,crop=1280:480:0:0[a];[1]format=rgb24,crop=1280:480:0:0[b];[a][b]psnr" -f null - 2>&1 | grep -oE 'average:[0-9.inf]+' | cut -d: -f2)
  echo "| $fmt | $(grep -oP 'capture: \S+ \S+, \K.*' "$R/bars_$fmt.log" | head -1) | $p |"
done
echo
echo "### B. Live modes (8 s; source 1280x720 moving ball; default window, VSR HIGH; two runs each)"
echo
echo "| Source | Mode | Frames in | Shown (generated) | Capture -> screen | Display fps | Missed | Dropped |"
echo "|---|---|---|---|---|---|---|---|"
for fps in 30 60; do
  gst_source "nitid-ball-$fps" 80 ball 1280x720 $fps
  for mode in low-latency smooth low-latency smooth; do
    out=$(timeout 30 "$nitid" --pipewire "nitid-ball-$fps" --live-mode $mode --quit-after 8 2>&1 || true)
    echo "| ${fps} fps | $mode | $(grep -oP 'Live:.*\| \K[0-9.]+(?= fps arriving)' <<<"$out") | $(grep -oP 'Played:.*?, \K[0-9]+ output frames \([0-9]+ generated\)' <<<"$out") | $(grep -oP 'capture -> screen \K[0-9.]+ ms' <<<"$out") | $(grep -oP 'Display:\s+\K[0-9.]+(?= fps)' <<<"$out") | $(grep -oP '[0-9]+(?= missed refreshes)' <<<"$out") | $(grep -oP '[0-9]+(?= dropped)' <<<"$out") |"
  done
done
