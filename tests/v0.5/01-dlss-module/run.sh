#!/usr/bin/env bash
# 0.5 step 1: the DLSS module and NVIDIA's DLSS runtime.
#  A. With everything present: nitid-cli info reports DLSS SR, DLSS-G (max multiplier) and optical flow.
#  B. nitid-setup-dlss into a temporary folder: download, SHA-256 check, Nitid finds it, second run is a no-op,
#     uninstall.
#  C. Missing pieces are reported, not fatal: no runtime (NITID_DLSS_DIR=empty folder), no module.
#  D. Nothing else changed: VSR HIGH + VFX frame gen still plays at the display rate in a window.
set -euo pipefail
source "$(dirname "$0")/../common.sh"
info() { "$nitid_cli" info 2>/dev/null | grep -E '^(DLSS|  upscaling|  frame gen|Optical)' | sed 's/^/    /'; }
echo "### A. Developer build (module + SDK runtime)"
echo; echo '```'; info; echo '```'; echo
echo "### B. nitid-setup-dlss"
echo
T=$(mktemp -d)
start=$SECONDS
XDG_DATA_HOME=$T "$root/packaging/linux/nitid-setup-dlss" --yes 2>/dev/null | grep -E "^(installed|check)" | sed "s|$T|\$XDG_DATA_HOME|; s/^/    /"
echo "    (took $((SECONDS - start)) s)"
echo "    files: $(ls "$T/nitid/dlss" | tr '\n' ' ')($(du -sh "$T/nitid/dlss" | cut -f1))"
echo "    Nitid uses: $(XDG_DATA_HOME=$T "$nitid_cli" info 2>&1 | grep -oP 'DLSS \S+ from \K.*' | sed "s|$T|\$XDG_DATA_HOME|")"
echo "    second run: $(XDG_DATA_HOME=$T "$root/packaging/linux/nitid-setup-dlss" --yes | sed "s|$T|\$XDG_DATA_HOME|")"
echo "    uninstall: $(XDG_DATA_HOME=$T "$root/packaging/linux/nitid-setup-dlss" --uninstall | sed "s|$T|\$XDG_DATA_HOME|")"
rm -rf "$T"
echo
echo "### C. Missing pieces"
echo
E=$(mktemp -d)
echo "- No DLSS runtime: \`$(NITID_DLSS_DIR=$E "$nitid_cli" info 2>/dev/null | grep '^DLSS')\`"
echo "- No DLSS module: \`$(NITID_DLSS_MODULE=$E/libnitid-dlss.so "$nitid_cli" info 2>/dev/null | grep '^DLSS' | sed "s|$E|/tmp/x|")\`"
rmdir "$E"
echo
echo "### D. Unchanged playback (lq_540p, default window, VSR HIGH + VFX frame gen MEDIUM, 6 s)"
echo
play 6 "$media/lq_540p.mp4" >/dev/null
echo "    $(grep -E '^Display:' "$log")"
echo "    $(grep -E '^Work:' "$log")"
