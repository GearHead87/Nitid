#!/usr/bin/env bash
# Print a snapshot of the test machine: driver, kernel, GPU, Vulkan features,
# Proton / dxvk-nvapi versions, display, and relevant packages.
# Usage: scripts/sysinfo.sh [output-file]   (default: stdout)
set -uo pipefail

out=${1:-/dev/stdout}

{
echo "# sysinfo $(date -Iseconds)"
echo
echo "## System"
echo "kernel: $(uname -r)"
echo "cpu: $(lscpu | sed -n 's/^Model name: *//p')"
echo "ram: $(free -h | awk '/Mem/{print $2}')"
echo "session: ${XDG_SESSION_TYPE:-?} / ${XDG_CURRENT_DESKTOP:-?}"
echo
echo "## GPU / driver"
nvidia-smi --query-gpu=name,driver_version,memory.total,pci.bus_id --format=csv,noheader 2>/dev/null || echo "nvidia-smi: unavailable"
pacman -Q nvidia-open-dkms nvidia-utils cuda 2>/dev/null
echo
echo "## Vulkan"
vk=$(vulkaninfo 2>/dev/null)
grep -m1 -E 'apiVersion' <<<"$vk" | sed 's/^[[:space:]]*/api: /'
for e in VK_NV_optical_flow VK_KHR_external_memory_fd VK_KHR_external_semaphore_fd \
         VK_EXT_external_memory_dma_buf VK_EXT_image_drm_format_modifier \
         VK_KHR_video_decode_queue VK_KHR_video_decode_h264 VK_KHR_video_decode_h265 VK_KHR_video_decode_av1 \
         VK_KHR_present_wait VK_KHR_present_id VK_EXT_present_timing VK_EXT_swapchain_maintenance1 \
         VK_NV_low_latency2 VK_EXT_hdr_metadata VK_EXT_swapchain_colorspace VK_KHR_wayland_surface \
         VK_KHR_timeline_semaphore; do
  printf '%-34s %s\n' "$e" "$(grep -q "$e" <<<"$vk" && echo yes || echo NO)"
done
echo
echo "## NGX libraries"
ls /usr/lib/libnvidia-ngx* 2>/dev/null
ls /usr/lib/nvidia/wine/ 2>/dev/null | sed 's/^/wine: /'
echo
echo "## Proton (dxvk-nvapi)"
steam=~/.steam/steam
for d in "$steam"/compatibilitytools.d/* "$steam"/steamapps/common/Proton*; do
  [ -d "$d" ] || continue
  nv=$(find "$d" -maxdepth 6 -path '*nvapi*' -name version 2>/dev/null | head -1)
  ver=$([ -n "$nv" ] && grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' "$nv" | head -1)
  printf '%-40s dxvk-nvapi=%s\n' "$(basename "$d")" "${ver:-?}"
done
echo
echo "## Display"
kscreen-doctor -o 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -E 'Output:|HDR:|Modes:' | head -10
echo
echo "## Packages"
pacman -Q ffmpeg mpv obs-studio-tytan652 pipewire wireplumber xdg-desktop-portal-kde libportal \
  v4l2loopback-dkms vulkan-headers volk vulkan-memory-allocator directx-shader-compiler \
  vulkan-validation-layers sdl3 spdlog cli11 tomlplusplus stb uv git-lfs 2>/dev/null
} >"$out"
