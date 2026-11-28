#!/usr/bin/env bash
# Builds a Nitid release tarball: nitid-<version>-linux-x86_64.tar.gz (+ .sha256) in <output dir>.
#
#   packaging/linux/build-release.sh <work dir> <output dir>
#
# Runs on Ubuntu 24.04 with packaging/linux/install-build-deps.sh done: in GitHub Actions directly, locally in
# Docker through packaging/linux/docker-build.sh. Steps: bundled libraries (build-deps.sh), VFX SDK headers,
# configure + build + unit tests, install into a folder, copy in the bundled libraries, check what the
# binaries need from the host (check-deps.sh), start both programs once, pack.
#
# The tarball runs from any folder: bin/ finds lib/ through RPATH $ORIGIN/../lib. The host provides glibc 2.38+,
# libstdc++ (GCC 13+), zlib, the Vulkan loader and the NVIDIA driver; the VFX runtime comes from nitid-setup-vfx.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
source "$here/versions.env"

work=$(realpath -m "${1:?usage: build-release.sh <work dir> <output dir>}")
out=$(realpath -m "${2:?usage: build-release.sh <work dir> <output dir>}")
deps="$work/deps"
mkdir -p "$work" "$out"
[ -d /usr/local/cuda/bin ] && export PATH="/usr/local/cuda/bin:$PATH"                     # nvcc
if command -v g++-14 >/dev/null; then export CC=${CC:-gcc-14} CXX=${CXX:-g++-14}; fi   # C++23 (std::print)

version=$(sed -nE 's/^project\(nitid_superbuild VERSION ([0-9.]+).*/\1/p' "$repo/CMakeLists.txt")
[ -n "$version" ] || { echo "cannot read the version from CMakeLists.txt" >&2; exit 1; }
name="nitid-$version-linux-x86_64"
stage="$work/$name"
echo ">> Nitid $version"

# --- bundled libraries ---------------------------------------------------------------------------------
"$here/build-deps.sh" "$work" "$deps"

# --- NVIDIA VFX SDK C headers (MIT), pinned commit ------------------------------------------------------
vfx_inc="${NITID_VFX_INCLUDE_DIR:-}"
if [ -z "$vfx_inc" ]; then
  maxine="$work/Maxine-VFX-SDK"
  if [ "$(git -C "$maxine" rev-parse HEAD 2>/dev/null)" != "$MAXINE_VFX_SDK_COMMIT" ]; then
    rm -rf "$maxine"
    git init -q "$maxine"
    git -C "$maxine" remote add origin https://github.com/NVIDIA-Maxine/Maxine-VFX-SDK.git
    git -C "$maxine" sparse-checkout set nvvfx/include
    git -C "$maxine" fetch -q --depth 1 --filter=blob:none origin "$MAXINE_VFX_SDK_COMMIT"
    git -C "$maxine" checkout -q FETCH_HEAD
  fi
  vfx_inc="$maxine/nvvfx/include"
fi

# --- NVIDIA DLSS SDK (headers + static NGX loader) for the DLSS module, pinned commit -----------------------
dlss_sdk="${NITID_DLSS_SDK_DIR:-}"
if [ -z "$dlss_sdk" ]; then
  dlss_sdk="$work/DLSS"
  if [ "$(git -C "$dlss_sdk" rev-parse HEAD 2>/dev/null)" != "$DLSS_SDK_COMMIT" ]; then
    rm -rf "$dlss_sdk"
    git init -q "$dlss_sdk"
    git -C "$dlss_sdk" remote add origin https://github.com/NVIDIA/DLSS.git
    git -C "$dlss_sdk" sparse-checkout set --no-cone /include/ /lib/Linux_x86_64/libnvsdk_ngx.a /LICENSE.txt
    git -C "$dlss_sdk" fetch -q --depth 1 --filter=blob:none origin "$DLSS_SDK_COMMIT"
    git -C "$dlss_sdk" checkout -q FETCH_HEAD
  fi
  echo "$DLSS_NGX_LIB_SHA256  $dlss_sdk/lib/Linux_x86_64/libnvsdk_ngx.a" | sha256sum -c --quiet - \
    || { echo "DLSS SDK: libnvsdk_ngx.a checksum mismatch" >&2; exit 1; }
fi

# --- build + unit tests -------------------------------------------------------------------------------------
# The build machine has no NVIDIA driver, only CUDA's link stub (libcuda.so). Linking the programs checks the
# libraries libnitid needs too, so point the linker at a libcuda.so.1 made from the stub, and at $deps/lib for
# FFmpeg's own dependencies. Only the link step uses these; the binaries keep needing the real libcuda.so.1.
stub_dir="$work/cuda-stub"
mkdir -p "$stub_dir"
ln -sf /usr/local/cuda/lib64/stubs/libcuda.so "$stub_dir/libcuda.so.1"
build="$work/build"
PKG_CONFIG_PATH="$deps/lib/pkgconfig" cmake -S "$repo" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$stub_dir:$deps/lib" \
  -DCMAKE_PREFIX_PATH="$deps" \
  -DCMAKE_INSTALL_PREFIX="$stage" -DCMAKE_INSTALL_LIBDIR=lib \
  -DNITID_VFX_INCLUDE_DIR="$vfx_inc" -DNITID_VFX_RUNTIME_DIR= \
  -DNITID_DLSS_SDK_DIR="$dlss_sdk" -DNITID_DLSS_SDK_VERSION="$DLSS_SDK_VERSION" -DNITID_DLSS_RUNTIME_DIR= \
  -DNITID_BUILD_TESTS=ON
cmake --build "$build"
[ -f "$build/libnitid/libnitid-dlss.so" ] || { echo "the DLSS module was not built" >&2; exit 1; }
ctest --test-dir "$build" --output-on-failure

# --- install into the tarball folder --------------------------------------------------------------------
rm -rf "$stage"
cmake --install "$build" --strip
rm -rf "$stage/include" "$stage/lib/cmake" "$stage/lib/pkgconfig"   # runtime only, no development files
find "$stage/lib" -name 'libnitid.so' -type l -delete                 # dev symlink; keep libnitid.so.N

# Copy every library a Nitid binary needs from $deps (FFmpeg, SDL3), under its soname, and let them find each
# other through RPATH $ORIGIN.
bundle() {
  local elf=$1 lib path
  for lib in $(readelf -d "$elf" | sed -nE 's/.*\(NEEDED\).*\[(.*)\]/\1/p'); do
    [ -e "$stage/lib/$lib" ] && continue
    path="$deps/lib/$lib"
    [ -e "$path" ] || continue
    cp -L "$path" "$stage/lib/$lib"
    chmod 0755 "$stage/lib/$lib"
    strip --strip-unneeded "$stage/lib/$lib"
    patchelf --set-rpath '$ORIGIN' "$stage/lib/$lib"
    bundle "$stage/lib/$lib"
  done
}
for elf in "$stage"/bin/* "$stage"/lib/libnitid.so.*; do
  file -L "$elf" | grep -q ELF && bundle "$elf"
done

install -m 0755 "$here/nitid-setup-vfx" "$stage/bin/nitid-setup-vfx"
install -m 0755 "$here/nitid-setup-dlss" "$stage/bin/nitid-setup-dlss"
doc="$stage/share/doc/nitid"
mkdir -p "$doc/licenses"
cp "$repo/README.md" "$repo/CHANGELOG.md" "$here/THIRD-PARTY-NOTICES.md" "$doc/"
[ -f "$repo/LICENSE" ] && cp "$repo/LICENSE" "$doc/"
cp -r "$deps/share/licenses/." "$doc/licenses/"
mkdir -p "$doc/licenses/Maxine-VFX-SDK"
cp "$(dirname "$(dirname "$vfx_inc")")/LICENSE"* "$doc/licenses/Maxine-VFX-SDK/" 2>/dev/null || true
mkdir -p "$doc/licenses/DLSS-SDK"   # lib/libnitid-dlss.so contains NVIDIA's NGX loader (libnvsdk_ngx.a)
cp "$dlss_sdk/LICENSE.txt" "$doc/licenses/DLSS-SDK/LICENSE.txt"
install -m 0644 "$here/README-linux.txt" "$stage/README.txt"

# --- checks -----------------------------------------------------------------------------------------------
"$here/check-deps.sh" "$stage"
"$here/smoke-test.sh" "$stage" "$version"

# --- pack -----------------------------------------------------------------------------------------------------
tar -C "$work" --owner=0 --group=0 --sort=name -czf "$out/$name.tar.gz" "$name"
(cd "$out" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo ">> $out/$name.tar.gz ($(du -h "$out/$name.tar.gz" | cut -f1))"
cat "$out/$name.tar.gz.sha256"
