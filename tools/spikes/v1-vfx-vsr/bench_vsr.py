"""V1 benchmark: GPU-only time of VFX VideoSuperRes per frame.

Frames are already on the GPU (as they would be in Nitid), so this measures only
the effect, timed with CUDA events after a warm-up. Input is real video content
(a frame of results/media/src_1080p.mp4 resized on the GPU), not noise.

Usage: python bench_vsr.py <src_1080p.mp4> [frames]
"""
import sys
import time

import av
import numpy as np
import torch
import torch.nn.functional as F
from nvvfx import VideoSuperRes, get_sdk_version

Q = VideoSuperRes.QualityLevel
CASES = [  # (label, in_w, in_h, scale)
    ("540p -> 1080p", 960, 540, 2),
    ("720p -> 1440p", 1280, 720, 2),
    ("1080p -> 2160p", 1920, 1080, 2),
    ("720p -> 2160p", 1280, 720, 3),
    ("540p -> 2160p", 960, 540, 4),
]
MODES = [Q.BICUBIC, Q.LOW, Q.MEDIUM, Q.HIGH, Q.ULTRA, Q.HIGHBITRATE_HIGH, Q.STREAMING_MEDIUM, Q.STREAMING_ULTRA]


def first_frame(path: str) -> torch.Tensor:
    with av.open(path) as c:
        for f in c.decode(video=0):
            rgb = f.to_ndarray(format="rgb24")
            return torch.from_numpy(np.ascontiguousarray(rgb)).cuda().permute(2, 0, 1).float() / 255.0
    raise RuntimeError("no frame")


def bench(src: torch.Tensor, w: int, h: int, scale: int, mode: Q, n: int) -> tuple[float, float]:
    x = F.interpolate(src[None], size=(h, w), mode="area")[0].contiguous()
    stream = torch.cuda.current_stream()
    sr = VideoSuperRes(quality=mode)
    sr.output_width, sr.output_height = w * scale, h * scale
    t0 = time.perf_counter()
    sr.load()
    load_s = time.perf_counter() - t0
    for _ in range(10):  # warm-up
        sr.run(x, stream_ptr=stream.cuda_stream)
    stream.synchronize()
    start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
    start.record(stream)
    for _ in range(n):
        sr.run(x, stream_ptr=stream.cuda_stream)
    end.record(stream)
    end.synchronize()
    sr.close()
    return start.elapsed_time(end) / n, load_s


def main() -> None:
    path = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    print(f"VFX SDK {get_sdk_version()} | torch {torch.__version__} | {torch.cuda.get_device_name(0)}")
    src = first_frame(path)
    print()
    print("| Case | Mode | ms / frame | Max fps | Model load (s) |")
    print("|---|---|---|---|---|")
    for label, w, h, s in CASES:
        for m in MODES:
            try:
                ms, load = bench(src, w, h, s, m, n)
                print(f"| {label} | {m.name} | {ms:.2f} | {1000 / ms:.0f} | {load:.1f} |", flush=True)
            except Exception as e:  # unsupported combination -> record, keep going
                print(f"| {label} | {m.name} | error | – | {type(e).__name__}: {e} |", flush=True)
    print()
    print(f"peak GPU memory (torch allocator only): {torch.cuda.max_memory_allocated() / 2**20:.0f} MiB")


if __name__ == "__main__":
    main()
