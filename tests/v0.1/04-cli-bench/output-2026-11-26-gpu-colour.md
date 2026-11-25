# 04-cli-bench output, 2026-11-26, rerun after GPU colour conversion (05)

Same script, same machine; decoding now uses NVDEC + GPU colour conversion.

### A. Upscaling to 1080p (whole 10 s clip vs the real 1080p video)

| Source / method | PSNR | SSIM | VMAF |
|---|---|---|---|
| lq_540p, plain bicubic | 40.390883 | 0.976269 | 82.428006 |
| lq_540p, **nitid VSR high** | 42.613141 | 0.972405 | 90.073681 |
| lq_540p, **nitid VSR ultra** | 42.437100 | 0.969045 | 91.249657 |
| lq_720p, plain bicubic | 42.266753 | 0.982388 | 88.638879 |
| lq_720p, **nitid VSR high** | 41.420727 | 0.977916 | 91.649355 |
| lq_720p, **nitid VSR ultra** | 41.058197 | 0.975882 | 92.545515 |
| vlq_540p, plain bicubic | 36.600186 | 0.943014 | 55.171866 |
| vlq_540p, **nitid VSR high** | 37.443903 | 0.940091 | 62.866711 |
| vlq_540p, **nitid VSR ultra** | 37.485267 | 0.937669 | 64.843674 |

### B. Frame generation: rebuild every 2nd frame (all frames vs the original)

| Clip / method | PSNR | SSIM | VMAF |
|---|---|---|---|
| src_1080p, repeat each kept frame | 26.714858 | 0.945405 | 70.301582 |
| src_1080p, FFmpeg blend | 27.998487 | 0.952663 | 71.159788 |
| src_1080p, **nitid frame gen MEDIUM, scene cuts on** (6 cuts) | 29.446382 | 0.979056 | 82.556945 |
| src_1080p, nitid frame gen MEDIUM, scene cuts off | 29.406521 | 0.978488 | 82.261838 |
| sintel_trailer-1080p, repeat each kept frame | 31.172355 | 0.976251 | 83.107091 |
| sintel_trailer-1080p, FFmpeg blend | 33.086386 | 0.979975 | 83.422412 |
| sintel_trailer-1080p, **nitid frame gen MEDIUM, scene cuts on** (8 cuts) | 34.482909 | 0.989618 | 89.541616 |
| sintel_trailer-1080p, nitid frame gen MEDIUM, scene cuts off | 33.981950 | 0.989028 | 89.467751 |

### C. Speed (no file written; throughput after the first frame)

| Input | Settings | Output | Throughput (fps) | x real time | Decode+convert ms | Upscale ms | Frame gen ms |
|---|---|---|---|---|---|---|---|
| src_1080p.mp4 | nothing (decode only) | 1920x1080 @ 24.000 | 2324.8 | 96.87 | 0.33 | 0.00 | 0.00 |
| lq_540p.mp4 | VSR HIGH to 1080p | 1920x1080 @ 24.000 | 1294.3 | 53.93 | 0.16 | 0.76 | 0.00 |
| src_1080p.mp4 | VSR HIGH to 1440p | 2560x1440 @ 24.000 | 360.1 | 15.01 | 0.86 | 2.74 | 0.00 |
| src_1080p.mp4 | VSR HIGH to 4K | 3840x2160 @ 24.000 | 353.6 | 14.73 | 0.78 | 2.85 | 0.00 |
| src_1080p.mp4 | frame gen MEDIUM 2x | 1920x1080 @ 48.000 | 1427.8 | 29.75 | 0.38 | 0.00 | 1.38 |
| src_1080p.mp4 | VSR HIGH 1440p + frame gen MEDIUM 4x | 2560x1440 @ 96.000 | 572.0 | 5.96 | 0.87 | 2.75 | 1.46 |
| src_4k_hevc.mkv | nothing (decode only, 4K) | 3840x2160 @ 60.000 | 661.7 | 11.03 | 1.46 | 0.00 | 0.00 |
