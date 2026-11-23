"""S5 reference: NVIDIA's Python VSR (nvvfx) on the same PPM frame, written as PPM.
Usage: python reference_vsr.py <in.ppm> <out.ppm> <scale> <quality>
"""
import sys

import numpy as np
import torch
from nvvfx import VideoSuperRes


def read_ppm(p):
    with open(p, "rb") as f:
        data = f.read()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(parts[4], dtype=np.uint8, count=w * h * 3).reshape(h, w, 3)


src, dst, scale, q = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
img = read_ppm(src)
x = torch.from_numpy(img.copy()).cuda().permute(2, 0, 1).float().div(255).contiguous()
sr = VideoSuperRes(quality=VideoSuperRes.QualityLevel(q))
sr.output_width, sr.output_height = img.shape[1] * scale, img.shape[0] * scale
sr.load()
o = torch.from_dlpack(sr.run(x).image).clone()
sr.close()
out = (o.clamp(0, 1) * 255).round().byte().permute(1, 2, 0).contiguous().cpu().numpy()
with open(dst, "wb") as f:
    f.write(f"P6 {out.shape[1]} {out.shape[0]} 255\n".encode())
    f.write(out.tobytes())
print("reference written", dst, out.shape)
