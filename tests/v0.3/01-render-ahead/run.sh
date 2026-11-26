#!/usr/bin/env bash
# 0.3 step 1: the pipeline on its own thread, ahead of the display ("render-ahead"), with frame-gen pacing.
#  A. Pacing: 12 s runs (Sintel trailer 1080p in the default window, 4K60 AV1), frame gen LOW / MEDIUM / HIGH per
#     refresh, 2x HIGH, frame gen off. Stride = one new frame every N refreshes; late = a due frame was not ready.
#  B. Before / after: the same HIGH runs with the 0.2.0 release (pipeline on the render thread), from out/.
#  C. Settings changed while playing (keys H, B, V, G = model reloads): missed refreshes.
#  D. Right frame: window image after --start 5 (upscaling and frame gen off, window = video size) vs frame 120
#     decoded by FFmpeg, and vs its neighbours.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.3/01"; mkdir -p "$R"
row() {  # label secs file [options]
  local label=$1 secs=$2 file=$3; shift 3
  play "$secs" "$file" --volume 0.2 "$@" >/dev/null
  echo "| $label | $(grep -o 'every [0-9] refreshes' "$log" | tail -1 | grep -o '[0-9]' || echo 1) | $(field 'FrameGen:\s+FG \S+ .*?\K[0-9.]+(?= ms)') | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') | $(field '([0-9]+) late frames') |"
}
echo "### A. Pacing with render-ahead (12 s each)"
echo
echo "| Run | Stride | FG ms | Display fps | Missed refreshes | Late frames |"
echo "|---|---|---|---|---|---|"
s="$media/sintel_trailer-1080p.mp4"; k="$media/src_4k_av1.mkv"
row "1080p trailer, FG LOW per refresh" 12 "$s" --frame-gen low
row "1080p trailer, FG MEDIUM per refresh" 12 "$s" --frame-gen medium
row "1080p trailer, FG HIGH per refresh" 12 "$s" --frame-gen high
row "1080p trailer, FG HIGH 2x" 12 "$s" --frame-gen high --multiplier 2
row "1080p trailer, FG off" 12 "$s" --frame-gen off
row "4K60 AV1, FG MEDIUM per refresh" 12 "$k" --frame-gen medium --loop
row "4K60 AV1, FG HIGH per refresh" 12 "$k" --frame-gen high --loop

echo
echo "### B. Before: 0.2.0 (pipeline on the render thread), same HIGH runs"
echo
old=$(ls "$root"/out/nitid-0.2.0-linux-x86_64.tar.gz 2>/dev/null || true)
if [ -n "$old" ]; then
  tmp=$(mktemp -d); tar -xzf "$old" -C "$tmp"
  vfx=$(cd "$root/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs" && pwd)
  echo "| Run | FG ms | Display fps | Missed refreshes |"
  echo "|---|---|---|---|"
  for job in "1080p trailer|$s|" "4K60 AV1|$k|--loop"; do
    IFS='|' read -r label f extra <<<"$job"
    # shellcheck disable=SC2086
    out=$(NITID_VFX_DIR="$vfx" timeout 60 "$tmp"/nitid-0.2.0-linux-x86_64/bin/nitid "$f" --frame-gen high --quit-after 12 --volume 0.2 $extra 2>&1 || true)
    echo "| $label, FG HIGH per refresh (0.2.0) | $(grep -oP 'FrameGen:\s+FG \S+ .*?\K[0-9.]+(?= ms)' <<<"$out") | $(grep -oP 'Display:\s+\K[0-9.]+(?= fps)' <<<"$out") | $(grep -oP '([0-9]+)(?= missed refreshes)' <<<"$out" | tail -1) |"
  done
  rm -rf "$tmp"
else
  echo "(out/nitid-0.2.0-linux-x86_64.tar.gz not found: build it with packaging/linux/docker-build.sh at v0.2.0)"
fi

echo
echo "### C. Settings changed while playing (H at 2 s, B at 4 s, V at 6 s, G at 8 s; 10 s)"
echo
play 10 "$s" --volume 0.2 --test-key 2:H 4:B 6:V 8:G >/dev/null
echo "Display: $(field 'Display:\s+\K.*')"

echo
echo "### D. Right frame after --start 5 (window image vs FFmpeg's frame 120 and its neighbours)"
echo
(cd "$R" && timeout 60 "$nitid" "$media/src_1080p.mp4" --start 5 --upscale off --frame-gen off --width 1920 --height 1080 \
  --capture-at 5.0 --capture-prefix start5 --volume 0.2 >/dev/null 2>&1) || true
for n in 119 120 121; do frame "$media/src_1080p.mp4" $n "$R/ref_$n.png"; done
echo "| Reference frame | PSNR vs window image |"
echo "|---|---|"
for n in 119 120 121; do echo "| $n | $(psnr "$R/start5_5.00s.ppm" "$R/ref_$n.png") |"; done
