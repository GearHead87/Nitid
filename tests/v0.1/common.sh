# Shared helpers for the Nitid 0.1 tests (source this file).
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
nitid="$root/build/apps/nitid/nitid"
nitid_cli="$root/build/apps/nitid-cli/nitid-cli"
media="$root/results/media"
[ -x "$nitid" ] || { echo "build first: cmake -S $root -B $root/build -G Ninja && cmake --build $root/build"; exit 1; }

# play <seconds> <file> [nitid options...] -> prints the final report; also keeps the full log in $log
play() {
  local secs=$1 file=$2; shift 2
  log=$(mktemp)
  timeout $((secs + 30)) "$nitid" "$file" --quit-after "$secs" "$@" >"$log" 2>&1 || true
  cat "$log"
}

# field <regex> : first capture group from the last report in $log
field() { grep -oP "$1" "$log" | tail -1; }

# psnr <a> <b> : PSNR (dB) between two images or videos (RGB)
psnr() { ffmpeg -hide_banner -nostdin -i "$1" -i "$2" -lavfi "[0]format=rgb24[a];[1]format=rgb24[b];[a][b]psnr" -f null - 2>&1 \
  | grep -oE 'average:[0-9.inf]+' | cut -d: -f2; }

# frame <video> <index> <out.ppm> [scale WxH] : one decoded frame (optionally Lanczos-scaled)
frame() {
  local vf="select=eq(n\,$2)"; [ -n "${4:-}" ] && vf="$vf,scale=${4/x/:}:flags=lanczos"
  ffmpeg -hide_banner -loglevel error -nostdin -y -i "$1" -vf "$vf" -frames:v 1 "$3"
}
