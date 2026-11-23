#!/usr/bin/env bash
# V2: Do VFX VSR's same-resolution modes (denoise / deblur) clean up a compressed clip?
# Inputs are src_1080p.mp4 re-encoded at the same resolution, so the original is a true
# reference: a good cleanup moves the score closer to it (higher PSNR / SSIM / VMAF).
#   lq_1080p.mp4     ~3.2 Mbit/s (moderate, from fetch-media.sh)
#   vlq_1080p.mp4    600 kbit/s  (heavy, stream-like; made here)
# Also times each mode on the GPU only (frame already on the GPU).
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
py="$root/third_party/vfx-venv/bin/python"
samples="$root/third_party/nvidia-vfx-python-samples"
m="$root/results/media"
out="$root/results/v2-vfx-denoise-deblur"
mkdir -p "$out"
ref="$m/src_1080p.mp4"
lq="$m/lq_1080p.mp4"
vlq="$m/vlq_1080p.mp4"
[ -f "$vlq" ] || ffmpeg -hide_banner -loglevel error -y -i "$ref" -c:v libx264 -preset veryfast \
  -b:v 600k -maxrate 600k -bufsize 600k -an "$vlq"

echo "## Quality vs the original (higher is better)"
echo
echo "| Input | Processing | PSNR (dB) | SSIM (Y) | VMAF |"
echo "|---|---|---|---|---|"
for in in vlq_1080p lq_1080p; do
  read -r p s v < <("$root/scripts/quality.sh" "$m/$in.mp4" "$ref")
  echo "| $in | none (input) | $p | $s | $v |"
  for q in DENOISE_LOW DENOISE_MEDIUM DENOISE_HIGH DENOISE_ULTRA DEBLUR_LOW DEBLUR_HIGH; do
    o="$out/${in}_${q}.mp4"
    (cd "$samples" && "$py" video_super_resolution.py -i "$m/$in.mp4" -o "$o" --scale 1 --quality "$q" >/dev/null 2>&1) \
      || { echo "| $in | $q | FAILED | | |"; continue; }
    read -r p s v < <("$root/scripts/quality.sh" "$o" "$ref")
    echo "| $in | $q | $p | $s | $v |"
  done
done

echo
echo "## GPU-only time per 1080p frame"
echo
"$py" - "$vlq" <<'EOF'
import sys, av, numpy as np, torch
from nvvfx import VideoSuperRes
Q = VideoSuperRes.QualityLevel
with av.open(sys.argv[1]) as c:
    f = next(c.decode(video=0))
x = torch.from_numpy(np.ascontiguousarray(f.to_ndarray(format="rgb24"))).cuda().permute(2, 0, 1).float().div(255).contiguous()
st = torch.cuda.current_stream()
print("| Mode | ms / frame | Max fps |")
print("|---|---|---|")
for m in [Q.DENOISE_LOW, Q.DENOISE_MEDIUM, Q.DENOISE_HIGH, Q.DENOISE_ULTRA,
          Q.DEBLUR_LOW, Q.DEBLUR_MEDIUM, Q.DEBLUR_HIGH, Q.DEBLUR_ULTRA]:
    sr = VideoSuperRes(quality=m)
    sr.output_width, sr.output_height = x.shape[2], x.shape[1]
    sr.load()
    for _ in range(10):
        sr.run(x, stream_ptr=st.cuda_stream)
    a, b = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
    a.record(st)
    for _ in range(100):
        sr.run(x, stream_ptr=st.cuda_stream)
    b.record(st); b.synchronize(); sr.close()
    ms = a.elapsed_time(b) / 100
    print(f"| {m.name} | {ms:.2f} | {1000/ms:.0f} |")
EOF

echo
echo "## Side-by-side crops for viewing (results/v2-vfx-denoise-deblur/crops/)"
mkdir -p "$out/crops"
# Frames are picked by index (not by time) so every crop shows the same moment.
for n in 150 225; do  # bright, detailed frames (frame 40 is a black fade)
  for f in "$ref" "$vlq" "$out"/vlq_1080p_DENOISE_HIGH.mp4 "$out"/vlq_1080p_DEBLUR_HIGH.mp4; do
    b=$(basename "$f" .mp4)
    ffmpeg -hide_banner -loglevel error -y -i "$f" -vf "select=eq(n\,$n),crop=640:360:640:300" \
      -frames:v 1 "$out/crops/${b}_f$n.png"
  done
  ffmpeg -hide_banner -loglevel error -y -i "$out/crops/src_1080p_f$n.png" -i "$out/crops/vlq_1080p_f$n.png" \
    -i "$out/crops/vlq_1080p_DENOISE_HIGH_f$n.png" -i "$out/crops/vlq_1080p_DEBLUR_HIGH_f$n.png" \
    -filter_complex "[0][1][2][3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0" \
    "$out/crops/grid_ref_vlq_denoise_deblur_f$n.png"
  echo "grid: $out/crops/grid_ref_vlq_denoise_deblur_f$n.png (top: original | 600k input, bottom: DENOISE_HIGH | DEBLUR_HIGH)"
done
