"""V3 follow-up: why does MEDIUM score below LOW/HIGH on the Sintel clip?
Rebuilds dropped frames (2x) with automatic shot-change detection on vs off, per mode,
and lists the pairs where MEDIUM loses most against HIGH.
Usage: python medium_check.py <clip.mp4>
"""
import sys

import numpy as np
import torch
from nvvfx import VideoFrameGeneration

from vfg_eval import f32, load_frames, psnr

Mode = VideoFrameGeneration.Mode
frames = load_frames(sys.argv[1], 240)
h, w = frames[0].shape[1:]
st = torch.cuda.current_stream()
per = {}
print("| Mode | Shot-change detection | Mean PSNR (dB) |")
print("|---|---|---|")
for m in Mode:
    for scd in (True, False):
        g = VideoFrameGeneration(mode=m, automatic_shot_change_detection_enabled=scd)
        g.input_width, g.input_height = w, h
        g.load()
        vals = []
        for i in range(0, len(frames) - 2, 2):
            a, b, real = f32(frames[i]), f32(frames[i + 2]), f32(frames[i + 1])
            o = torch.from_dlpack(g.run(a, b, stream_ptr=st.cuda_stream).image).clone()
            vals.append(psnr(o, real))
        g.close()
        per[(m, scd)] = np.array(vals)
        print(f"| {m.name} | {'on' if scd else 'off'} | {np.mean(vals):.2f} |", flush=True)
d = per[(Mode.HIGH, True)] - per[(Mode.MEDIUM, True)]
worst = np.argsort(-d)[:8]
print()
print("Pairs where MEDIUM loses most vs HIGH (frame index of the rebuilt frame, PSNR HIGH / MEDIUM):")
for k in worst:
    print(f"  frame {2 * k + 1}: {per[(Mode.HIGH, True)][k]:.2f} / {per[(Mode.MEDIUM, True)][k]:.2f}")
