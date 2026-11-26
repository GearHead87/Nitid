# Third-party software in Nitid releases

The Linux release tarball includes, or is built with, the following software. Their license texts are in
`share/doc/nitid/licenses/`.

| Component | Version | License | How it is used |
|---|---|---|---|
| [FFmpeg](https://ffmpeg.org) (libavformat, libavcodec, libavutil, libswscale, libswresample) | 9.1 | LGPL 2.1 or later | Bundled as unmodified shared libraries in `lib/`, built with `--disable-gpl --disable-nonfree` |
| [SDL](https://libsdl.org) | 3.4.20 | zlib | Bundled as a shared library in `lib/` |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.7.2 | BSD 3-Clause | Header-only, compiled into the programs |
| [nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) | 12.2.72.0 | MIT | Headers compiled into FFmpeg (loads NVIDIA's decoder from the driver) |
| [NVIDIA Maxine VFX SDK headers](https://github.com/NVIDIA-Maxine/Maxine-VFX-SDK) | see `packaging/linux/versions.env` | MIT | C headers compiled into libnitid |

**FFmpeg source:** the bundled libraries are built unchanged from
<https://ffmpeg.org/releases/ffmpeg-9.1.tar.xz> with the configure options in
`packaging/linux/build-deps.sh` of the Nitid source for this release. You may replace them with your own build
of a compatible FFmpeg version.

**Not included: the NVIDIA VFX runtime.** Nitid's AI upscaling and frame generation use NVIDIA's VFX runtime
(the `nvidia-vfx` package), which is NVIDIA software under NVIDIA's license. Nitid does not ship it; the
`nitid-setup-vfx` script downloads it from NVIDIA (`pypi.nvidia.com`) after you accept that license.
