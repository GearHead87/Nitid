# Shared helpers for the Nitid 0.4 tests (source this file): the 0.1 helpers (play, field, psnr, frame, paths), plus
# a synthetic PipeWire video source (GStreamer test pattern; nothing from the desktop is captured).
source "$(dirname "${BASH_SOURCE[0]}")/../v0.1/common.sh"

# gst_source <node name> <seconds> <pattern> <WxH> <fps> [format=BGRx]: a test pattern published as a PipeWire node.
gst_source() {
  local name=$1 secs=$2 pattern=$3 size=$4 fps=$5 format=${6:-BGRx}
  (timeout "$secs" gst-launch-1.0 -q videotestsrc pattern="$pattern" is-live=true \
     ! "video/x-raw,format=$format,width=${size%x*},height=${size#*x},framerate=$fps/1" \
     ! pipewiresink mode=provide stream-properties="props,node.name=$name" >/dev/null 2>&1 &)
  sleep 2
}
