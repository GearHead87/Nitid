# M4: Does PipeWire screen capture work?

- **Result:** ✅ pass: portal ready (automatic check) and real capture confirmed by S6 (2026-11-23):
  KDE dialog approved, 120/120 frames received over PipeWire (DMA-BUF, 2560x1440)
- **Date:** 2026-11-23
- **How to run:** `tools/spikes/m4-screen-capture/run.sh` (automatic part), then OBS →
  "Screen Capture (PipeWire)" (needs a person to approve the dialog).

## Output (automatic part)

```
ScreenCast version:        u 6
AvailableSourceTypes:      u 7   (bitmask: 1 monitor, 2 window, 4 virtual)
AvailableCursorModes:      u 7   (bitmask: 1 hidden, 2 embedded, 4 metadata)
Portal packages:           xdg-desktop-portal 1.22.1-2 xdg-desktop-portal-kde 6.8.3-1 
PipeWire:                  1.6.11
Services:                  active active active 
```

- ScreenCast portal **version 6**; sources: **monitor, window and virtual** (7); cursor:
  **hidden, embedded and metadata** (7).

## Real capture (done via S6)

[S6](../s6-pipewire-dmabuf/NOTES.md) opened a ScreenCast session, the KDE dialog was approved, and
frames arrived over PipeWire as DMA-BUF and imported into Vulkan. A restore token now lets it run
without asking. Checking in OBS is optional.

## What this means for PLAN.md

The portal offers everything Nitid needs (monitor and window capture, cursor as metadata so it
can be drawn after enhancement).
