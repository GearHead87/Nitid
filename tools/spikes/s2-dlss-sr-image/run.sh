#!/usr/bin/env bash
# S2: DLSS Super Resolution in our own Vulkan app, on jittered renders of a still 1080p frame.
# Cases: 540p -> 1080p (Performance) and 720p -> 1080p (Quality), presets K and M, jitter sign +/-1.
# Scores each output (after frame 1 and after 32 frames) against the 1080p reference image,
# with ffmpeg bicubic / lanczos upscales of the unjittered low-res frame as baselines.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/s2-dlss-sr-image/s2-dlss-sr-image"
py="$root/third_party/vfx-venv/bin/python"
R="$root/results/s2-dlss-sr-image"
mkdir -p "$R"

[ -x "$bin" ] || { echo "build first: cmake -S tools/spikes -B build-spikes -G Ninja && cmake --build build-spikes"; exit 1; }
for r in 540p:960:540 720p:1280:720; do
  IFS=: read -r n w h <<<"$r"
  [ -f "$R/in_$n/jitter.txt" ] || "$py" "$(dirname "$0")/make_jittered.py" "$root/results/media/src_1080p.mp4" 150 "$R/in_$n" "$w" "$h" 32
done

metric() { # $1 image, $2 reference -> "psnr ssim"
  local log
  log=$(ffmpeg -hide_banner -nostdin -i "$1" -i "$2" -filter_complex "[0:v]format=yuv444p,split[a][b];[1:v]format=yuv444p,split[c][d];[a][c]psnr;[b][d]ssim" -f null - 2>&1)
  echo "$(grep -oE 'average:[0-9.inf]+' <<<"$log" | head -1 | cut -d: -f2) $(grep -oE 'SSIM Y:[0-9.]+' <<<"$log" | cut -d: -f2)"
}

echo "| Input | Method | Frames used | PSNR (dB) | SSIM (Y) | GPU ms / eval |"
echo "|---|---|---|---|---|---|"
for c in 540p:perf 720p:quality; do
  IFS=: read -r n mode <<<"$c"
  ref="$R/in_$n/reference.ppm"
  for f in bicubic lanczos; do
    o="$R/base_${n}_$f.ppm"
    ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/in_$n/lowres_nojitter.ppm" -vf "scale=1920:1080:flags=$f" "$o"
    read -r p s < <(metric "$o" "$ref"); echo "| $n | ffmpeg $f | 1 | $p | $s | – |"
  done
  for preset in K M; do
    for sign in +1 -1; do
      o="$R/out_${n}_${preset}_${mode}_${sign}"
      log=$("$bin" "$R/in_$n" "$o" 1920 1080 "$preset" "$mode" "$sign" 2>"$o.ngx.log")
      ms=$(grep -oE 'mean eval \(frames 5..[0-9]+\): [0-9.]+' <<<"$log" | grep -oE '[0-9.]+$')
      read -r p s < <(metric "${o}_frame01.ppm" "$ref"); echo "| $n | DLSS $preset $mode, jitter sign $sign | 1 | $p | $s | – |"
      read -r p s < <(metric "${o}_final.ppm" "$ref");  echo "| $n | DLSS $preset $mode, jitter sign $sign | 32 | $p | $s | $ms |"
    done
  done
done

# Crop grid (540p case): reference | lanczos / DLSS K +1 final | DLSS M +1 final
cr="crop=640:360:640:300"
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/in_540p/reference.ppm" -i "$R/base_540p_lanczos.ppm" \
  -i "$R/out_540p_K_perf_-1_final.ppm" -i "$R/out_540p_M_perf_-1_final.ppm" \
  -filter_complex "[0]$cr[a];[1]$cr[b];[2]$cr[c];[3]$cr[d];[a][b][c][d]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0" \
  "$R/grid_540p_ref_lanczos_dlssK_dlssM.png"
echo
echo "grid: $R/grid_540p_ref_lanczos_dlssK_dlssM.png (top: reference | lanczos, bottom: DLSS K | DLSS M, 32 frames, jitter sign -1)"
