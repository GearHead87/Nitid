# S7: How evenly do frames reach the screen (KDE Wayland, NVIDIA, 165 Hz)?

- **Result:** ✅ pass for FIFO: **6.061 ms ± 0.027 ms** between displayed frames, 0 missed refreshes
  (target was ±0.5 ms). ⚠️ `VK_EXT_present_timing` reports timing but **cannot schedule presents at a
  given time** on this setup.
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, KDE Plasma Wayland, SDL 3.4.20 (Wayland backend), monitor DP-6
  2560x1440 @ 165 Hz (no VRR test)
- **How to run:** build spikes, then `tools/spikes/s7-present-timing/run.sh` (opens a 960x540 window
  for ~16 s)
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## Surface capabilities (windowed, Wayland)

| Item | Value |
|---|---|
| Present modes | MAILBOX, FIFO_LATEST_READY, FIFO, IMMEDIATE |
| `VK_EXT_present_timing` | supported = 1, **present at absolute time = 0, relative time = 0**, stage queries = 0x5 |
| `presentId` / `presentWait` | 1 / 1 |

## Displayed-frame intervals (one present queued ahead, ~4 s per mode)

| Present mode | Intervals | Mean (ms) | Std dev (ms) | p1 / p99 (ms) | > 1.5x refresh | Effective rate |
|---|---|---|---|---|---|---|
| **FIFO** | 630 | **6.061** | **0.027** | 6.00 / 6.13 | **0** | **165.0 Hz** |
| MAILBOX | 7020 | 0.569 | 1.472 | 0.07 / 6.59 | 1 | (frames replaced) |
| FIFO_LATEST_READY | 6627 | 0.599 | 1.506 | 0.08 / 6.63 | 0 | (frames replaced) |
| IMMEDIATE | 3748 | 1.063 | 2.516 | 0.08 / 11.02 | 100 | (no tearing on Wayland; erratic) |

Refresh interval: 6.061 ms.

## Surprises / gotchas

- With MAILBOX / FIFO_LATEST_READY the app renders far faster than the display and frames are replaced;
  `vkWaitForPresentKHR` then completes for frames that were never shown, so the "intervals" are not
  display intervals. FIFO is the mode that matches real display pacing.
- `VK_EXT_present_timing` can't target a time (absolute/relative = 0), so Nitid can't say "show this
  generated frame at t". Pacing must come from FIFO (one frame per refresh) plus the app choosing *which*
  frame to submit for each refresh, or from VRR.
- Windowed only; fullscreen/direct scanout and VRR (adaptive sync) were not tested.

## What this means for PLAN.md

- Display pacing on KDE + NVIDIA is rock-steady with FIFO at the native refresh rate.
- For frame generation, aim the output at the **refresh rate** (165 Hz here) and pick for each vblank the
  real/generated frame closest in time (FIFO + present-wait feedback). Non-integer ratios (e.g. 60 → 165)
  then need timestamp-based frame selection, or VRR. Test VRR/fullscreen in Phase 1–3.
