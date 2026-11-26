#!/usr/bin/env bash
# 0.2 step 3: sound, in sync with the picture.
# Nitid keeps the picture locked to the display and makes the sound follow it (slight resampling, resync on
# jumps). This test reads the engine's own measurement: the gap between what PipeWire will be playing and what
# will be on screen at the same moment, using PipeWire's reported delay to the speakers and the measured
# present-to-screen time. (Whether that matches what you hear is the manual check in NOTES.md.)
# Test clip results/media/avsync_1080p24.mp4: white flash + 1 kHz beep at every whole second (made below).
# Played at volume 0.2.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
clip="$media/avsync_1080p24.mp4"
if [ ! -f "$clip" ]; then
  ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=black:s=1920x1080:r=24:d=60,drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='lt(mod(t\,1)\,0.04)',drawtext=text='%{pts\:hms}':fontsize=96:fontcolor=gray:x=(w-tw)/2:y=h-200" \
    -f lavfi -i "aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)|if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:d=60" \
    -c:v libx264 -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest "$clip"
fi
echo "Audio output: $(wpctl inspect @DEFAULT_AUDIO_SINK@ 2>/dev/null | grep -oP 'node.description = "\K[^"]+' || echo unknown)"
echo
echo "| Case | Sound | A/V now (ms) | A/V max (ms) | Speed | Resyncs | Underruns | Audio latency | Display latency | fps | Missed |"
echo "|---|---|---|---|---|---|---|---|---|---|---|"
run() {  # label secs file [options...]
  local label=$1 secs=$2 file=$3; shift 3
  play "$secs" "$file" --volume 0.2 "$@" >/dev/null
  local snd; snd=$(grep -oP '^Sound: \K.*' "$log" | head -1)
  if grep -q '^Sound:    A/V' "$log"; then
    echo "| $label | ${snd%% ->*} | $(field 'Sound:\s+A/V \K[-+0-9.]+') | $(field 'Sound:.*\(max \K[0-9.]+') | $(field 'Sound:.*speed \K[0-9.]+') | $(field 'Sound:.*resyncs \K[0-9]+') | $(field 'Sound:.*underruns \K[0-9]+') | $(field 'latency audio \K[0-9]+') ms | $(field 'display \K[0-9]+') ms | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') |"
  else
    echo "| $label | $snd | – | – | – | – | – | – | – | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') |"
  fi
}
run "Sync clip, 20 s, VSR + FG per refresh" 20 "$clip"
run "Sync clip, 20 s, FG off" 20 "$clip" --frame-gen off --upscale off
run "Sync clip, 4 seeks + 2 pauses in 20 s" 20 "$clip" --test-seek 3:40 7:10 12:55 16:20 --test-pause 9:1.5 14:3
run "Sync clip, loops (start at 55 s, 15 s)" 15 "$clip" --start 55 --loop
run "Sintel trailer (AAC), 25 s, VSR + FG" 25 "$media/sintel_trailer-1080p.mp4"
run "Sintel trailer, audio delay +40 ms" 10 "$media/sintel_trailer-1080p.mp4" --audio-delay 40
run "File without sound" 5 "$media/src_1080p.mp4"
run "--no-audio" 5 "$clip" --no-audio
