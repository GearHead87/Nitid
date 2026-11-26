#!/usr/bin/env bash
# 0.2 step 4: on-screen controls (Dear ImGui drawn by the engine's overlay hook).
#  A. Pictures: window images (the player's own --capture-at, which saves only Nitid's window) with the control bar
#     and the settings + stats panels, and with the help panel. Saved as JPEG next to this script.
#  B. Cost: 10 s playback (VSR + FG per refresh) with the controls and two panels shown vs hidden: display fps,
#     missed refreshes.
#  C. Start without a file (the "open a file" screen) and at the end of a video (stays on the last frame):
#     runs, no errors.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
here=$(cd "$(dirname "$0")" && pwd)
R="$root/results/v0.2/04"; mkdir -p "$R"
clip="$media/sintel_trailer-1080p.mp4"

shot() {  # name time [options...]
  local name=$1 t=$2; shift 2
  (cd "$R" && timeout 60 "$nitid" "$clip" --ui on --volume 0.2 --capture-at "$t" --capture-prefix "$name" "$@" >/dev/null 2>&1) || true
  ffmpeg -hide_banner -loglevel error -y -i "$R/${name}_$(printf '%.2f' "$t")s.ppm" -vf "scale=1600:-1" -q:v 3 "$here/$name.jpg"
  echo "- $name.jpg"
}
echo "### A. Pictures"
echo
shot controls-settings-stats 2.0 --test-panel settings --test-panel stats
shot help 2.0 --test-panel help --upscale fsr

echo
echo "### B. Cost of the controls (10 s, VSR HIGH + frame gen MEDIUM per refresh)"
echo
echo "| Controls | Display fps | Missed refreshes |"
echo "|---|---|---|"
for ui in off "on --test-panel settings --test-panel stats" off "on --test-panel settings --test-panel stats" off "on --test-panel settings --test-panel stats"; do
  # shellcheck disable=SC2086
  play 10 "$clip" --volume 0.2 --ui $ui >/dev/null
  echo "| ${ui%% *} | $(field 'Display:\s+\K[0-9.]+') | $(field '([0-9]+) missed refreshes') |"
done

echo
echo "### C. Start without a file / end of a video"
echo
out=$(timeout 30 "$nitid" --quit-after 3 2>&1) && echo "- no file, 3 s: ran, exit 0, $(grep -c -i error <<<"$out") error lines"
tmp=$(mktemp)
"$nitid" "$media/avsync_1080p24.mp4" --start 57 --volume 0.2 --stats >"$tmp" 2>&1 &
pid=$!; sleep 8; kill -TERM $pid; wait $pid || true
echo "- end of a video (start 3 s before the end, 8 s, then SIGTERM): '$(grep -o 'End of video.' "$tmp" | head -1)', $(grep -A100 'End of video' "$tmp" | grep -c '^t=') stats lines after the end (still running), quit on SIGTERM: $(grep -q '^Source:' "$tmp" && echo yes || echo no)"
rm -f "$tmp"
