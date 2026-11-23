# M3: Does HDR output work on KDE Wayland + NVIDIA?

- **Result:** ⏭️ skipped for now (user decision, 2026-11-23)
- **Date:** 2026-11-23
- **Known so far:** KDE reports `HDR: disabled` for DP-6 (2560x1440 @ 165 Hz), not "incapable".
  Vulkan exposes `VK_EXT_swapchain_colorspace` and `VK_EXT_hdr_metadata` (see 00-sysinfo).
- **How to run later:** turn on HDR in KDE Display Settings, then
  `mpv --vo=gpu-next --gpu-api=vulkan --target-colorspace-hint <HDR10 clip>`.
- **Pass if:** KDE shows HDR on, the mpv log shows PQ/BT.2020 output, highlights look brighter.

## What this means for PLAN.md

Until this runs, plan for SDR output first; HDR stays a later item (also affects V4 viewing and H1).
