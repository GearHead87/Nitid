# V4: Does VFX TrueHDR (SDR → HDR10) work?

- **Result:** ⚠️ partial: ✅ runs, ✅ very fast, ✅ output correctly tagged HDR10; ⏭️ visual check on an
  HDR display skipped for now (HDR testing postponed, see M3)
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, VFX SDK 1.3.0, RTX 5080
- **How to run:** `tools/spikes/v4-vfx-truehdr/run.sh`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md); videos in `results/v4-vfx-truehdr/`

## Output file check (NVIDIA sample, Sintel 1080p and Drift 360p60)

| Property | Value |
|---|---|
| Codec / profile | HEVC Main 10 (`hevc_nvenc`, p010le) |
| Pixel format | yuv420p10le |
| Colour primaries / matrix | BT.2020 / BT.2020 non-constant |
| Transfer | SMPTE ST 2084 (PQ) |
| Range | limited (tv) |
| HDR10 static metadata (mastering display, MaxCLL) | **not present** in the file |
| Sample speed (incl. CPU decode + encode) | 1080p: 118 fps, 360p: 848 fps |

## GPU-only time per frame (default settings: luminance 650 nits, contrast/saturation 100, middle gray 50)

| Resolution | ms / frame | Max fps |
|---|---|---|
| 1920x1080 | 0.44 | 2261 |
| 2560x1440 | 0.65 | 1549 |
| 3840x2160 | 1.13 | 883 |

Output is a packed RGB10A2 image (`uint32` per pixel, shape H×W), already PQ-encoded BT.2020.

## Surprises / gotchas

- No HDR10 static metadata is written. For display, Nitid must set it itself
  (`VK_EXT_hdr_metadata`, e.g. MaxCLL ≈ the TrueHDR `luminance` setting, 400–2000 nits).
- Tunables: `luminance` 400–2000 (default 650), `contrast` / `saturation` 0–200 (100),
  `middle_gray` 10–100 (50), `debanding_off` 0/1.

## Still to do

- [ ] With HDR enabled in KDE: play `results/v4-vfx-truehdr/src_1080p_hdr10.mp4` with
  `mpv --vo=gpu-next --gpu-api=vulkan --target-colorspace-hint` and judge it (together with M3).

## What this means for PLAN.md

TrueHDR costs ~1 ms even at 4K, so it fits any pipeline. Its RGB10A2 PQ output can go straight to
an HDR10 swapchain (`A2B10G10R10_UNORM` + `HDR10_ST2084` colour space) once H1 confirms HDR output.
