# S8: Can capture-card frames get into Vulkan (directly or by upload)?

- **Result:** ⏭️ skipped: no capture card (user, 2026-11-23); see M6
- **Plan when a card is available:** V4L2 `VIDIOC_EXPBUF` → DMA-BUF → Vulkan import (same import code as
  S6), with a CPU-upload fallback. Note: many USB (UVC) cards use vmalloc buffers that can't be exported
  as DMA-BUF; a 1080p60 upload (~500 MB/s) is fine as a fallback.
