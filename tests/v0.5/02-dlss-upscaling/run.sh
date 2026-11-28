#!/usr/bin/env bash
# 0.5 step 2: DLSS Super Resolution as an upscaler (no motion vectors yet: step 3 adds optical flow).
#  A. Quality: 540p / 720p / heavily compressed 540p -> 1080p, whole clip vs the real 1080p video, next to bicubic
#     inside Nitid, NIS and VSR HIGH; DLSS presets J, K, L, M (Auto = K at 1.5x, M at 2x).
#  B. Same size (DLAA): compressed 1080p shown at 1080p: plain, VFX denoise, DLAA.
#  C. Scene cuts: DLSS history reset at cuts vs not (Sintel trailer at 540p -> 1080p): PSNR of the 4 frames after
#     each cut.
#  D. Speed: nitid-cli bench, per-frame upscale time (includes the CUDA <-> Vulkan copies).
#  E. Window: 5 s, 540p in a 1920x1080 window with frame gen MEDIUM per refresh.
# Outputs are lossless (FFV1) and deleted after scoring. Saves one crop comparison (crop_*.jpg, video frames only).
set -euo pipefail
source "$(dirname "$0")/../common.sh"
here=$(cd "$(dirname "$0")" && pwd)
R="$root/results/v0.5/02"; mkdir -p "$R"
q() { "$root/scripts/quality.sh" "$1" "$2"; }
bench() { "$nitid_cli" bench "$@" 2>/dev/null; }
label() {
  case $1 in
    bicubic) echo "bicubic inside Nitid" ;; nis) echo "NIS sharpness 0.5" ;; high) echo "VSR HIGH" ;;
    off) echo "plain (no processing)" ;; denoise-medium) echo "VFX denoise medium" ;;
    dlss) echo "**DLSS auto**" ;; dlss-*) echo "**DLSS preset ${1#dlss-}**" | tr a-z A-Z | sed 's/DLSS PRESET/DLSS preset/' ;;
  esac
}

echo "### A. Quality, upscaled to 1080p (whole 10 s clip vs the real 1080p video)"
echo
echo "| Source | Method | Mode | PSNR | SSIM | VMAF | ms / frame |"
echo "|---|---|---|---|---|---|---|"
for src in lq_540p lq_720p vlq_540p; do
  for u in bicubic nis high dlss-j dlss-k dlss-l dlss-m; do
    out="$R/a_${src}_$u.mkv"
    b=$(bench "$media/$src.mp4" --upscale "$u" --size 1920x1080 --lossless -o "$out")
    mode=$(grep -oP '^\| Upscaling \| .*\(\K[^)]*(?=\) \|)' <<<"$b" || true)
    ms=$(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b")
    read -r p s v <<<"$(q "$out" "$media/src_1080p.mp4")"
    echo "| $src | $(label $u) | ${mode:--} | $p | $s | $v | $ms |"
    [ "$src" = lq_540p ] && case $u in bicubic|high|dlss-m) cp "$out" "$R/keep_$u.mkv" ;; esac
    rm -f "$out"
  done
done
c="select=eq(n\,100),crop=480:300:700:400"
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/keep_bicubic.mkv" -i "$R/keep_high.mkv" -i "$R/keep_dlss-m.mkv" \
  -i "$media/src_1080p.mp4" -filter_complex "[0]$c[a];[1]$c[b];[2]$c[c];[3]$c[d];[a][b][c][d]hstack=4" \
  -frames:v 1 -q:v 3 "$here/crop_bicubic-vsr-dlss-real.jpg"
rm -f "$R"/keep_*.mkv

echo
echo "### B. Same size: compressed 1080p shown at 1080p (vs the real 1080p video)"
echo
echo "| Method | Mode | PSNR | SSIM | VMAF | ms / frame |"
echo "|---|---|---|---|---|---|"
for u in off denoise-medium dlss; do
  out="$R/b_$u.mkv"
  case $u in
    off) b=$(bench "$media/lq_1080p.mp4" --upscale off --lossless -o "$out") ;;
    denoise-medium) b=$(bench "$media/lq_1080p.mp4" --upscale high --cleanup denoise-medium --lossless -o "$out") ;;
    dlss) b=$(bench "$media/lq_1080p.mp4" --upscale dlss --lossless -o "$out") ;;
  esac
  mode=$(grep -oP '^\| Upscaling \| .*\(\K[^)]*(?=\) \|)' <<<"$b" || true)
  ms=$(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b")
  read -r p s v <<<"$(q "$out" "$media/src_1080p.mp4")"
  echo "| $(label $u) | ${mode:--} | $p | $s | $v | $ms |"
  rm -f "$out"
done

echo
echo "### C. Scene cuts: DLSS history reset (Sintel trailer, 540p -> 1080p, preset M)"
echo
lo="$R/sintel_540p.mp4"
[ -f "$lo" ] || ffmpeg -hide_banner -loglevel error -nostdin -y -i "$media/sintel_trailer-1080p.mp4" -an \
  -vf scale=960:540:flags=bicubic -c:v libx264 -crf 12 -preset fast "$lo"
cuts=$("$nitid_cli" bench "$lo" --upscale off -v 2>&1 | grep -oP 'scene cut at frame \K[0-9]+' | tr '\n' ' ')
echo "Cuts found: $(wc -w <<<"$cuts") ($cuts)"
echo
echo "| History at cuts | Mean PSNR, 4 frames after each cut | Mean PSNR, whole trailer |"
echo "|---|---|---|"
for mode in reset kept; do
  out="$R/c_$mode.mkv"
  extra=(); [ $mode = kept ] && extra=(--no-scene-cuts)
  bench "$lo" --upscale dlss-m --size 1920x1080 --lossless -o "$out" "${extra[@]}" >/dev/null
  ffmpeg -hide_banner -loglevel fatal -nostdin -i "$out" -i "$media/sintel_trailer-1080p.mp4" -lavfi \
    "[0]format=yuv420p,settb=AVTB,setpts=N*40000[a];[1]format=yuv420p,settb=AVTB,setpts=N*40000[b];[a][b]psnr=stats_file=$R/c_$mode.txt" -f null -
  # PSNR from the mean squared error over the frames (black title frames would dominate a mean of per-frame PSNR)
  read -r after all <<<"$(awk -v cuts="$cuts" 'BEGIN{n=split(cuts,c," "); for(i=1;i<=n;i++) for(k=0;k<4;k++) want[c[i]+k]=1}
    {split($1,a,":"); f=a[2]-1; for(i=1;i<=NF;i++) if($i ~ /^mse_avg:/){split($i,b,":"); v=b[2]}
     sum+=v; cnt++; if(f in want){s2+=v; c2++}}
    END{printf "%.2f %.2f", 10*log(65025/(s2/c2))/log(10), 10*log(65025/(sum/cnt))/log(10)}' "$R/c_$mode.txt")"
  echo "| $mode | $after | $all |"
  rm -f "$out" "$R/c_$mode.txt"
done

echo
echo "### D. Speed (nitid-cli bench, no file written)"
echo
echo "| Input | Output | Method | Mode | Throughput (fps) | ms / frame |"
echo "|---|---|---|---|---|---|"
for job in "lq_540p.mp4 1920x1080" "lq_720p.mp4 1920x1080" "src_1080p.mp4 3840x2160" "lq_720p.mp4 3840x2160" "src_1080p.mp4 1920x1080"; do
  read -r f size <<<"$job"
  for u in high dlss; do
    [ "$size" = 1920x1080 ] && [ "$f" = src_1080p.mp4 ] && [ $u = high ] && continue
    b=$(bench "$media/$f" --upscale "$u" --size "$size")
    echo "| $f | $size | $(label $u) | $(grep -oP '^\| Upscaling \| .*\(\K[^)]*(?=\) \|)' <<<"$b" || echo -) | $(grep -oP 'Throughput \| \K[0-9.]+' <<<"$b") | $(grep -oP 'Upscale / clean-up \(per source frame\) \| \K[0-9.]+' <<<"$b") |"
  done
done

echo
echo "### E. Window playback (5 s, 540p in a 1920x1080 window, frame gen MEDIUM per refresh)"
echo
echo "| Upscaler | Display fps | Missed refreshes | Late | Skipped |"
echo "|---|---|---|---|---|"
for u in high dlss high dlss; do
  play 5 "$media/lq_540p.mp4" --upscale "$u" --width 1920 --height 1080 >/dev/null
  echo "| $(label $u) | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') | $(field '([0-9]+) late frames') | $(field '([0-9]+) skipped' || echo 0) |"
done
