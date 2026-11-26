#!/usr/bin/env bash
# 0.4 step 2: real screen capture through the desktop portal (KDE), DMA-BUF from the compositor.
# PRIVACY: this captures the desktop. It only records numbers (status, rates, latency); it never saves or shows a
# captured image (no --capture-at), and its output contains no picture data.
# Permission: uses Nitid's remembered choice ($XDG_STATE_HOME/nitid/screencast-token). If there is none it copies
# spike S6's token (same unsandboxed host app, so the desktop accepts it); if neither exists, the desktop shows its
# dialog on the first run and a person must choose a screen.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
state=$(mktemp -d); trap 'rm -rf "$state"' EXIT
mkdir -p "$state/nitid"
own="${XDG_STATE_HOME:-$HOME/.local/state}/nitid/screencast-token"
if [ -f "$own" ]; then cp "$own" "$state/nitid/screencast-token"; src="Nitid's saved choice"
elif [ -f "$root/results/s6-pipewire-dmabuf/restore_token.txt" ]; then cp "$root/results/s6-pipewire-dmabuf/restore_token.txt" "$state/nitid/screencast-token"; src="spike S6's saved choice"
else src="none (dialog)"; fi
echo "Permission: $src"
echo
echo "| Mode | Portal | Format | Frames in (fps) | Capture -> screen | Copy + convert per frame | Display fps | Missed | Generated |"
echo "|---|---|---|---|---|---|---|---|---|"
for mode in low-latency smooth low-latency smooth; do
  out=$(XDG_STATE_HOME="$state" timeout 60 "$nitid" --capture-screen --live-mode $mode --quit-after 10 --width 1280 --height 720 2>&1 || true)
  echo "| $mode | $(grep -q 'remembered your choice' <<<"$out" && echo "no dialog" || echo dialog) | $(grep -oP 'capture: \K\S+ \S+, \S+(?= modifier| memory)' <<<"$out" | head -1) | $(grep -oP 'Live:.*\| \K[0-9.]+(?= fps arriving)' <<<"$out") | $(grep -oP 'capture -> screen \K[0-9.]+ ms' <<<"$out") | $(grep -oP 'decode\+copy \K[0-9.]+ ms' <<<"$out") | $(grep -oP 'Display:\s+\K[0-9.]+(?= fps)' <<<"$out") | $(grep -oP '[0-9]+(?= missed refreshes)' <<<"$out") | $(grep -oP '\(\K[0-9]+(?= generated\))' <<<"$out") |"
done
echo
echo "Nitid's window was on the captured screen, so the desktop changed every frame (the window shows itself)."
