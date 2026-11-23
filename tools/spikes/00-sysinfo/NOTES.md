# 00: Test machine snapshot

- **Result:** ✅ recorded
- **Date:** 2026-11-23
- **How to run:** `scripts/sysinfo.sh tools/spikes/00-sysinfo/sysinfo-<date>.txt`
- **Full output:** [sysinfo-2026-11-23.txt](sysinfo-2026-11-23.txt)

## Summary

| Item | Value |
|---|---|
| Kernel / session | 7.3.5-arch1-1, KDE Plasma on Wayland |
| CPU / RAM | Ryzen 9 9950X, 30 GiB |
| GPU / driver | RTX 5080 16 GB, `nvidia-open-dkms` 620.73.04 |
| CUDA | 13.5 |
| Vulkan | 1.4.357; all 18 extensions the plan needs are present |
| NGX | `libnvidia-ngx.so.620.73.04`, `libnvidia-ngx-builtin-dlssd.so.310.9.0`; Wine: `nvngx_dlssg.dll` |
| Monitor | DP-6, 2560x1440 @ 165 Hz, HDR disabled |
| Proton | Experimental 0.9.3, GE-Proton11-12 0.9.3, CachyOS 11.0 0.9.2, Proton 11.0 0.9.2, GE-Proton10-34 0.9.1, Proton 10.0 0.9.0 (numbers are dxvk-nvapi versions) |
| Capture card | none |

## What this means for PLAN.md

Baseline confirmed. Re-run before each test session; if the driver changes, note it in each test's NOTES.md.
