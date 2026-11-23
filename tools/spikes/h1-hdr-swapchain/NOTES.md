# H1: Is an HDR10 swapchain offered and shown correctly (KDE Wayland + NVIDIA)?

- **Result:** ⏭️ skipped for now (user decision 2026-11-23: HDR testing postponed; see M3)
- **Known so far (00-sysinfo):** `VK_EXT_swapchain_colorspace` and `VK_EXT_hdr_metadata` are present;
  KDE reports `HDR: disabled` on DP-6 (2560x1440 @ 165 Hz), not "incapable".
- **Plan when resumed:** turn on HDR in KDE, then an SDL3 + Vulkan window lists the surface formats /
  colour spaces (looking for `A2B10G10R10_UNORM` + `HDR10_ST2084_EXT`), shows a PQ gradient with
  `VkHdrMetadataEXT`, and the user judges it by eye. S7's code (`s7-present-timing`) already has the
  SDL3/Vulkan swapchain setup to start from.
