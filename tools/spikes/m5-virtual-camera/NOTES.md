# M5: Does a v4l2loopback virtual camera work?

- **Result:** ⏳ waiting for the user: loading the kernel module needs `sudo`
- **Date:** 2026-11-23
- **Known so far:** `v4l2loopback-dkms` 0.15.4 and `v4l2loopback-utils` are installed; the module is
  not loaded (`lsmod` shows nothing).
- **How to run:**
  1. (user, once) `sudo modprobe v4l2loopback devices=1 video_nr=42 card_label="Nitid Virtual Camera" exclusive_caps=1`
  2. `tools/spikes/m5-virtual-camera/run.sh` writes a 1080p60 test pattern into `/dev/video42`,
     reads it back with a second ffmpeg (like OBS / Discord would), and saves a frame to
     `results/m5-virtual-camera/received_frame.png`.
  3. Optional: open "Nitid Virtual Camera" in OBS while the script runs.
- **Pass if:** the reader gets ~180 frames of 1920x1080 in 3 s and the saved frame shows the test pattern.

## Output

(not run yet)

## What this means for PLAN.md

Nitid's virtual camera needs this module loaded; the packaged app should document the
`modprobe` line (or ship a `modprobe.d` / `modules-load.d` snippet).
