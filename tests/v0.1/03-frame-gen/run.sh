#!/usr/bin/env bash
# 0.1 step 3: AI frame generation (VFX VFG) in the player.
#  A. Accuracy: play the 24 fps clip using only every 2nd (or 4th) frame, let frame generation rebuild the
#     dropped frames at 2x (4x), capture the window at the moments of dropped frames and compare with the
#     real ones. Baselines: 50/50 blend of the neighbours, and repeating the previous frame.
#  B. Speed and smoothness per mode: generated frames, frame gen time, output fps, missed refreshes.
# Window 2048x1152 (1080p is upscaled by VSR HIGH first, then frames are generated at 2048x1152).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.1/03"; mkdir -p "$R"
win=(--width 2048 --height 1152 --upscale high)
secs=(2 3 4 5 6)

mean() { awk '$1 ~ /^[0-9.]+$/ {s+=$1; n++} $1 == "missing" {m++} END {printf "%.2f", n ? s/n : 0; if (m) printf " (%d missing)", m}'; }

echo "### A. Accuracy: rebuilt frames vs the real dropped frames (mean PSNR dB over ${#secs[@]} moments per row)"
echo
echo "| Test | Frame position | VFX frame gen (MEDIUM) | 50/50 blend | Repeat previous |"
echo "|---|---|---|---|---|"
for cfg in "2 2" "4 4"; do
  read -r step mult <<<"$cfg"
  times=()
  for s in "${secs[@]}"; do for k in $(seq 1 $((step - 1))); do times+=("$(awk -v s=$s -v k=$k 'BEGIN{printf "%.4f", s + k/24}')"); done; done
  play 30 "$media/src_1080p.mp4" "${win[@]}" --frame-gen medium --multiplier "$mult" --source-step "$step" \
    --capture-at "${times[@]}" --capture-prefix "$R/a${mult}x" >/dev/null
  for k in $(seq 1 $((step - 1))); do
    : >"$R/fg.txt"; : >"$R/bl.txt"; : >"$R/rp.txt"
    for s in "${secs[@]}"; do
      o=$((s * 24)); t=$(awk -v s=$s -v k=$k 'BEGIN{printf "%.2f", s + k/24}')
      frame "$media/src_1080p.mp4" $((o + k)) "$R/real.ppm" 2048x1152
      frame "$media/src_1080p.mp4" $o "$R/prev.ppm" 2048x1152
      frame "$media/src_1080p.mp4" $((o + step)) "$R/next.ppm" 2048x1152
      w=$(awk -v k=$k -v n=$step 'BEGIN{printf "%.4f", k/n}')
      ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/prev.ppm" -i "$R/next.ppm" \
        -lavfi "[0][1]blend=all_expr='A*(1-$w)+B*$w'" "$R/blend.ppm"
      psnr "$R/a${mult}x_${t}s.ppm" "$R/real.ppm" >>"$R/fg.txt"
      psnr "$R/blend.ppm" "$R/real.ppm" >>"$R/bl.txt"
      psnr "$R/prev.ppm" "$R/real.ppm" >>"$R/rp.txt"
    done
    echo "| ${mult}x (every ${step}th frame kept) | $k/$step | **$(mean <"$R/fg.txt")** | $(mean <"$R/bl.txt") | $(mean <"$R/rp.txt") |"
  done
done

echo
echo "### B. Speed and smoothness (8 s each)"
echo
echo "| Source | Quality | Multiplier | Output fps | Generated / output frames | Frame gen ms | VSR ms | Missed refreshes | Skipped |"
echo "|---|---|---|---|---|---|---|---|---|"
run_b() {  # src quality multiplier
  play 8 "$media/$1" "${win[@]}" --frame-gen "$2" --multiplier "$3" >/dev/null
  local m=$3; [ "$m" = 0 ] && m="per refresh"
  echo "| $1 | $2 | $m | $(field 'Display:\s+\K[0-9.]+') | $(field 'frames \(\K[0-9]+(?= generated)') / $(field 'Played:.*?, \K[0-9]+(?= output)') | $(field 'frame gen \K[0-9.]+') | $(field 'upscale \K[0-9.]+') | $(field '([0-9]+)(?= missed refreshes)') | $(field '([0-9]+)(?= skipped)') |"
}
for q in low medium high; do run_b src_1080p.mp4 $q 0; done
run_b src_1080p.mp4 medium 2
run_b src_1080p.mp4 medium 4
run_b synth_1080p.mp4 medium 0
run_b synth_1080p.mp4 medium 2
run_b drift_360p60.mp4 medium 0
