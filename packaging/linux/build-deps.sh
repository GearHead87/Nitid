#!/usr/bin/env bash
# Builds the libraries a Nitid release bundles, from source, into $DEPS_PREFIX:
#   FFmpeg (LGPL, shared, NVDEC on, decoding only), SDL3, plus build-only nv-codec-headers and CLI11.
# Distros ship different FFmpeg versions, so a release that linked the system FFmpeg would only run on one
# distro. Re-running skips what is already built (a stamp records the versions), so CI can cache $DEPS_PREFIX.
#
#   packaging/linux/build-deps.sh <work dir> [deps prefix]     (default prefix: <work dir>/deps)
set -euo pipefail

source "$(dirname "$0")/versions.env"

work=$(realpath -m "${1:?usage: build-deps.sh <work dir> [deps prefix]}")
prefix=$(realpath -m "${2:-$work/deps}")
jobs=${JOBS:-$(nproc)}
stamp="$prefix/.nitid-deps-$DEPS_STAMP"
mkdir -p "$work/src" "$prefix"

if [ -f "$stamp" ]; then
  echo ">> dependencies already built ($stamp)"
  exit 0
fi

fetch() {  # url sha256 -> unpacked dir in $work/src
  local url=$1 sha=$2 file="$work/src/$(basename "$1")"
  [ -f "$file" ] || curl -fL --retry 3 -o "$file" "$url"
  echo "$sha  $file" | sha256sum -c --quiet -
  tar -xf "$file" -C "$work/src"
}

echo ">> nv-codec-headers $NV_CODEC_HEADERS_VERSION"
fetch "https://github.com/FFmpeg/nv-codec-headers/releases/download/n$NV_CODEC_HEADERS_VERSION/nv-codec-headers-$NV_CODEC_HEADERS_VERSION.tar.gz" "$NV_CODEC_HEADERS_SHA256"
make -C "$work/src/nv-codec-headers-$NV_CODEC_HEADERS_VERSION" PREFIX="$prefix" install >/dev/null

echo ">> FFmpeg $FFMPEG_VERSION"
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" "$FFMPEG_SHA256"
(
  cd "$work/src/ffmpeg-$FFMPEG_VERSION"
  # LGPL build (no --enable-gpl/nonfree). Nothing is autodetected, so the libraries depend only on glibc and
  # zlib; NVIDIA's libcuda/libnvcuvid are opened at run time by FFmpeg itself (ffnvcodec).
  PKG_CONFIG_PATH="$prefix/lib/pkgconfig" ./configure \
    --prefix="$prefix" --enable-shared --disable-static \
    --disable-programs --disable-doc --disable-autodetect \
    --disable-avdevice --disable-avfilter --disable-encoders --disable-muxers --disable-devices \
    --disable-network --enable-zlib \
    --enable-ffnvcodec --enable-nvdec --enable-cuvid \
    > "$work/ffmpeg-configure.log"
  make -j"$jobs" >/dev/null
  make install >/dev/null
)

echo ">> SDL $SDL3_VERSION"
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" "$SDL3_SHA256"
cmake -S "$work/src/SDL3-$SDL3_VERSION" -B "$work/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF > "$work/sdl-configure.log"
cmake --build "$work/sdl-build" -j"$jobs" > /dev/null
cmake --install "$work/sdl-build" > /dev/null

echo ">> CLI11 $CLI11_VERSION (header only)"
fetch "https://github.com/CLIUtils/CLI11/archive/refs/tags/v$CLI11_VERSION.tar.gz" "$CLI11_SHA256"
cmake -S "$work/src/CLI11-$CLI11_VERSION" -B "$work/cli11-build" -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCLI11_BUILD_TESTS=OFF -DCLI11_BUILD_EXAMPLES=OFF -DCLI11_BUILD_DOCS=OFF > /dev/null
cmake --install "$work/cli11-build" > /dev/null

# License texts shipped in the tarball (share/doc/nitid/licenses).
lic="$prefix/share/licenses"
mkdir -p "$lic/FFmpeg" "$lic/SDL3" "$lic/CLI11" "$lic/nv-codec-headers"
cp "$work/src/ffmpeg-$FFMPEG_VERSION/"{LICENSE.md,COPYING.LGPLv2.1} "$lic/FFmpeg/"
cp "$work/src/SDL3-$SDL3_VERSION/LICENSE.txt" "$lic/SDL3/"
cp "$work/src/CLI11-$CLI11_VERSION/LICENSE" "$lic/CLI11/"
sed -n '1,26p' "$work/src/nv-codec-headers-$NV_CODEC_HEADERS_VERSION/include/ffnvcodec/dynlink_loader.h" > "$lic/nv-codec-headers/LICENSE-header.txt"

rm -rf "$work/sdl-build" "$work/cli11-build" "$work/src"/*/   # keep the downloads, drop build trees
rm -f "$prefix"/.nitid-deps-*
touch "$stamp"
echo ">> dependencies ready in $prefix"
