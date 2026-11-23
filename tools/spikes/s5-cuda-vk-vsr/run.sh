#!/usr/bin/env bash
# S5: Vulkan -> CUDA -> VFX VSR -> Vulkan with no CPU copy in the loop.
# Runs the C++ spike on real frames (540p -> 1080p, 1080p -> 2160p; HIGH and ULTRA), then checks the
# output against NVIDIA's Python VSR on the same frame (PSNR between the two should be very high).
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/s5-cuda-vk-vsr/s5-cuda-vk-vsr"
py="$root/third_party/vfx-venv/bin/python"
R="$root/results/s5-cuda-vk-vsr"
# The VFX runtime libs need their bundled NPP/cuDNN siblings on the library path (NVIDIA's Python loader preloads them).
vfxlibs="$root/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs"
mkdir -p "$R"
[ -x "$bin" ] || { echo "build first (needs scripts/fetch-sdks.sh + vfx-env)"; exit 1; }

ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/lq_540p.mp4" -vf "select=eq(n\,150)" -frames:v 1 "$R/in_540p.ppm"
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/src_1080p.mp4" -vf "select=eq(n\,150)" -frames:v 1 "$R/in_1080p.ppm"

psnr() { ffmpeg -hide_banner -nostdin -i "$1" -i "$2" -lavfi "[0]format=rgb24[a];[1]format=rgb24[b];[a][b]psnr" -f null - 2>&1 \
  | grep -oE 'average:[0-9.inf]+' | cut -d: -f2; }

echo "| Case | Quality | VSR GPU ms (median) | Full round trip ms (median / p90) | PSNR vs NVIDIA Python VSR |"
echo "|---|---|---|---|---|"
for c in 540p:2 1080p:2; do
  IFS=: read -r n s <<<"$c"
  for q in 3:HIGH 4:ULTRA; do
    qi=${q%%:*}; qn=${q##*:}
    out="$R/out_${n}_${qn}.ppm"; ref="$R/ref_${n}_${qn}.ppm"
    res=$(LD_LIBRARY_PATH="$vfxlibs" "$bin" "$R/in_$n.ppm" "$out" "$s" "$qi" 200 | tee "$R/log_${n}_${qn}.txt" | grep RESULT)
    "$py" "$(dirname "$0")/reference_vsr.py" "$R/in_$n.ppm" "$ref" "$s" "$qi" >/dev/null
    vsr=$(grep -oE 'vsr_ms_median=[0-9.]+' <<<"$res" | cut -d= -f2)
    rt=$(grep -oE 'round_trip_ms_median=[0-9.]+' <<<"$res" | cut -d= -f2)
    rt9=$(grep -oE 'round_trip_ms_p90=[0-9.]+' <<<"$res" | cut -d= -f2)
    echo "| $n x$s | $qn | $vsr | $rt / $rt9 | $(psnr "$out" "$ref") dB |"
  done
done
head -1 "$R/log_540p_HIGH.txt"
