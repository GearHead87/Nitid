#!/usr/bin/env bash
# 0.3 step 3: optional VFX clean-up at the video's own size (VSR denoise / deblur modes), used when nothing upscales.
#  A. Quality: 1080p clips re-encoded at 1080p (lq ~3.2 Mbit/s, vlq 600 kbit/s, blocky) vs the original 1080p video,
#     whole clip, through Nitid (so "off" includes Nitid's RGB round trip and is the fair baseline).
#  B. Speed: per-frame time at 1080p and 4K.
#  C. Window: 4K video in a smaller window (no upscaling) with denoise + frame gen MEDIUM per refresh, 10 s.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
R="$root/results/v0.3/03"; mkdir -p "$R"
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }
echo "### A. Quality vs the original 1080p video"
echo
echo "| Clip | Clean-up | PSNR | SSIM | VMAF |"
echo "|---|---|---|---|---|"
for clip in vlq_1080p lq_1080p; do
  for c in off denoise-low denoise-medium denoise-high denoise-ultra deblur-low deblur-high; do
    bench "$media/$clip.mp4" --upscale off --cleanup $c --lossless -o "$R/$clip-$c.mkv" >/dev/null
    read -r p s v <<<"$("$root/scripts/quality.sh" "$R/$clip-$c.mkv" "$media/src_1080p.mp4")"
    echo "| $clip | $c | $p | $s | $v |"
    rm -f "$R/$clip-$c.mkv"
  done
done
echo
echo "### B. Speed (ms per frame)"
echo
echo "| Input | Clean-up | ms per frame | Throughput (fps) |"
echo "|---|---|---|---|"
for f in vlq_1080p.mp4 src_4k_hevc.mkv; do
  for c in denoise-medium denoise-ultra deblur-high; do
    out=$(bench "$media/$f" --upscale off --cleanup $c --frames 240)
    echo "| $f | $c | $(grep -oP 'clean-up \(per source frame\) \| \K[0-9.]+' <<<"$out") | $(grep -oP 'Throughput \| \K[0-9.]+' <<<"$out") |"
  done
done
echo
echo "### C. Window: 4K60 HEVC in the default window (no upscaling), denoise medium + frame gen MEDIUM per refresh, 10 s"
echo
play 10 "$media/src_4k_hevc.mkv" --cleanup denoise-medium --loop --volume 0.2 >/dev/null
echo "- $(field 'Upscale:\s+\K.*')"
echo "- $(field 'FrameGen:\s+\K.*')"
echo "- Display: $(field 'Display:\s+\K.*')"
