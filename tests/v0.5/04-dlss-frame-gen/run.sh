#!/usr/bin/env bash
# 0.5 step 4: DLSS Frame Generation (full and lite, 2x-6x) next to VFX frame generation.
#  A. Quality: rebuild dropped frames (--source-step S, multiplier S) and score only the generated frames against
#     the real ones (mean per-frame luma PSNR, as spikes S3 / F1), plus VMAF of the whole rebuilt clip.
#  B. Speed: per generated frame at 1080p and 4K, 2x / 4x / 6x.
#  C. Window: 24 fps video, frame generation per display refresh (DLSS picks the whole step closest to it).
#  D. Live (synthetic PipeWire source, nothing from the desktop): smooth mode with DLSS frame generation.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
source "$root/tests/v0.4/common.sh"
R="$root/results/v0.5/04"; mkdir -p "$R"
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }
gen_psnr() {  # <out.mkv> <ref> <step>: mean luma PSNR of the generated frames
  ffmpeg -hide_banner -loglevel fatal -nostdin -i "$1" -i "$2" -lavfi \
    "[0]format=yuv420p,settb=AVTB,setpts=N*40000[a];[1]format=yuv420p,settb=AVTB,setpts=N*40000[b];[a][b]psnr=stats_file=$R/q.txt" -f null -
  awk -v s="$3" '{split($1,a,":"); f=a[2]-1; for(i=1;i<=NF;i++) if($i ~ /^psnr_y:/){split($i,b,":"); v=b[2]}
    if(v=="inf") v=99; if(f%s!=0){t+=v;c++}} END{printf "%.2f", t/c}' "$R/q.txt"
  rm -f "$R/q.txt"
}
echo "### A. Quality: rebuild dropped frames (generated frames vs the real ones; VMAF of the whole rebuilt clip)"
echo
echo "| Clip | Multiplier | VFX MEDIUM | VFX HIGH | DLSS full | DLSS lite |"
echo "|---|---|---|---|---|---|"
for clip in drift_360p60 src_1080p sintel_trailer-1080p; do
  ref="$media/$clip.mp4"
  for st in 2 4 6; do
    row="| $clip | ${st}x |"
    for fg in medium high dlss dlss-lite; do
      out="$R/a.mkv"
      bench "$ref" --upscale off --frame-gen $fg --source-step $st --multiplier $st --lossless -o "$out" >/dev/null
      p=$(gen_psnr "$out" "$ref" $st)
      v=$(read -r _ _ v <<<"$("$root/scripts/quality.sh" "$out" "$ref")"; echo "$v")
      row="$row $p / $(printf '%.1f' "$v") |"
      rm -f "$out"
    done
    echo "$row"
  done
done
echo
echo "### B. Speed (bench, ms per generated frame; DLSS lite also needs optical flow per real frame)"
echo
echo "| Frames | Multiplier | VFX MEDIUM | VFX HIGH | DLSS full | DLSS lite | Optical flow (lite) |"
echo "|---|---|---|---|---|---|---|"
for job in "src_1080p.mp4 1920x1080 off" "src_1080p.mp4 3840x2160 dlss"; do
  read -r f size up <<<"$job"
  for m in 2 4 6; do
    row="| $size | ${m}x |"
    for fg in medium high dlss dlss-lite; do
      b=$(bench "$media/$f" --upscale $up --size $size --frame-gen $fg --multiplier $m --frames 480)
      row="$row $(grep -oP 'Frame gen \(per generated frame\) \| \K[0-9.]+' <<<"$b") |"
      [ $fg = dlss-lite ] && fl=$(grep -oP 'optical flow \(per source frame\) \| \K[0-9.]+' <<<"$b")
    done
    echo "$row $fl |"
  done
done
echo
echo "### C. Window: 24 fps 540p in a 1920x1080 window, frame generation per display refresh (5 s, twice)"
echo
echo "| Frame gen | Report | Display fps | Output frames (generated) | Missed | Late |"
echo "|---|---|---|---|---|---|"
for fg in medium dlss dlss-lite medium dlss dlss-lite; do
  play 5 "$media/lq_540p.mp4" --frame-gen $fg --width 1920 --height 1080 >/dev/null
  echo "| $fg | $(grep -oP 'FrameGen:\s+\K.*?(?=\s{3}scene)' "$log" | tail -1) | $(field 'Display:\s+\K[0-9.]+') | $(grep -oP 'Played:.*?, \K[0-9]+ output frames \([0-9]+ generated\)' "$log" | tail -1) | $(field '([0-9]+) missed refreshes') | $(field '([0-9]+) late frames') |"
done
echo
echo "### D. Live: synthetic 1280x720 moving ball at 30 fps, smooth mode (8 s)"
echo
echo "| Frame gen | Frames in | Shown (generated) | Capture -> screen | Display fps | Missed |"
echo "|---|---|---|---|---|---|"
gst_source "nitid-ball-30" 60 ball 1280x720 30
for fg in medium dlss dlss-lite; do
  out=$(timeout 30 "$nitid" --pipewire nitid-ball-30 --live-mode smooth --frame-gen $fg --quit-after 8 2>&1 || true)
  echo "| $fg | $(grep -oP 'Live:.*\| \K[0-9.]+(?= fps arriving)' <<<"$out") | $(grep -oP 'Played:.*?, \K[0-9]+ output frames \([0-9]+ generated\)' <<<"$out") | $(grep -oP 'capture -> screen \K[0-9.]+ ms' <<<"$out") | $(grep -oP 'Display:\s+\K[0-9.]+(?= fps)' <<<"$out") | $(grep -oP '[0-9]+(?= missed refreshes)' <<<"$out") |"
done
