# S6: Screen capture on KDE Wayland via PipeWire, imported into Vulkan as DMA-BUF?

- **Result:** ✅ pass: **120/120 frames arrived as DMA-BUF and were imported into Vulkan** (0 failures,
  0 shared-memory fallbacks), 2560x1440, NVIDIA tiled modifier, ~0.4–0.6 ms per import. The portal's
  restore token works: after one approval, later runs need no dialog.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, KDE Plasma (xdg-desktop-portal 1.22.1, -kde 6.8.3), PipeWire 1.6.11,
  libportal 0.11.0, RTX 5080, monitor 2560x1440 @ 165 Hz
- **How to run:** build spikes, then `tools/spikes/s6-pipewire-dmabuf/run.sh` (first run: approve the
  KDE dialog)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## What it does

1. Vulkan device with `VK_EXT_external_memory_dma_buf`, `VK_EXT_image_drm_format_modifier`,
   `VK_KHR_external_memory_fd`, `VK_EXT_queue_family_foreign`; lists the DRM modifiers Vulkan can import
   for `B8G8R8A8_UNORM` (7: six NVIDIA block-linear `0x03000000006060xx` + LINEAR).
2. libportal ScreenCast session (monitor, cursor embedded, **persistent** → restore token saved).
3. PipeWire stream offering BGRx/BGRA **with those modifiers** (plus a no-modifier shared-memory fallback);
   fixates the modifier itself when PipeWire asks (`SPA_VIDEO_FLAG_MODIFIER_FIXATION_REQUIRED`).
4. Each DMA-BUF frame → `VkImage` (`VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, explicit plane layout,
   `VkImportMemoryFdInfoKHR`, dedicated allocation), acquired from `VK_QUEUE_FAMILY_FOREIGN_EXT`; the first
   frame is copied out and saved for checking.

## Results

| Item | Run 1 (dialog approved) | Run 2 (restore token) |
|---|---|---|
| Portal | ok (node 80) | ok, **no dialog** (node 105) |
| Negotiated format | BGRx 2560x1440, variable rate (0/1) | same |
| Modifier | `0x0300000000606015` (NVIDIA block-linear) | same |
| Frames received / DMA-BUF / shared memory | 60 / 60 / 0 | 120 / 120 / 0 |
| Imported into Vulkan / failed | 60 / 0 | 120 / 0 |
| Import time (create image + import + bind) | 0.60 ms | 0.42 ms |
| Frame rate received | 35.4 fps | 44.8 fps |
| Saved frame | valid (luma 16–235, mean 45.6) | valid |

The saved frame is a screenshot of the desktop, so it is **not committed**; only its statistics were checked.

## Surprises / gotchas

- **Modifier fixation:** KWin offers several modifiers with `DONT_FIXATE`; the consumer must pick one and
  re-announce it. Re-announcing it *as a choice* made PipeWire ask again 11 times; announcing a single plain
  value settles it at once (fixed in the code).
- **Frame rate follows screen activity:** KWin sends frames only when something changes (35–45 fps on a
  mostly idle desktop, framerate 0/1 = variable). A game in motion should deliver up to the refresh rate.
- No explicit sync used (no `SPA_META_SyncTimeline`); frames looked complete, but a real pipeline should
  honour the compositor's sync to avoid tearing.
- Per-frame import can be cached per PipeWire buffer (usually 4–8 buffers are recycled).

## What this means for PLAN.md

Screen capture for Phase 3 is confirmed with zero-copy DMA-BUF into Vulkan on KDE + NVIDIA. Use a
persistent portal session (restore token) so users approve only once. This also completes M4.
