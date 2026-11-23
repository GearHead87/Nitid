# vfx-env: shared Python environment for V1–V5

- **Result:** ✅ ready
- **Date:** 2026-11-23
- **How to run:** `scripts/fetch-sdks.sh`, then `tools/spikes/vfx-env/setup.sh`
  (creates `third_party/vfx-venv`, Python 3.12 via `uv`, from the samples' `requirements.txt`).
- **Install time:** ~10 min (mostly downloading PyTorch). **Size: 7.2 GB.**

## Installed

| Package | Version |
|---|---|
| Python | 3.12 (uv-managed; system Python 3.14 untouched) |
| nvidia-vfx | 0.2.0.0 (Python module name: `nvvfx`) |
| torch | 2.11.0+cu128 (`torch.cuda.is_available()` = True, RTX 5080) |
| av | 19.0.2 |
| numpy | 2.5.5 |

## What the `nvidia-vfx` wheel contains (important)

- `nvvfx/libs/libVideoFX.so`, `libNVCVImage.so`: the VFX SDK C runtime.
- `libnvVFXVideoSuperRes.so`, `libnvVFXVideoFrameGeneration.so`, `libnvVFXTrueHDR.so`: the effects.
- **`libnvidia-ngx-vsr.so.1.8.3`** and **`libnvidia-ngx-truehdr.so.1.2.3`**: Linux **NGX** feature
  libraries for Video Super Resolution and TrueHDR. So on Linux, VSR/TrueHDR are NGX features shipped
  by the application rather than by the driver; the VFX SDK loads them.
- Its own `libcudart` 12.9, `libcudnn` 9 and NPP 12 libraries.
- Licenses: NVIDIA Software License Agreement, NVIDIA Open Model License, product-specific terms
  (in `nvidia_vfx-0.2.0.0.dist-info/licenses/`). Read before redistributing anything.

## Gotchas

- `uv` needed `--index-strategy unsafe-best-match` because `requirements.txt` mixes PyPI and the
  PyTorch CUDA index.
- `/home` free space dropped to ~29 GB after this install.
