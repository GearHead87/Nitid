#!/usr/bin/env bash
# Starts nitid and nitid-cli from a release folder with a clean environment (no LD_LIBRARY_PATH) and checks they
# report the expected version. Needs no GPU: if the host has no NVIDIA driver, the CUDA link stub stands in for
# libcuda.so.1 (the programs only start; nothing touches the GPU for --version / --help).
#
#   packaging/linux/smoke-test.sh <release folder> <version>
set -euo pipefail

stage=$(realpath "${1:?usage: smoke-test.sh <release folder> <version>}")
version=${2:?usage: smoke-test.sh <release folder> <version>}

env=(env -i PATH=/usr/bin:/bin HOME=/tmp)
if ! ldconfig -p | grep -q 'libcuda.so.1 '; then
  stub=""
  for s in /usr/local/cuda/lib64/stubs/libcuda.so "${CUDA_STUB:-}"; do [ -f "$s" ] && stub=$s && break; done
  [ -n "$stub" ] || { echo "no NVIDIA driver and no CUDA stub (set CUDA_STUB)"; exit 1; }
  shim=$(mktemp -d)
  trap 'rm -rf "$shim"' EXIT
  cp "$stub" "$shim/libcuda.so.1"
  env+=(LD_LIBRARY_PATH="$shim")
  echo "(no NVIDIA driver here: using the CUDA stub for libcuda.so.1)"
fi

check() {
  local got
  got=$("${env[@]}" "$@" 2>&1) || { echo "FAILED: $* -> $got"; exit 1; }
  echo "$* -> $got" | head -1
  [[ "$got" == *"$version"* ]] || { echo "FAILED: expected version $version"; exit 1; }
}
check "$stage/bin/nitid-cli" --version
check "$stage/bin/nitid" --version
"${env[@]}" "$stage/bin/nitid" --help > /dev/null
"${env[@]}" "$stage/bin/nitid-setup-vfx" --help > /dev/null
echo "smoke test passed"
