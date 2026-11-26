#!/usr/bin/env bash
# Sets Nitid's version everywhere it is written and starts its CHANGELOG section.
#
#   scripts/bump-version.sh 0.2.0
#
# Updates the project VERSION in the four CMakeLists.txt files (top level, libnitid, both apps) and the
# libnitid version the apps ask for, then renames CHANGELOG.md's "Unreleased" section to "[0.2.0] - <today>".
# Then: fill in the changelog, commit, and tag:  git tag v0.2.0 && git push origin main v0.2.0
set -euo pipefail

version=${1:?usage: bump-version.sh X.Y.Z}
[[ "$version" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "version must be X.Y.Z" >&2; exit 1; }
major_minor="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}"
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"

sed -i -E "s/^(project\([A-Za-z_-]+ VERSION )[0-9]+\.[0-9]+\.[0-9]+/\1$version/" \
  CMakeLists.txt libnitid/CMakeLists.txt apps/nitid/CMakeLists.txt apps/nitid-cli/CMakeLists.txt
sed -i -E "s/find_package\(libnitid [0-9]+\.[0-9]+ /find_package(libnitid $major_minor /" \
  apps/nitid/CMakeLists.txt apps/nitid-cli/CMakeLists.txt

if grep -q "^## \[$version\]" CHANGELOG.md; then
  echo "CHANGELOG.md already has a $version section"
else
  today=$(date -u +%F)
  prev=$(sed -nE 's/^## \[([0-9][^]]*)\].*/\1/p' CHANGELOG.md | head -1)
  sed -i "s/^## \[Unreleased\]$/## [Unreleased]\n\n## [$version] - $today/" CHANGELOG.md
  sed -i -E "s#^\[Unreleased\]: .*#[Unreleased]: https://github.com/GearHead87/Nitid/compare/v$version...HEAD\n[$version]: https://github.com/GearHead87/Nitid/compare/v$prev...v$version#" CHANGELOG.md
fi

scripts/check-version.sh "v$version"
git --no-pager diff --stat
