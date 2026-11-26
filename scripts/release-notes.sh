#!/usr/bin/env bash
# Writes the GitHub release text for a tag: the version's CHANGELOG.md section (written by hand), install
# steps, and every commit since the previous release (generated from git, folded).
#
#   scripts/release-notes.sh <tag> <previous tag or ""> <output file>
set -euo pipefail

tag=${1:?usage: release-notes.sh <tag> <previous tag> <output>}
prev=${2-}
out=${3:?usage: release-notes.sh <tag> <previous tag> <output>}
version=${tag#v}; version=${version%%-*}
repo_url=https://github.com/GearHead87/Nitid
cd "$(dirname "$0")/.."

{
  if [ "$tag" != "v$version" ]; then
    echo "> [!NOTE]"
    echo "> Pre-release of Nitid $version for testing. It may have bugs that are fixed before $version."
    echo
  fi

  # CHANGELOG section: from "## [X.Y.Z]" up to the next "## [" or the link list at the bottom.
  awk -v v="$version" '
    $0 ~ "^## \\[" v "\\]" { on = 1; next }
    on && (/^## \[/ || /^\[[^]]+\]: /) { exit }
    on { print }
  ' CHANGELOG.md | sed -e '/./,$!d'   # drop leading blank lines

  cat <<EOM

## Install

1. Download \`nitid-$version-linux-x86_64.tar.gz\` below and extract it anywhere:
   \`tar -xzf nitid-$version-linux-x86_64.tar.gz\`
2. Install NVIDIA's VFX runtime once (AI upscaling and frame generation; about 450 MB from NVIDIA, under
   NVIDIA's license): \`nitid-$version-linux-x86_64/bin/nitid-setup-vfx\`
3. Play a video: \`nitid-$version-linux-x86_64/bin/nitid video.mp4\` (\`bin/nitid-cli info\` shows what Nitid sees)

Needs an NVIDIA RTX GPU with the proprietary driver, the Vulkan loader, and glibc 2.38+ (Ubuntu 24.04+,
Debian 13+, Fedora 39+, Arch). See \`README.txt\` in the tarball.
EOM

  echo
  if [ -n "$prev" ]; then
    range="$prev..$tag"
    count=$(git rev-list --count "$range")
    echo "<details><summary>All $count commits since $prev</summary>"
  else
    range="$tag"
    count=$(git rev-list --count "$range")
    echo "<details><summary>All $count commits</summary>"
  fi
  echo
  git log --no-merges --format="- %s ([%h]($repo_url/commit/%H))" "$range"
  echo
  echo "</details>"
  echo
  if [ -n "$prev" ]; then
    echo "**Full diff:** $repo_url/compare/$prev...$tag"
  fi
} > "$out"
echo "wrote $out ($(wc -l < "$out") lines)"
