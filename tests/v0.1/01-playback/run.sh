#!/usr/bin/env bash
# 0.1 step 1: plain playback (no AI). Does the window play files at the right speed, paced to the display,
# without missed refreshes or skipped frames? Each clip plays 8 s in a window (80% of the screen), twice:
# missed refreshes come in bursts when something else on the desktop stalls the compositor, so two runs
# show whether a miss count is the pipeline or a one-off.
set -euo pipefail
source "$(dirname "$0")/../common.sh"

echo "| Clip | Decoder | Window | Output fps | Missed refreshes (run 1 / 2) | Skipped frames (run 1 / 2) | Decode+convert ms | Upload ms | Output ms |"
echo "|---|---|---|---|---|---|---|---|---|"
for c in synth_1080p.mp4 drift_360p60.mp4 src_1080p.mp4 src_4k_hevc.mkv src_4k_av1.mkv; do
  play 8 "$media/$c" --upscale off --frame-gen off >/dev/null
  missed1=$(field '([0-9]+)(?= missed refreshes)'); skipped1=$(field '([0-9]+)(?= skipped)')
  play 8 "$media/$c" --upscale off --frame-gen off >/dev/null
  dec=$(field 'decoder \K[^)]+\)?' | head -1)
  win=$(field 'Output: \K[0-9x]+')
  fps=$(field 'Display:\s+\K[0-9.]+')
  missed=$(field '([0-9]+)(?= missed refreshes)')
  skipped=$(field '([0-9]+)(?= skipped)')
  dms=$(field 'decode\+convert \K[0-9.]+'); ums=$(field 'upload \K[0-9.]+'); oms=$(field 'output \K[0-9.]+')
  echo "| $c | $dec | $win | $fps | $missed1 / $missed | $skipped1 / $skipped | $dms | $ums | $oms |"
done
