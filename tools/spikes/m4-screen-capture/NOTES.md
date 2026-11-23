# M4: Does PipeWire screen capture work?

- **Result:** ⚠️ partial: the portal is ready; the actual capture needs the user (KDE permission dialog)
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

## Still to do (user)

- [ ] OBS → add "Screen Capture (PipeWire)" → approve the KDE dialog → picture appears.

Spike **S6** (`s6-pipewire-dmabuf`) also needs this dialog once; after that a restore token
lets it run without asking.

## What this means for PLAN.md

The portal offers everything Nitid needs (monitor and window capture, cursor as metadata so it
can be drawn after enhancement).
