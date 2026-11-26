#!/usr/bin/env bash
# Checks what a release folder needs from the host: every shared library a binary or bundled library links must
# be bundled in lib/ or be on the allow-list below (things every desktop Linux with an NVIDIA driver has).
# Also prints the newest glibc / libstdc++ symbol versions used, i.e. the oldest distro it runs on.
#
#   packaging/linux/check-deps.sh <release folder>
set -euo pipefail

stage=${1:?usage: check-deps.sh <release folder>}
[ -d "$stage/bin" ] && [ -d "$stage/lib" ] || { echo "$stage is not a release folder (no bin/ and lib/)" >&2; exit 1; }

allowed=(
  linux-vdso.so.1 ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1
  libstdc++.so.6 libgcc_s.so.1
  libz.so.1          # zlib
  libvulkan.so.1     # Vulkan loader (vulkan-icd-loader / libvulkan1)
  libcuda.so.1       # NVIDIA driver
)

fail=0
elves=()
for f in "$stage"/bin/* "$stage"/lib/*.so*; do
  file -L "$f" | grep -q ELF && elves+=("$f")
done
for f in "${elves[@]}"; do
  for lib in $(readelf -d "$f" | sed -nE 's/.*\(NEEDED\).*\[(.*)\]/\1/p'); do
    [ -e "$stage/lib/$lib" ] && continue
    if [[ " ${allowed[*]} " != *" $lib "* ]]; then
      echo "NOT ALLOWED: ${f#$stage/} needs $lib (bundle it or add it to the allow-list)"
      fail=1
    fi
  done
  rp=$(readelf -d "$f" | sed -nE 's/.*\((RUNPATH|RPATH)\).*\[(.*)\]/\2/p')
  case "$rp" in ''|'$ORIGIN'|'$ORIGIN/../lib') ;; *) echo "BAD RPATH: ${f#$stage/} has '$rp'"; fail=1 ;; esac
done

newest() { objdump -T "${elves[@]}" 2>/dev/null | grep -oE "$1_[0-9.]+" | sort -uV | tail -1; }
echo "host libraries used: $(cd "$stage" && for f in "${elves[@]}"; do readelf -d "$f" | sed -nE 's/.*\(NEEDED\).*\[(.*)\]/\1/p'; done | sort -u | while read -r l; do [ -e "$stage/lib/$l" ] || printf '%s ' "$l"; done)"
echo "newest symbol versions: $(newest GLIBC) $(newest GLIBCXX) $(newest CXXABI)"
echo "bundled: $(cd "$stage/lib" && ls | tr '\n' ' ')"
[ "$fail" = 0 ] && echo "dependency check passed" || { echo "dependency check FAILED"; exit 1; }
