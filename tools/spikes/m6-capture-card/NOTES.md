# M6: Capture card formats and live video?

- **Result:** ⏭️ skipped: no capture card (user, 2026-11-23)
- **How to run later:** `v4l2-ctl --list-formats-ext -d /dev/videoN`, then `mpv av://v4l2:/dev/videoN`.
- **Pass if:** formats are listed and live video shows; note format, resolution and frame rate.

## What this means for PLAN.md

Phase 3 capture-card input stays untested; S8 is skipped too. Screen capture (M4/S6) and files
remain the live inputs for now.
