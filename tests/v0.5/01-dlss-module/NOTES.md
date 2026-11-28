# 0.5 step 1: DLSS module, NVIDIA's DLSS runtime, GPU check

- **Result:** ✅ DLSS starts natively inside Nitid: DLSS 310.9.16, Super Resolution yes, Frame Generation up to 6x,
  hardware optical flow yes. `nitid-setup-dlss` installs NVIDIA's runtime (64 MB, 9 s); missing pieces are reported,
  not fatal; playback is unchanged.
- **Date:** 2026-11-28
- **Versions:** driver 620.73.04, DLSS SDK 310.9.16, RTX 5080, KDE Wayland 165 Hz
- **How to run:** `tests/v0.5/01-dlss-module/run.sh`
- **Output:** [output-2026-11-28.md](output-2026-11-28.md)

## What was built

| Piece | What it is |
|---|---|
| `libnitid/dlss-module/` → `libnitid-dlss.so` | All DLSS SDK (NGX) code: init, DLSS SR and DLSS-G create / evaluate. Statically links NVIDIA's `libnvsdk_ngx.a`. Built only when the SDK is present |
| `libnitid/src/dlss/plugin_api.h` | The small C table between libnitid and the module (Vulkan handles and sizes only, no SDK types) |
| `libnitid/src/dlss/runtime.*` | Loads the module (`dlopen`, next to libnitid), finds NVIDIA's runtime, starts NGX on Nitid's Vulkan device |
| `gpu/vulkan_context` | Enables NGX's extensions (all or none), buffer device address, `VK_NV_optical_flow` + its queue, and a third queue for the pipeline thread |
| `gpu/vk_resources.*` | Images, command buffer + fence, buffer ↔ image copies for the pipeline thread's Vulkan work |
| `packaging/linux/nitid-setup-dlss` | Downloads `libnvidia-ngx-dlss.so` / `-dlssg.so` 310.9.16 and NVIDIA's license from github.com/NVIDIA/DLSS, checks SHA-256, installs into `~/.local/share/nitid/dlss` |
| `Capabilities` | `dlss_version`, `dlss_error`, `dlss_super_res`, `dlss_frame_gen`, `dlss_max_multiplier`, `optical_flow`; `nitid-cli info` prints them |

Why a separate module: NVIDIA's DLSS SDK license forbids making the SDK subject to an open-source license. Keeping it
in its own file that libnitid only loads at run time keeps that choice open for Nitid's own license (still to be
decided).

## Results

| Check | Result |
|---|---|
| A. `nitid-cli info`, developer build | DLSS 310.9.16, upscaling yes, frame gen yes up to 6x, optical flow yes |
| B. `nitid-setup-dlss` | 64 MB in 9 s, SHA-256 checked; Nitid picks it from `~/.local/share/nitid/dlss`; second run says "already installed"; `--uninstall` removes it |
| C. No runtime / no module | `not available: NVIDIA's DLSS runtime is not installed (run nitid-setup-dlss)` / `... libnitid-dlss.so not found`; Nitid runs |
| D. Playback unchanged | 165 fps with VSR HIGH + frame gen. The scripted run showed 23–34 missed refreshes (desktop noise); a direct A/B right after, 0.4.0 tarball vs this build, twice each: **0 missed refreshes for both** |

## Surprises / gotchas

- The static `libnvsdk_ngx.a` links into a shared module (`-fPIC` not needed on our side); `--no-undefined` passes.
- The SDK headers carry no "310.9.16" string; the version comes from CMake (`NITID_DLSS_SDK_VERSION`).
- `NITID_DLSS_DIR` / `NITID_DLSS_MODULE` now *replace* the search (only that folder / file), so tests can hide DLSS.
- Headless engines create the GPU device in `Engine::create()` now, so DLSS availability is known at once; with a
  window it is known after `attach_window()`.
