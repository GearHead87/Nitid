"""S2 input generator: jittered low-resolution renders of one still 1080p frame.

Like a game rendering for DLSS: every low-res frame samples the scene (here: the 1080p
reference frame) at a slightly different sub-pixel offset (Halton 2,3 jitter in
[-0.5, 0.5) low-res pixels). Writes frame_XX.png, jitter.txt ("jx jy" per line, low-res
pixels, sample offset from the pixel centre) and reference.png (the 1080p truth).

Usage: python make_jittered.py <video> <frame_index> <out_dir> <lr_w> <lr_h> [frames=32]
"""
import sys
from pathlib import Path

import av
import numpy as np
import torch
import torch.nn.functional as F


def halton(i: int, b: int) -> float:
    f, r = 1.0, 0.0
    while i > 0:
        f /= b
        r += f * (i % b)
        i //= b
    return r


def save_ppm(path: Path, img: np.ndarray) -> None:
    with open(path, "wb") as fh:
        fh.write(f"P6 {img.shape[1]} {img.shape[0]} 255\n".encode())
        fh.write(np.ascontiguousarray(img).tobytes())


def main() -> None:
    video, idx, out = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    lw, lh = int(sys.argv[4]), int(sys.argv[5])
    n = int(sys.argv[6]) if len(sys.argv) > 6 else 32
    out.mkdir(parents=True, exist_ok=True)
    with av.open(video) as c:
        for k, f in enumerate(c.decode(video=0)):
            if k == idx:
                ref = f.to_ndarray(format="rgb24")
                break
    hh, hw = ref.shape[:2]
    save_ppm(out / "reference.ppm", ref)
    hr = torch.from_numpy(ref).permute(2, 0, 1).float()[None] / 255.0
    sx, sy = hw / lw, hh / lh
    ys, xs = torch.meshgrid(torch.arange(lh).float(), torch.arange(lw).float(), indexing="ij")
    with open(out / "jitter.txt", "w") as jf:
        for k in range(n):
            jx, jy = halton(k + 1, 2) - 0.5, halton(k + 1, 3) - 0.5
            # sample position in HR pixel coordinates for low-res pixel centre + jitter
            x = (xs + 0.5 + jx) * sx
            y = (ys + 0.5 + jy) * sy
            grid = torch.stack([x / hw * 2 - 1, y / hh * 2 - 1], dim=-1)[None]
            lr = F.grid_sample(hr, grid, mode="bilinear", padding_mode="border", align_corners=False)
            save_ppm(out / f"frame_{k:02d}.ppm", (lr[0].permute(1, 2, 0).clamp(0, 1) * 255).round().byte().numpy())
            jf.write(f"{jx:.6f} {jy:.6f}\n")
    # unjittered low-res frame for the bicubic baseline
    grid = torch.stack([(xs + 0.5) * sx / hw * 2 - 1, (ys + 0.5) * sy / hh * 2 - 1], dim=-1)[None]
    lr = F.grid_sample(hr, grid, mode="bilinear", padding_mode="border", align_corners=False)
    save_ppm(out / "lowres_nojitter.ppm", (lr[0].permute(1, 2, 0).clamp(0, 1) * 255).round().byte().numpy())
    print(f"wrote {n} jittered {lw}x{lh} frames + reference {hw}x{hh} to {out}")


if __name__ == "__main__":
    main()
