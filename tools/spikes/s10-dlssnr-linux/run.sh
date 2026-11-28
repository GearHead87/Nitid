#!/usr/bin/env bash
# S10: asks the native Linux NGX whether DLSS Neural Rendering (feature 18, "DLSS 5") is available.
# Build first: cmake -S tools/spikes -B build-spikes -G Ninja && cmake --build build-spikes --target s10-dlssnr-linux
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
day="$(date +%F)"
__NGX_LOG_LEVEL=1 ./build-spikes/s10-dlssnr-linux/s10-dlssnr-linux \
  > "tools/spikes/s10-dlssnr-linux/output-$day.md" 2> /tmp/s10-ngx.log || true
{ echo "# S10 NGX log excerpt ($day)"; echo; echo '```'; grep -i dlssnr /tmp/s10-ngx.log; echo '```'; } \
  > "tools/spikes/s10-dlssnr-linux/ngx-log-excerpt-$day.md"
grep -E "Reserved18|DLSSNR" "tools/spikes/s10-dlssnr-linux/output-$day.md"
