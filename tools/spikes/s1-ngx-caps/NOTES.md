# S1: Are DLSS SR and DLSS Frame Generation available to our own native Vulkan app?

- **Result:** ✅ **yes, both**, with up to **5 generated frames (6x)**. No game, no Proton, no Streamline.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, DLSS SDK v310.9.16 (`libnvsdk_ngx.a` + `libnvidia-ngx-dlss*.so.310.9.16`),
  Vulkan 1.4.357, RTX 5080
- **How to run:** `scripts/fetch-sdks.sh`; `cmake -S tools/spikes -B build-spikes -G Ninja && cmake --build build-spikes`;
  `./build-spikes/s1-ngx-caps/s1-ngx-caps [-v]` (stdout = Markdown report, stderr = NGX log)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md), NGX log excerpt:
  [ngx-log-excerpt-2026-11-23.md](ngx-log-excerpt-2026-11-23.md)

## What it does

1. Asks NGX which Vulkan extensions DLSS SR / DLSS-G / RR / Reserved16 need (before creating Vulkan).
2. Creates a Vulkan 1.3 instance + device on the **NVIDIA GPU picked by vendor ID** (this PC also has an
   AMD iGPU) with those extensions.
3. `NVSDK_NGX_VULKAN_GetFeatureRequirements`, then `NVSDK_NGX_VULKAN_Init_with_ProjectID` (custom engine,
   feature search paths = DLSS SDK libs + the nvidia-vfx wheel libs), then `GetCapabilityParameters`.

## Results

| Item | Result |
|---|---|
| Vulkan extensions NGX needs (all features) | instance: `VK_KHR_get_physical_device_properties2`; device: `VK_NVX_binary_import`, `VK_NVX_image_view_handle`, `VK_KHR_buffer_device_address`, `VK_KHR_push_descriptor` |
| `NVSDK_NGX_VULKAN_Init_with_ProjectID` | ✅ Success |
| DLSS SR: requirements / `SuperSampling.Available` | Supported (min arch 0x160 = Turing) / **1** (min driver 470) |
| DLSS-G: requirements / `FrameGeneration.Available` | Supported (min arch 0x190 = Ada) / **1** (min driver 520) |
| `DLSSG.MultiFrameCountMax` | **5** → 2x, 3x, 4x, 5x, 6x possible |
| DLSS RR: requirements | Supported (`RayReconstruction.Available` is not a valid parameter name; ignore) |
| `Reserved16` (Veyra's driver VSR on Windows) | `0xBAD00012` = **NotImplemented** on Linux |
| GPU arch reported by NGX | 0x1b0 (Blackwell) |
| Feature libraries NGX found | `libnvidia-ngx-dlss/dlssg/dlssd.so.310.9.16` (SDK dir), `libnvidia-ngx-vsr.so.1.8.3` + `libnvidia-ngx-truehdr.so.1.2.3` (wheel dir), driver `libnvidia-ngx-builtin-dlssd.so.310.9.0` |

## Surprises / gotchas

- The NGX headers don't include `vulkan.h` themselves: include it (and `<cstddef>`) first.
- Linking the static `libnvsdk_ngx.a` from the SDK just worked with GCC 16 (`-ldl` + Vulkan loader).
  NVIDIA's guide calls it `libsdk_nvngx.a`; in SDK 310.9.16 the file is `libnvsdk_ngx.a`.
- Harmless log noise: missing `nvidia-ngx-conf.json` config files, `failed to create directory
  /usr/share/nvidia/ngx - Permission denied` / `could not create model folder` (NGX's model-update
  folder; not needed for the shipped models), `failed to find mapping file path` (project-ID mapping).
- VSR via raw NGX (`Reserved16`) is not implemented on Linux, even with `libnvidia-ngx-vsr.so` on the
  search path. On Linux, VSR is only reachable through the VFX SDK (V1), which is fine.
- Useful env vars from the DLSS-FG guide: `__NGX_LOG_LEVEL`, `__NGX_SHOW_INDICATOR=1`.

## What this means for PLAN.md

The biggest unknown before S3 is answered positively: a standalone Linux Vulkan app can create the
DLSS-G feature path, including multi-frame generation up to 6x. The DLSS-FG programming guide also
states that with raw NGX the **application** does presentation and pacing, which is exactly Nitid's
design. Next: S2 (DLSS SR on an image) and S3 (DLSS-G on real frames).
