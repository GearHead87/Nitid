"""S3 comparison: VFX Video Frame Generation on exactly the same frames and steps as the
DLSS-G spike (same raw RGBA input, same luma PSNR formula, outputs quantised to 8-bit).

Usage: python compare_vfx.py <frames.rgba> <w> <h> <n> <step>
"""
import sys

import numpy as np
import torch
from nvvfx import VideoFrameGeneration

Mode = VideoFrameGeneration.Mode
path, w, h, n, step = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
raw = np.fromfile(path, dtype=np.uint8, count=w * h * 4 * n).reshape(n, h, w, 4)
frames = torch.from_numpy(raw[..., :3].copy()).cuda().permute(0, 3, 1, 2)  # n,3,h,w uint8


def y(x: torch.Tensor) -> torch.Tensor:  # uint8/float 0..255 CHW -> luma float64
    x = x.double()
    return 0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2]


def psnr(a: torch.Tensor, b: torch.Tensor) -> float:
    mse = torch.mean((y(a) - y(b)) ** 2).item()
    return 99.0 if mse < 1e-10 else 10 * np.log10(255.0**2 / mse)


st = torch.cuda.current_stream()
for mode, scd in [(Mode.LOW, False), (Mode.MEDIUM, False), (Mode.HIGH, False), (Mode.MEDIUM, True)]:
    g = VideoFrameGeneration(mode=mode, automatic_shot_change_detection_enabled=scd)
    g.input_width, g.input_height = w, h
    g.frame_multiplier = step
    g.load()
    vals = []
    for r in range(step, n, step):
        a = frames[r - step].float().div(255).contiguous()
        b = frames[r].float().div(255).contiguous()
        for k in range(1, step):
            o = torch.from_dlpack(g.run(a, b, frame_index=k, stream_ptr=st.cuda_stream).image)
            o8 = (o.clamp(0, 1) * 255).round()
            vals.append(psnr(o8, frames[r - step + k]))
    g.close()
    print(f"VFX step={step} mode={mode.name} shot_change_detection={'on' if scd else 'off'} "
          f"gen={len(vals)} mean_psnr={np.mean(vals):.2f}")
