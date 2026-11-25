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
