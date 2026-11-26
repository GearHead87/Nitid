# 0.4 test 02: screen capture through the desktop portal

**Question:** does Nitid capture the real desktop (KDE Plasma, Wayland, NVIDIA) through the portal, with the
compositor's frames arriving as DMA-BUF, at the display rate and with low latency?

**Run:** `tests/v0.4/02-screen-capture/run.sh`. Output: [output-2026-11-27.md](output-2026-11-27.md).
**Privacy:** the test records numbers only; no captured image is saved, shown or committed (no `--capture-at`).

## Result: ✅

| Mode | Format | Frames in | Capture → screen | Copy + convert | Display |
|---|---|---|---|---|---|
| Low latency | BGRx 2560x1440, **DMA-BUF** (NVIDIA tiled modifier) | 165 fps | **21.7–22.1 ms** | 0.33 ms | 165 fps, 0–2 missed refreshes |
| Smooth | same | 165 fps | 27.7–27.9 ms | 0.32 ms | 165 fps, 0–2 missed |

- **Portal:** `org.freedesktop.portal.ScreenCast` over D-Bus (libdbus loaded at run time). With a remembered
  choice (restore token, persist mode "until revoked") the desktop shows **no dialog**; the run used spike S6's
  saved choice (same unsandboxed host app), and the token stayed valid.
- **Zero-copy from the compositor:** every frame came as DMA-BUF and was imported into Vulkan (cached per buffer),
  copied on Nitid's second Vulkan queue to a buffer CUDA reads, and converted to RGBA: 0.33 ms per 1440p frame.
- **Latency:** 22 ms from a frame arriving to it being on screen in low-latency mode; ~19 ms of that is the
  window's own present path (two presents queued, as for video). Smooth mode adds a frame interval (6 ms here).
- Smooth mode generated few frames here because the capture already arrives at 165 fps (Nitid's window was on the
  captured screen, so the desktop changed every frame). It matters for 30 / 60 fps sources (test 01: 30 → 165 fps).

## Use

`nitid --capture-screen` (or "Capture the screen..." on the open screen): the desktop asks which screen or window;
Nitid remembers the choice in `$XDG_STATE_HOME/nitid/screencast-token`. `L` / Settings → Screen capture switches
low latency / smooth. Put Nitid's window on another screen than the one it captures (or capture a single window),
otherwise it shows itself.

## Limits

- No capture-card input (no device to test); no sound capture.
- The ~19 ms present path could be shortened for live input with one present queued instead of two, but that
  locked into 2/3 of the refresh rate on KDE (0.1 test 01); left as is.
