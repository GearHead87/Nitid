"""S9 reference: NVIDIA's Python VideoFrameGeneration on the same pair (MEDIUM, auto shot-change off, t=0.5).

Usage: python reference_vfg.py a.ppm c.ppm out.ppm [mode]
"""
import sys

import numpy as np
import torch
from nvvfx import VideoFrameGeneration


def read_ppm(p: str) -> torch.Tensor:
    with open(p, "rb") as f:
        data = f.read()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    img = np.frombuffer(parts[4][: w * h * 3], np.uint8).reshape(h, w, 3)
    return torch.from_numpy(img.copy()).cuda().permute(2, 0, 1).float().div(255).contiguous()


a, c = read_ppm(sys.argv[1]), read_ppm(sys.argv[2])
mode = int(sys.argv[4]) if len(sys.argv) > 4 else 1
g = VideoFrameGeneration(mode=VideoFrameGeneration.Mode(mode), automatic_shot_change_detection_enabled=False)
g.input_width, g.input_height = a.shape[2], a.shape[1]
g.load()
o = torch.from_dlpack(g.run_at_timestep(a, c, 0.5).image).clone()
img = (o.clamp(0, 1) * 255).round().byte().permute(1, 2, 0).contiguous().cpu().numpy()
with open(sys.argv[3], "wb") as f:
    f.write(f"P6 {img.shape[1]} {img.shape[0]} 255\n".encode())
    f.write(img.tobytes())
