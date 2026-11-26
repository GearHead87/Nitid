#!/usr/bin/env bash
# Release test 01: the Linux release tarball, built in Docker (Ubuntu 24.04, as in GitHub Actions), used the way
# a user would on this machine (Arch, NVIDIA GPU): extract to a temp folder, install the VFX runtime with
# nitid-setup-vfx, check info, play videos in the window, and compare its bench output with the developer
# build's (same code, bundled FFmpeg 9.1 vs the system's).
# Everything goes to /tmp (removed at the end); the build itself stays in the nitid-release-work Docker volume.
#   tests/release/01-linux-tarball/run.sh [--no-build]
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
media="$root/results/media"
dev_cli="$root/build/apps/nitid-cli/nitid-cli"
tmp=/tmp/nitid-release-test
rm -rf "$tmp"; mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT

if [ "${1:-}" != "--no-build" ]; then
  echo "### Build (Docker, Ubuntu 24.04)"
  echo '```'
  "$root/packaging/linux/docker-build.sh" "$root/out" 2>&1 | grep -E '^>> |tests passed|newest|bundled|check passed|smoke test passed|^/.* -> '
  echo '```'
fi
tarball=$(ls "$root"/out/nitid-*-linux-x86_64.tar.gz | tail -1)
(cd "$root/out" && sha256sum -c "$(basename "$tarball").sha256")
tar -xzf "$tarball" -C "$tmp"
app=$(ls -d "$tmp"/nitid-*-linux-x86_64)
export XDG_DATA_HOME="$tmp/xdg"     # nitid-setup-vfx installs here instead of ~/.local/share
unset LD_LIBRARY_PATH NITID_VFX_DIR

echo
echo "### Host libraries the binaries resolve (outside lib/)"
echo '```'
ldd "$app/bin/nitid" "$app/bin/nitid-cli" | grep '=>' | grep -v "$app" | awk '{print $1, $3}' | sort -u
echo '```'

echo
echo "### Before nitid-setup-vfx"
echo '```'
"$app/bin/nitid-cli" info 2>&1
echo '```'

echo
echo "### nitid-setup-vfx --yes"
echo '```'
"$app/bin/nitid-setup-vfx" --yes 2>&1 | grep -v '^#\|%'
"$app/bin/nitid-setup-vfx" --yes 2>&1 | tail -1     # second run: already installed
du -sh "$XDG_DATA_HOME/nitid/vfx"
echo '```'

echo
echo "### After: info on the 4K AV1 file (bundled FFmpeg with NVDEC)"
echo '```'
"$app/bin/nitid-cli" info "$media/src_4k_av1.mkv" 2>&1
echo '```'

echo
echo "### Window playback, 6 s each (VSR HIGH + frame gen MEDIUM per refresh, on-screen controls, sound at 0.2)"
echo '```'
for f in lq_540p.mp4 src_4k_av1.mkv sintel_trailer-1080p.mp4; do
  echo "== $f"
  timeout 40 "$app/bin/nitid" "$media/$f" --quit-after 6 --volume 0.2 2>&1 | grep -E '^(Sound|Display|Upscale|FrameGen):' || true
done
echo "== sintel_trailer-1080p.mp4 --start 30 --upscale fsr (seek + FSR 1)"
timeout 40 "$app/bin/nitid" "$media/sintel_trailer-1080p.mp4" --start 30 --upscale fsr --quit-after 5 --volume 0.2 2>&1 \
  | grep -E 'seek to|^(Sound|Display|Upscale):' || true
echo "== sintel_trailer-1080p.mp4 --frame-gen high --compare (render-ahead pacing, compare view)"
timeout 40 "$app/bin/nitid" "$media/sintel_trailer-1080p.mp4" --frame-gen high --compare --quit-after 8 --volume 0.2 2>&1 \
  | grep -E "can't keep up|^(FrameGen|Display):" || true
echo "== no file (open screen), 3 s"
timeout 40 "$app/bin/nitid" --quit-after 3 >/dev/null 2>&1 && echo "ran, exit 0"
echo '```'

echo
echo "### Release build vs developer build: same bench output?"
echo "540p -> 1080p, first 96 output frames, lossless: (1) VSR HIGH + frame gen MEDIUM 2x, (2) FSR 1 + seek to 4 s."
echo '```'
for job in "vsr --frame-gen medium --multiplier 2" "fsr --upscale fsr --start 4"; do
  read -r name args <<<"$job"
  for b in release dev; do
    cli=$([ $b = release ] && echo "$app/bin/nitid-cli" || echo "$dev_cli")
    # shellcheck disable=SC2086
    "$cli" bench "$media/lq_540p.mp4" --size 1920x1080 $args --frames 96 --lossless -o "$tmp/${name}_$b.mkv" >/dev/null 2>&1
  done
  for b in release dev; do
    ffmpeg -hide_banner -loglevel error -nostdin -i "$tmp/${name}_$b.mkv" -map 0:v -f framemd5 - | grep -v '^#' | md5sum | sed "s/-/$name $b frames/"
  done
done
echo '```'

echo
echo "### nitid-setup-vfx --uninstall"
echo '```'
"$app/bin/nitid-setup-vfx" --uninstall
echo '```'
