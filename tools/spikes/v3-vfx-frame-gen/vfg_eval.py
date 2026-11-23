"""V3 evaluation of VFX VideoFrameGeneration (GPU-only).

1. Speed: ms per generated frame at 1080p / 1440p / 2160p for LOW / MEDIUM / HIGH.
2. Quality: drop frames from a real clip, let VFG rebuild them, compare with the real dropped
   frames (PSNR / SSIM-like score on luma). Baselines: repeat previous frame, 50/50 blend.
   - 2x test: from frames (i, i+2) generate t=0.5, compare with real frame i+1.
   - 4x test: from frames (i, i+4) generate t=0.25/0.5/0.75 (frame_index 1..3), compare with i+1..i+3.
3. Saves a crop comparison (real | VFG | blend) for one frame.

Usage: python vfg_eval.py <clip.mp4> <out_dir> [max_frames]
"""
import sys
from pathlib import Path

import av
import numpy as np
import torch
import torch.nn.functional as F
from nvvfx import VideoFrameGeneration, get_sdk_version

Mode = VideoFrameGeneration.Mode


def load_frames(path: str, n: int) -> list[torch.Tensor]:
    out = []
    with av.open(path) as c:
        for f in c.decode(video=0):
            out.append(torch.from_numpy(np.ascontiguousarray(f.to_ndarray(format="rgb24"))).cuda().permute(2, 0, 1))
            if len(out) >= n:
                break
    return out  # uint8 CHW on GPU


def f32(x: torch.Tensor) -> torch.Tensor:
    return x.float().div(255).contiguous()


def luma(x: torch.Tensor) -> torch.Tensor:  # x float CHW [0,1]
    return 0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2]


def psnr(a: torch.Tensor, b: torch.Tensor) -> float:
    mse = F.mse_loss(luma(a), luma(b)).item()
    return 99.0 if mse == 0 else 10 * np.log10(1.0 / mse)


def ssim(a: torch.Tensor, b: torch.Tensor) -> float:  # global-window SSIM on luma, 8x8 mean pooling
    x, y = luma(a)[None, None], luma(b)[None, None]
    mu_x, mu_y = F.avg_pool2d(x, 8), F.avg_pool2d(y, 8)
    sxx = F.avg_pool2d(x * x, 8) - mu_x**2
    syy = F.avg_pool2d(y * y, 8) - mu_y**2
    sxy = F.avg_pool2d(x * y, 8) - mu_x * mu_y
    c1, c2 = 0.01**2, 0.03**2
    s = ((2 * mu_x * mu_y + c1) * (2 * sxy + c2)) / ((mu_x**2 + mu_y**2 + c1) * (sxx + syy + c2))
    return s.mean().item()


def make(mode: Mode, w: int, h: int, mult: int) -> VideoFrameGeneration:
    g = VideoFrameGeneration(mode=mode)
    g.input_width, g.input_height = w, h
    g.frame_multiplier = mult
    g.load()
    return g


def speed(frames: list[torch.Tensor]) -> None:
    st = torch.cuda.current_stream()
    print("| Resolution | Mode | ms per generated frame | Max generated fps |")
    print("|---|---|---|---|")
    a0, b0 = f32(frames[10]), f32(frames[11])
    for (w, h) in [(1920, 1080), (2560, 1440), (3840, 2160)]:
        a = F.interpolate(a0[None], size=(h, w), mode="bilinear", align_corners=False)[0].contiguous()
        b = F.interpolate(b0[None], size=(h, w), mode="bilinear", align_corners=False)[0].contiguous()
        for m in Mode:
            g = make(m, w, h, 2)
            for _ in range(5):
                g.run(a, b, stream_ptr=st.cuda_stream)
            st.synchronize()
            e0, e1 = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
            e0.record(st)
            for _ in range(50):
                g.run(a, b, stream_ptr=st.cuda_stream)
            e1.record(st)
            e1.synchronize()
            g.close()
            ms = e0.elapsed_time(e1) / 50
            print(f"| {w}x{h} | {m.name} | {ms:.2f} | {1000 / ms:.0f} |", flush=True)


def quality(frames: list[torch.Tensor], out: Path) -> None:
    h, w = frames[0].shape[1:]
    st = torch.cuda.current_stream()
    rows = []
    saved = False
    for step, mult in [(2, 2), (4, 4)]:
        res = {"repeat previous": [], "50/50 blend": []}
        for m in Mode:
            res[f"VFG {m.name}"] = []
        gens = {m: make(m, w, h, mult) for m in Mode}
        for i in range(0, len(frames) - step, step):
            a, b = f32(frames[i]), f32(frames[i + step])
            for k in range(1, step):
                real = f32(frames[i + k])
                t = k / step
                res["repeat previous"].append((psnr(a, real), ssim(a, real)))
                blend = a * (1 - t) + b * t
                res["50/50 blend"].append((psnr(blend, real), ssim(blend, real)))
                for m, g in gens.items():
                    # VFG detects shot changes itself (default on); pass nothing extra.
                    o = torch.from_dlpack(g.run(a, b, frame_index=k, stream_ptr=st.cuda_stream).image).clone()
                    res[f"VFG {m.name}"].append((psnr(o, real), ssim(o, real)))
                    if not saved and mult == 2 and m == Mode.HIGH and i >= 140:
                        crop = lambda x: x[:, h // 4: h // 4 + 360, w // 3: w // 3 + 640]
                        grid = torch.cat([crop(real), crop(o), crop(blend)], dim=2)
                        img = (grid.clamp(0, 1) * 255).byte().permute(1, 2, 0).contiguous().cpu().numpy()
                        p = out / f"crop_real_vfg_blend_f{i + 1}.ppm"  # PPM: no PIL needed
                        with open(p, "wb") as fh:
                            fh.write(f"P6 {img.shape[1]} {img.shape[0]} 255\n".encode())
                            fh.write(img.tobytes())
                        saved = True
        for g in gens.values():
            g.close()
        for name, v in res.items():
            arr = np.array(v)
            rows.append((f"{mult}x", name, arr[:, 0].mean(), np.percentile(arr[:, 0], 5), arr[:, 1].mean(), len(v)))
    print("| Test | Method | Mean PSNR (dB) | Worst 5% PSNR (dB) | Mean SSIM | Frames compared |")
    print("|---|---|---|---|---|---|")
    for r in rows:
        print(f"| {r[0]} | {r[1]} | {r[2]:.2f} | {r[3]:.2f} | {r[4]:.4f} | {r[5]} |")


def main() -> None:
    clip, out = sys.argv[1], Path(sys.argv[2])
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    out.mkdir(parents=True, exist_ok=True)
    print(f"VFX SDK {get_sdk_version()} | {torch.cuda.get_device_name(0)} | clip {Path(clip).name}")
    frames = load_frames(clip, n)
    print(f"frames loaded: {len(frames)} at {frames[0].shape[2]}x{frames[0].shape[1]}")
    print()
    print("### Speed (GPU-only, 2x, input pair already on the GPU)")
    print()
    speed(frames)
    print()
    print("### Quality: rebuild dropped real frames")
    print()
    quality(frames, out)


if __name__ == "__main__":
    main()
