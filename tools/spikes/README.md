# Spikes

Small, standalone test programs and scripts that answer one question each: **does this part of
the plan work on the target Linux setup?** The full plan is in [TESTPLAN.md](TESTPLAN.md).

## Results

| ID | Question | Result | Driver | Proton | Date | Notes |
|---|---|---|---|---|---|---|
| G1 | DLSS SR + Frame Gen 2x work in a Proton game? | ⏳ | 620.73.04 | Experimental (0.9.3) | – | [g1-dlss-fg-proton](g1-dlss-fg-proton/NOTES.md): run by user, result not reported yet |
| G2 | Multi Frame Gen 4x override works? Which Proton builds? | ⏳ | 620.73.04 | Exp / GE11-12 / CachyOS / 11.0 | – | [g2-mfg-override](g2-mfg-override/NOTES.md): run by user, result not reported yet |
| G3 | Newest DLSS preset override works? | ⏳ | 620.73.04 | Experimental | – | [g3-dlss-preset-override](g3-dlss-preset-override/NOTES.md): run by user, result not reported yet |
| G4 | Smooth Motion works on a game without DLSS? | ⏳ | 620.73.04 | Experimental | – | [g4-smooth-motion](g4-smooth-motion/NOTES.md): run by user, result not reported yet |
| G5 | Frame gen lag acceptable? (optional) | ⏳ | 620.73.04 | – | – | [g5-fg-latency-feel](g5-fg-latency-feel/NOTES.md): run by user, result not reported yet |
| M1 | NVENC H.264 / HEVC / AV1 faster than real time at 4K60? | ✅ | 620.73.04 | – | 2026-11-23 | [m1-nvenc](m1-nvenc/NOTES.md): HEVC/AV1 p5 ~135 fps, H.264 p5 80 fps |
| M2 | Vulkan and CUDA decoding ≥ 120 fps at 4K? | ✅ | 620.73.04 | – | 2026-11-23 | [m2-decode](m2-decode/NOTES.md): Vulkan ~380–405 fps, NVDEC ~460–520 fps |
| M3 | HDR output on KDE Wayland + NVIDIA? | ⏭️ | 620.73.04 | – | 2026-11-23 | [m3-hdr-output](m3-hdr-output/NOTES.md): skipped for now |
| M4 | PipeWire screen capture works? | ⚠️ | 620.73.04 | – | 2026-11-23 | [m4-screen-capture](m4-screen-capture/NOTES.md): portal v6 ready (monitor/window/virtual); real capture needs user approval |
| M5 | v4l2loopback virtual camera works? | ⏳ | 620.73.04 | – | 2026-11-23 | [m5-virtual-camera](m5-virtual-camera/NOTES.md): script ready; needs user to run sudo modprobe first |
| M6 | Capture card formats and live video? | ⏭️ | 620.73.04 | – | – | [m6-capture-card](m6-capture-card/NOTES.md): skipped, no capture card |
| V1 | VFX VSR runs; 1080p→4K ≥ 60 fps? | ✅ | 620.73.04 | – | 2026-11-23 | [v1-vfx-vsr](v1-vfx-vsr/NOTES.md): 1080p→4K HIGH 2.9 ms/frame (344 fps) GPU-only; all modes real-time |
| V2 | VFX denoise / deblur visibly better? | ⚠️ | 620.73.04 | – | 2026-11-23 | [v2-vfx-denoise-deblur](v2-vfx-denoise-deblur/NOTES.md): fast (1–5.6 ms at 1080p) but no measurable cleanup; denoise softens blocks slightly, deblur worsens them |
| V3 | VFX frame generation 2x / 4x works in real time? | ✅ | 620.73.04 | – | 2026-11-23 | [v3-vfx-frame-gen](v3-vfx-frame-gen/NOTES.md): 1440p MEDIUM 1.7 ms/frame; beats blending by 2–4 dB; default shot-change detection misfires on fast motion (turn off) |
| V4 | VFX TrueHDR output correct in HDR? | ⚠️ | 620.73.04 | – | 2026-11-23 | [v4-vfx-truehdr](v4-vfx-truehdr/NOTES.md): works, 1.1 ms at 4K, output tagged HDR10 (no static metadata); HDR viewing skipped |
| V5 | VFX VSR quality vs bicubic? | ✅ | 620.73.04 | – | 2026-11-23 | [v5-vsr-quality](v5-vsr-quality/NOTES.md): 540p→1080p VSR ULTRA +1.4 dB / +7 VMAF over Lanczos; reduces blocking; STREAMING_ULTRA over-sharpens |
| S1 | NGX reports DLSS SR and Frame Gen available to our own Vulkan app? | ✅ | 620.73.04 | – | 2026-11-23 | [s1-ngx-caps](s1-ngx-caps/NOTES.md): DLSS SR + DLSS-G available natively; MultiFrameCountMax = 5 (6x); raw-NGX VSR not implemented on Linux |
| S2 | DLSS SR upscales an image in our own app? | ✅ | 620.73.04 | – | 2026-11-23 | [s2-dlss-sr-image](s2-dlss-sr-image/NOTES.md): DLSS SR native, 0.44–0.62 ms to 1080p; jittered 540p→1080p +6.5 dB over Lanczos (ideal case); jitter sign must be negated |
| S3 | **DLSS-G generates frames outside a game (Veyra's approach)?** | ✅ | 620.73.04 | – | 2026-11-23 | [s3-dlssg-standalone](s3-dlssg-standalone/NOTES.md): DLSS-G works natively at 2x/4x/6x (~1 ms at 1080p); with zero MVs ≈ VFX LOW/MEDIUM, below VFX HIGH; lite mode = blend without MVs |
| S3b | Veyra's DLSS-G on video works under Proton? (only if S3 fails) | ⏭️ | 620.73.04 | GE11-12 / Exp | 2026-11-23 | [s3b-veyra-proton](s3b-veyra-proton/NOTES.md): not needed, S3 passed |
| S4 | `VK_NV_optical_flow` gives sensible motion < 2 ms at 1080p? | ✅ | 620.73.04 | – | 2026-11-23 | [s4-nvof-pair](s4-nvof-pair/NOTES.md): sub-pixel accurate; warp +10.7 dB on Sintel; 1080p 4x4 grid 0.54–0.75 ms |
| S5 | Vulkan ↔ CUDA ↔ VFX VSR with no CPU copy? | – | 620.73.04 | – | – | |
| S6 | PipeWire screen frames imported into Vulkan via DMA-BUF? | – | 620.73.04 | – | – | |
| H1 | HDR10 swapchain offered and shown correctly? | ⏭️ | 620.73.04 | – | – | [h1-hdr-swapchain](h1-hdr-swapchain/NOTES.md): skipped for now (HDR postponed) |
| S7 | Frame timing within ±0.5 ms with present timing? | – | 620.73.04 | – | – | |
| S8 | Capture card frames into Vulkan (direct or upload)? | ⏭️ | 620.73.04 | – | – | [s8-v4l2-import](s8-v4l2-import/NOTES.md): skipped, no capture card |
| F1 | Optical flow + DLSS-G on a real clip looks good vs VFX frame gen? | – | 620.73.04 | – | – | |

Result values: ✅ pass · ❌ fail · ⚠️ partial · ⏳ waiting for the user's report · ⏭️ skipped · – not run yet.

## Layout

```
tools/spikes/
  README.md                 this index and the results table
  TESTPLAN.md               the full test plan
  CMakeLists.txt            builds only the spikes (separate from the main build)
  common/                   small shared helpers (Vulkan setup, saving PNGs)
  00-vfx-python/            VFX SDK test in Python
  01-ngx-caps/              each spike: main.cpp, CMakeLists.txt, NOTES.md
  02-dlss-sr-image/
  03-dlssg-standalone/
  04-nvof-pair/
  05-cuda-vk-vsr/
  06-pipewire-dmabuf/
  07-hdr-swapchain/
  08-present-timing/
  09-v4l2-import/
third_party/                NVIDIA SDKs, downloaded by scripts/fetch-sdks.sh (git-ignored)
results/                    test output: logs, PNGs, videos (git-ignored)
scripts/                    sysinfo.sh, fetch-sdks.sh, fetch-media.sh
```

## Rules

1. **Each spike answers one question** and records the answer in its `NOTES.md`: pass or fail,
   the driver version, the exact output and anything surprising. Then update the table above.
2. **Spikes don't use `libnitid`, and the engine doesn't use spikes.** They are built separately
   (`cmake -S tools/spikes`), so the main build never needs the NVIDIA SDKs because of a spike.
3. **Don't move spike code straight into the engine.** When a spike works, rewrite that piece
   properly in `libnitid` and keep the spike as the reference.
4. **Once a spike has its answer, stop maintaining it.** It's a record. If it breaks later, that's fine.
5. **Never commit the NVIDIA SDKs.** They are large and their licenses limit sharing them.
6. **No Veyra code in this repo until the license is decided.** Refer to Veyra by link and commit.

## `NOTES.md` template

```markdown
# <ID>: <question>

- **Result:** ✅ / ❌ / ⚠️
- **Date:**
- **Driver / Proton / SDK versions:**
- **How to run:**
- **Output:** (paste the important lines)
- **Timings:**
- **Surprises / gotchas:**
- **What this means for PLAN.md:**
```
