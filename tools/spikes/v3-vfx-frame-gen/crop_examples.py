"""V3: save a 2x2 crop grid for one rebuilt frame:
real | 50/50 blend
VFG HIGH (shot-change detection off) | VFG MEDIUM (shot-change detection on, the default)
Usage: python crop_examples.py <clip.mp4> <rebuilt_frame_index(odd)> <out.ppm>
"""
import sys

import torch
from nvvfx import VideoFrameGeneration

from vfg_eval import f32, load_frames

Mode = VideoFrameGeneration.Mode
clip, k, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
frames = load_frames(clip, k + 2)
h, w = frames[0].shape[1:]
a, b, real = f32(frames[k - 1]), f32(frames[k + 1]), f32(frames[k])
st = torch.cuda.current_stream()


def gen(mode, scd):
    g = VideoFrameGeneration(mode=mode, automatic_shot_change_detection_enabled=scd)
    g.input_width, g.input_height = w, h
    g.load()
    # feed the previous pair first so the effect has the same history as in the sequence run
    g.run(f32(frames[k - 3]), a, stream_ptr=st.cuda_stream)
    o = torch.from_dlpack(g.run(a, b, stream_ptr=st.cuda_stream).image).clone()
    g.close()
    return o


crop = lambda x: x[:, h // 4: h // 4 + 360, w // 3: w // 3 + 640]
top = torch.cat([crop(real), crop((a + b) / 2)], dim=2)
bot = torch.cat([crop(gen(Mode.HIGH, False)), crop(gen(Mode.MEDIUM, True))], dim=2)
img = (torch.cat([top, bot], dim=1).clamp(0, 1) * 255).byte().permute(1, 2, 0).contiguous().cpu().numpy()
with open(out, "wb") as fh:
    fh.write(f"P6 {img.shape[1]} {img.shape[0]} 255\n".encode())
    fh.write(img.tobytes())
print("saved", out)
