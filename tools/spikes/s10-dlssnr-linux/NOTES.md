# S10: Can our own Linux app use DLSS Neural Rendering ("DLSS 5", NGX feature 18; Veyra's "NR")?

- **Result:** ❌ **not natively, today.** Driver 620.73.04's NGX core *knows* the feature (it searches for
  `libnvidia-ngx-dlssnr.so` in the app's paths and `libnvidia-ngx-builtin-dlssnr.so` in `/usr/lib`), but
  NVIDIA ships no Linux library for it, so `DLSSNR.Available = 0` and
  `DLSSNR.FeatureInitResult = 0xBAD00004 (FeatureNotFound)`.
- **Date:** 2026-11-28
- **Versions:** driver 620.73.04, DLSS SDK 310.9.16, RTX 5080, Veyra source at `b49fd2b` (2.2.2)
- **How to run:** build spikes, then `tools/spikes/s10-dlssnr-linux/run.sh`
- **Full output:** [output-2026-11-28.md](output-2026-11-28.md), NGX log:
  [ngx-log-excerpt-2026-11-28.md](ngx-log-excerpt-2026-11-28.md)

## Method

Copy of S1 (native Vulkan NGX init on the NVIDIA GPU) with feature 18 added to the requirement query and the
`DLSSNR.*` capability parameters read after init. The parameter names come from the driver
(`strings libnvidia-ngx.so.620.73.04`) and match Veyra's `include/veyra/ngx/DlssNrParameters.h`.

## Results

| Item | Result |
|---|---|
| `GetFeatureRequirements(Reserved18)` | `0xBAD00012` NotImplemented (same as Reserved16 = driver VSR) |
| `DLSSNR.Available` | **0** |
| `DLSSNR.FeatureInitResult` | `0xBAD00004` FeatureNotFound |
| Libraries the driver looks for | `libnvidia-ngx-dlssnr.so` (feature search paths), `libnvidia-ngx-builtin-dlssnr.so` (`/usr/lib`): none exist |
| DLSS SR / DLSS-G (control) | still available, MultiFrameCountMax 5 (as S1) |

## How Veyra does it (Windows only)

- `src/ngx/DlssNrRuntimeAdapter.cpp` loads `nvngx_dlssnr.dll` **directly** (not through the driver's NGX
  dispatch) and calls its own `NVSDK_NGX_D3D12_Init_Ext / CreateFeature(18) / EvaluateFeature` exports, with a
  hard-coded "signed snippet" app ID and an import-table patch so the DLL believes its caller is `nvngx.dll`.
- The DLL is not in any public SDK. Veyra offers an NVIDIA-signed 310.8.0 build and community-modified ones
  (Lecram 310.8.3, SF-v2 310.8.2 for RTX 30); none are in its source repo.
- Inputs: colour, motion vectors (from optical flow), depth, plus style / intensity / skin / structure controls.
  It is D3D12-only: the exports take `ID3D12Device*` / `ID3D12GraphicsCommandList*`.

## Linux community routes (not tested here)

All run the **Windows DLL under Wine/Proton** (vkd3d-proton gives it D3D12): ReShade add-ons for games
(NapXDD/addon-dlssnr-linux and forks), and DLSS5VKLayer / DLSSNR-Video, which run a Windows helper `.exe`
under Wine next to a native Vulkan app. None is a native Linux library.

## What this means for PLAN.md

- Native DLSS NR in Nitid is blocked until NVIDIA ships `libnvidia-ngx-dlssnr.so` (the driver is already
  wired to load it, so re-run this test after each driver update).
- The only route today is a Wine/Proton helper process running the Windows DLL, sharing frames with Nitid.
  Possible, but fragile, depends on an unreleased NVIDIA binary we can't redistribute, and adds a GPU copy
  between D3D12 (vkd3d) and Nitid. Not part of Phase 4 unless the user decides otherwise.
