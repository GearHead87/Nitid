#!/usr/bin/env bash
# S9: VFX VSR (non-integer output size) and VFX frame generation (RGBA8, explicit Timestep) from C++.
# Checks: VFG output vs the real middle frame, vs a 50/50 blend, vs NVIDIA's Python VFG (should be identical).
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$root/build-spikes/s9-vfx-cpp-api/s9-vfx-cpp-api"
py="$root/third_party/vfx-venv/bin/python"
R="$root/results/s9-vfx-cpp-api"
vfxlibs="$root/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs"
mkdir -p "$R"
[ -x "$bin" ] || { echo "build first (needs scripts/fetch-sdks.sh + vfx-env)"; exit 1; }

psnr() { ffmpeg -hide_banner -nostdin -i "$1" -i "$2" -lavfi "[0]format=rgb24[a];[1]format=rgb24[b];[a][b]psnr" -f null - 2>&1 \
  | grep -oE 'average:[0-9.inf]+' | cut -d: -f2; }

# Frames 150/151/152 of the 1080p clip (fast camera move); a 1440p copy for the "upscale first" case.
for n in 150 151 152; do
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$root/results/media/src_1080p.mp4" -vf "select=eq(n\,$n)" -frames:v 1 "$R/f$n.ppm"
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/f$n.ppm" -vf scale=2560:1440:flags=lanczos "$R/q$n.ppm"
done
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/f150.ppm" -i "$R/f152.ppm" -lavfi "[0][1]blend=all_mode=average" "$R/blend.ppm"
ffmpeg -hide_banner -loglevel error -nostdin -y -i "$R/f150.ppm" -vf scale=2560:1440:flags=lanczos "$R/lanczos_1440.ppm"

echo "### Timing (ms, median of 50, includes stream sync)"
echo
echo "| Case | VFG mode | VSR HIGH -> 2560x1440 | VFG one frame |"
echo "|---|---|---|---|"
for m in 0 1 2; do
  out=$(LD_LIBRARY_PATH="$vfxlibs" "$bin" "$R/f150.ppm" "$R/f151.ppm" "$R/f152.ppm" "$R" 2560 1440 "$m")
  cp "$R/vfg_t050.ppm" "$R/vfg_t050_m$m.ppm"
  echo "| 1080p input | $m | $(grep -oP 'vsr ok median_ms=\K[0-9.]+' <<<"$out" || echo FAIL) | $(grep -oP 'vfg ok median_ms=\K[0-9.]+' <<<"$out" || echo FAIL) |"
done
q=$(mkdir -p "$R/q" && LD_LIBRARY_PATH="$vfxlibs" "$bin" "$R/q150.ppm" "$R/q151.ppm" "$R/q152.ppm" "$R/q" 2560 1440 1)
echo "| 1440p input | 1 | (1:1) $(grep -oP 'vsr ok median_ms=\K[0-9.]+' <<<"$q" || echo FAIL) | $(grep -oP 'vfg ok median_ms=\K[0-9.]+' <<<"$q" || echo FAIL) |"

"$py" "$(dirname "$0")/reference_vfg.py" "$R/f150.ppm" "$R/f152.ppm" "$R/ref_vfg_t050.ppm" 1 >/dev/null
echo
echo "### Correctness (PSNR, dB)"
echo
echo "| Comparison | PSNR |"
echo "|---|---|"
echo "| C++ VFG MEDIUM t=0.5 vs NVIDIA Python VFG (same settings) | $(psnr "$R/vfg_t050_m1.ppm" "$R/ref_vfg_t050.ppm") |"
for m in 0 1 2; do echo "| VFG mode $m t=0.5 vs real middle frame | $(psnr "$R/vfg_t050_m$m.ppm" "$R/f151.ppm") |"; done
echo "| 50/50 blend vs real middle frame | $(psnr "$R/blend.ppm" "$R/f151.ppm") |"
echo "| repeat previous vs real middle frame | $(psnr "$R/f150.ppm" "$R/f151.ppm") |"
echo "| t=0.25 vs previous / vs next | $(psnr "$R/vfg_t025.ppm" "$R/f150.ppm") / $(psnr "$R/vfg_t025.ppm" "$R/f152.ppm") |"
echo "| t=0.75 vs previous / vs next | $(psnr "$R/vfg_t075.ppm" "$R/f150.ppm") / $(psnr "$R/vfg_t075.ppm" "$R/f152.ppm") |"
echo "| ShotChange=1 output vs previous / vs next | $(psnr "$R/vfg_shotchange.ppm" "$R/f150.ppm") / $(psnr "$R/vfg_shotchange.ppm" "$R/f152.ppm") |"
echo "| VSR 2560x1440 vs Lanczos 2560x1440 | $(psnr "$R/vsr.ppm" "$R/lanczos_1440.ppm") |"
