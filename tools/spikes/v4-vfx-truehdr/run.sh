#!/usr/bin/env bash
# V4: Does VFX TrueHDR (SDR -> HDR10) run, how fast is it, and is the output tagged as HDR10?
# Viewing on an HDR display is skipped for now (see M3); this checks function, speed and metadata.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
py="$root/third_party/vfx-venv/bin/python"
samples="$root/third_party/nvidia-vfx-python-samples"
m="$root/results/media"
out="$root/results/v4-vfx-truehdr"
mkdir -p "$out"

echo "## 1. NVIDIA sample, end to end"
echo
for clip in src_1080p drift_360p60; do
  o="$out/${clip}_hdr10.mp4"
  log=$(cd "$samples" && "$py" true_hdr.py --input "$m/$clip.mp4" --output "$o" 2>&1) || {
    echo "$clip: FAILED"; tail -5 <<<"$log"; continue; }
  echo "### $clip"
  echo
  echo '```'
  grep -E 'FPS|fps|Frames|Encoder' <<<"$log" | sed 's/^ *//'
  ffprobe -v error -select_streams v:0 -show_entries \
    stream=codec_name,profile,pix_fmt,width,height,color_space,color_transfer,color_primaries,color_range \
    -of default=noprint_wrappers=1 "$o"
  ffprobe -v error -select_streams v:0 -read_intervals "%+#1" -show_frames \
    -show_entries frame=side_data_list -of default=noprint_wrappers=1 "$o" | grep -iE 'side_data_type|max_|min_|luminance' | head -8 || true
  echo '```'
  echo
done

echo "## 2. GPU-only time per frame"
echo
"$py" - "$m/src_1080p.mp4" <<'EOF'
import sys, av, numpy as np, torch, torch.nn.functional as F
from nvvfx import TrueHDR
with av.open(sys.argv[1]) as c:
    f = next(c.decode(video=0))
x0 = torch.from_numpy(np.ascontiguousarray(f.to_ndarray(format="rgb24"))).cuda().permute(2, 0, 1).float().div(255)
st = torch.cuda.current_stream()
print("| Resolution | ms / frame | Max fps | Output shape / dtype |")
print("|---|---|---|---|")
for (w, h) in [(1920, 1080), (2560, 1440), (3840, 2160)]:
    x = F.interpolate(x0[None], size=(h, w), mode="bilinear", align_corners=False)[0].contiguous()
    t = TrueHDR()
    t.load() if hasattr(t, "load") and not t.is_loaded else None
    for _ in range(10):
        r = t.run(x, stream_ptr=st.cuda_stream)
    st.synchronize()
    a, b = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
    a.record(st)
    for _ in range(100):
        r = t.run(x, stream_ptr=st.cuda_stream)
    b.record(st); b.synchronize()
    o = torch.from_dlpack(r.image)
    t.close()
    ms = a.elapsed_time(b) / 100
    print(f"| {w}x{h} | {ms:.2f} | {1000/ms:.0f} | {tuple(o.shape)} {o.dtype} |")
EOF
