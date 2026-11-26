#!/usr/bin/env bash
# Checks that a release tag matches the source: vX.Y.Z or vX.Y.Z-suffix (pre-release, e.g. v0.2.0-rc.1) must
# match the VERSION in all four CMakeLists.txt files, and CHANGELOG.md must have a "## [X.Y.Z]" section.
#
#   scripts/check-version.sh v0.2.0
set -euo pipefail

tag=${1:?usage: check-version.sh vX.Y.Z[-suffix]}
[[ "$tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$ ]] || { echo "tag '$tag' is not vX.Y.Z[-suffix]" >&2; exit 1; }
version=${BASH_REMATCH[1]}
cd "$(dirname "$0")/.."

fail=0
for f in CMakeLists.txt libnitid/CMakeLists.txt apps/nitid/CMakeLists.txt apps/nitid-cli/CMakeLists.txt; do
  v=$(sed -nE 's/^project\([A-Za-z_-]+ VERSION ([0-9.]+).*/\1/p' "$f")
  [ "$v" = "$version" ] || { echo "$f has version '$v', tag $tag needs $version" >&2; fail=1; }
done
grep -q "^## \[$version\]" CHANGELOG.md || { echo "CHANGELOG.md has no '## [$version]' section" >&2; fail=1; }
[ "$fail" = 0 ] && echo "version $version matches $tag"
exit $fail
