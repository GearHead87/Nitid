# Third-party software in Nitid releases

The Linux release tarball includes, or is built with, the following software. Their license texts are in
`share/doc/nitid/licenses/`.

| Component | Version | License | How it is used |
|---|---|---|---|
| [FFmpeg](https://ffmpeg.org) (libavformat, libavcodec, libavutil, libswscale, libswresample) | 9.1 | LGPL 2.1 or later | Bundled as unmodified shared libraries in `lib/`, built with `--disable-gpl --disable-nonfree` |
| [SDL](https://libsdl.org) | 3.4.20 | zlib | Bundled as a shared library in `lib/` |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.93.0 | MIT | Compiled into `nitid` (on-screen controls) |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.7.2 | BSD 3-Clause | Header-only, compiled into the programs |
| [nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) | 12.2.72.0 | MIT | Headers compiled into FFmpeg (loads NVIDIA's decoder from the driver) |
| [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | 1.0 (2021-06-29) | MIT | EASU and RCAS shader code ported to CUDA, compiled into libnitid |
| [NVIDIA Image Scaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling) | 1.0.3 | MIT | `NIS_Config.h` compiled into libnitid; the NVScaler shader ported to CUDA |
| [NVIDIA Maxine VFX SDK headers](https://github.com/NVIDIA-Maxine/Maxine-VFX-SDK) | see `packaging/linux/versions.env` | MIT | C headers compiled into libnitid |
| [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) (headers + `libnvsdk_ngx.a`) | 310.9.16 | NVIDIA RTX SDKs license (`licenses/DLSS-SDK/LICENSE.txt`) | Compiled into the separate DLSS module `lib/libnitid-dlss.so` only, which libnitid loads at run time; not part of libnitid |

**FFmpeg source:** the bundled libraries are built unchanged from
<https://ffmpeg.org/releases/ffmpeg-9.1.tar.xz> with the configure options in
`packaging/linux/build-deps.sh` of the Nitid source for this release. You may replace them with your own build
of a compatible FFmpeg version.

**Not included: the NVIDIA VFX runtime.** Nitid's AI upscaling and frame generation use NVIDIA's VFX runtime
(the `nvidia-vfx` package), which is NVIDIA software under NVIDIA's license. Nitid does not ship it; the
`nitid-setup-vfx` script downloads it from NVIDIA (`pypi.nvidia.com`) after you accept that license.

**Not included: the NVIDIA DLSS runtime** (`libnvidia-ngx-dlss.so`, `libnvidia-ngx-dlssg.so`). Nitid's DLSS
upscaling and DLSS frame generation load NVIDIA's DLSS runtime libraries, NVIDIA software under the NVIDIA RTX SDKs
license. The `nitid-setup-dlss` script downloads them from NVIDIA's DLSS SDK repository on GitHub after you accept
that license. Nitid is not sponsored or endorsed by NVIDIA.
