# S5: Vulkan → CUDA → VFX VSR → Vulkan with no CPU copy?

- **Result:** ✅ pass: frames stay on the GPU, output is **bit-identical** to NVIDIA's own Python VSR, and
  the whole round trip costs only ~0.2–0.4 ms more than VSR alone
- **Date:** 2026-11-23
- **Versions:** driver 620.73.04, CUDA 13.5 driver API, VFX SDK 1.3.0 (runtime from the `nvidia-vfx`
  wheel, C headers from `NVIDIA-Maxine/Maxine-VFX-SDK`, MIT), RTX 5080
- **How to run:** `scripts/fetch-sdks.sh` (adds the VFX headers), `tools/spikes/vfx-env/setup.sh`,
  build spikes, then `tools/spikes/s5-cuda-vk-vsr/run.sh`
- **Full output:** [output-2026-11-23.md](output-2026-11-23.md)

## What it does (per frame, 200 frames after 10 warm-up)

1. Vulkan: copy the low-res frame (a `VkImage`) into an **exportable** `VkBuffer`; signal semaphore A.
2. CUDA (driver API): wait on A, `NvVFX_Run(VideoSuperRes)` reading that buffer and writing a second
   exportable buffer; signal semaphore B.
3. Vulkan: wait on B, copy the output buffer into the high-res `VkImage`.

Memory and semaphores are shared as opaque file descriptors (`VK_KHR_external_memory_fd`,
`VK_KHR_external_semaphore_fd`, `cuImportExternalMemory`, `cuImportExternalSemaphore`). The CUDA device
is chosen by matching the Vulkan device UUID. The VFX effect takes `NvCVImage` RGBA8 "chunky" GPU images
wrapped around the shared pointers (`NvCVImage_Init(..., NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_GPU)`).

## Results

| Case | Quality | Full round trip, median / p90 (ms) | V1 VSR alone (ms) | Output vs NVIDIA Python VSR |
|---|---|---|---|---|
| 540p → 1080p | HIGH | **0.97** / 0.99 | 0.77 | identical (PSNR ∞) |
| 540p → 1080p | ULTRA | 1.18 / 1.19 | 0.98 | identical |
| 1080p → 2160p | HIGH | **3.33** / 3.98 | 2.91 | identical |
| 1080p → 2160p | ULTRA | 4.25 / 4.86 | 3.84 | identical |

## Surprises / gotchas

- **Library path:** `libVideoFX.so` needs its sibling NPP/cuDNN libraries (`nvvfx/libs`) on
  `LD_LIBRARY_PATH`; NVIDIA's Python loader preloads them. An executable's RUNPATH doesn't cover them.
- **CUDA-event timing around `NvVFX_Run` is not reliable** (it reported 0.07 ms): the SDK seems to do its
  work on internal streams and sync with ours. Use the end-to-end round trip instead.
- The Python wrapper's "RGB8" encoding (float tensors) is only a wrapper convenience: the C API takes
  plain RGBA8 GPU images directly (`ImageEncodingMode = 0`).
- **Later finding (2026-11-26, Nitid 0.1):** this spike uses a `CU_STREAM_NON_BLOCKING` stream. The VFX
  SDK does part of its work on CUDA's legacy default stream, so with a non-blocking stream the result is not
  ordered with our work: on a *video* (changing input) VSR returned the previous frame. This spike feeds the
  same frame every iteration, which hides it. Use `CU_STREAM_DEFAULT` (see S9 and tests/v0.1/02-upscaling).
- No CUDA runtime is linked by us: the driver API (`libcuda`) plus the wheel's bundled `libcudart` 12.9
  coexist fine on the primary context.

## What this means for PLAN.md

The CUDA ↔ Vulkan interop design is confirmed: VFX effects can sit inside a Vulkan pipeline with only
GPU copies and semaphores. For Nitid, allocate the VSR input/output as exportable buffers (or tiled
images later) once, and keep the frame on the GPU from decode to display.
