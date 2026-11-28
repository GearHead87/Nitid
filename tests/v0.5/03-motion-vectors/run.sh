#!/usr/bin/env bash
# 0.5 step 3: motion vectors from hardware optical flow (VK_NV_optical_flow) for DLSS Super Resolution.
#  A. Quality with motion: the same clips and presets as step 2 (which ran DLSS with zero motion), vs the real 1080p.
#  B. Same size (DLAA) with motion: compressed 1080p at 1080p.
#  C. Cost: optical flow per source frame at 540p / 720p / 1080p, and DLSS + flow throughput.
#  D. Window: 5 s, 540p in a 1920x1080 window, DLSS + frame gen MEDIUM per refresh.
# (The motion-vector sign / scale sweep is in NOTES.md; it was run with a temporary debug switch, since removed.)
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.5/03"; mkdir -p "$R"
q() { "$root/scripts/quality.sh" "$1" "$2"; }
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }
echo "### A. Quality with motion vectors, upscaled to 1080p (whole 10 s clip vs the real 1080p video)"
echo
echo "| Source | DLSS preset | Mode | PSNR | SSIM | VMAF | DLSS ms | flow ms |"
echo "|---|---|---|---|---|---|---|---|"
for src in lq_540p lq_720p vlq_540p; do
  for u in dlss-j dlss-k dlss-l dlss-m; do
    out="$R/a_${src}_$u.mkv"
    b=$(bench "$media/$src.mp4" --upscale "$u" --size 1920x1080 --lossless -o "$out")
    read -r p s v <<<"$(q "$out" "$media/src_1080p.mp4")"
    echo "| $src | ${u#dlss-} | $(grep -oP '^\| Upscaling \| .*\(\K[^)]*(?=\) \|)' <<<"$b") | $p | $s | $v | $(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b") | $(grep -oP 'optical flow \(per source frame\) \| \K[0-9.]+' <<<"$b") |"
    rm -f "$out"
  done
done
echo
echo "### B. Same size (DLAA) with motion: compressed 1080p at 1080p"
echo
out="$R/b_dlaa.mkv"
b=$(bench "$media/lq_1080p.mp4" --upscale dlss --lossless -o "$out")
read -r p s v <<<"$(q "$out" "$media/src_1080p.mp4")"
echo "| DLAA + motion | PSNR $p | SSIM $s | VMAF $v | $(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b") ms + flow $(grep -oP 'optical flow \(per source frame\) \| \K[0-9.]+' <<<"$b") ms |"
rm -f "$out"
echo
echo "### C. Cost (bench, no file written)"
echo
echo "| Input | Output | Mode | Throughput (fps) | DLSS ms | Optical flow ms |"
echo "|---|---|---|---|---|---|"
for job in "lq_540p.mp4 1920x1080" "lq_720p.mp4 1920x1080" "src_1080p.mp4 3840x2160" "src_1080p.mp4 1920x1080"; do
  read -r f size <<<"$job"
  b=$(bench "$media/$f" --upscale dlss --size "$size")
  echo "| $f | $size | $(grep -oP '^\| Upscaling \| .*\(\K[^)]*(?=\) \|)' <<<"$b") | $(grep -oP 'Throughput \| \K[0-9.]+' <<<"$b") | $(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b") | $(grep -oP 'optical flow \(per source frame\) \| \K[0-9.]+' <<<"$b") |"
done
echo
echo "### D. Window playback (5 s, 540p in a 1920x1080 window, DLSS + frame gen MEDIUM per refresh, twice)"
echo
for i in 1 2; do
  play 5 "$media/lq_540p.mp4" --upscale dlss --width 1920 --height 1080 >/dev/null
  echo "    $(grep -E '^(Upscale|Display):' "$log" | tr '\n' ' ')"
done
