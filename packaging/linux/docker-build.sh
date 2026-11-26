#!/usr/bin/env bash
# Builds a release tarball locally, in the same Ubuntu 24.04 setup GitHub Actions uses.
#
#   packaging/linux/docker-build.sh [output dir]      (default: out/)
#
# The image (nitid-build:ubuntu-24.04) is built once and kept for future releases; it is rebuilt only when
# install-build-deps.sh changes. Sources are mounted read-only; the build (dependencies, objects) lives in the
# Docker volume nitid-release-work, so repeated builds are quick and nothing large lands in the repo.
# Remove the volume with: docker volume rm nitid-release-work
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=$(realpath -m "${1:-$repo/out}")
image=nitid-build:ubuntu-24.04
mkdir -p "$out"

deps_hash=$(sha256sum "$here/install-build-deps.sh" "$here/Dockerfile" | sha256sum | cut -c1-16)
if [ "$(docker image inspect -f '{{ index .Config.Labels "nitid.deps" }}' "$image" 2>/dev/null)" != "$deps_hash" ]; then
  docker build --label "nitid.deps=$deps_hash" -t "$image" "$here"
fi

docker run --rm \
  -v "$repo:/src:ro" \
  -v nitid-release-work:/work \
  -v "$out:/out" \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  "$image" bash -c '/src/packaging/linux/build-release.sh /work /out; s=$?; chown -R "$HOST_UID:$HOST_GID" /out; exit $s'
