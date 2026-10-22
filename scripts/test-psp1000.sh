#!/usr/bin/env bash
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
eboot="$(realpath "${1:-$root/_work/psp/PSP/GAME/OMORI/EBOOT.PBP}")"
limit_kb="${PSP1000_LIMIT_KB:-15360}"
cases=(intro:270 maps:300 abbi:150 kc:150)

for tool in PPSSPPSDL weston; do
  command -v "$tool" >/dev/null || { echo "$tool not found"; exit 2; }
done
[ -f "$eboot" ] || { echo "no EBOOT at $eboot"; exit 2; }

work="$(mktemp -d "${TMPDIR:-/tmp}/omori-psp1000.XXXXXX")"
socket="omori-psp1000-$$"
runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
weston --backend=headless --renderer=gl --socket="$socket" --width=480 --height=272 >"$work/weston.log" 2>&1 &
weston_pid=$!
cleanup() {
  kill "$weston_pid" 2>/dev/null
  wait "$weston_pid" 2>/dev/null
  [ "${KEEP:-0}" = 1 ] && echo "logs kept in $work" || rm -rf "$work"
}
trap cleanup EXIT
for _ in $(seq 50); do [ -S "$runtime/$socket" ] && break; sleep 0.1; done

run_case() {
  local name="$1" secs="$2"
  local game="$work/$name/ppsspp/PSP/GAME/OMORI" system="$work/$name/ppsspp/PSP/SYSTEM"
  mkdir -p "$game/saves" "$system"
  ln -s "$eboot" "$game/EBOOT.PBP"
  cp "$root/engine/tests/psp1000/$name.txt" "$game/input.txt"
  printf -- '--input input.txt --perf\n' >"$game/args.txt"
  printf '[General]\nFirstRun = False\nCheckForNewVersion = False\n[SystemParam]\nPSPModel = 0\n[Sound]\nEnable = False\n' >"$system/ppsspp.ini"
  XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" SDL_VIDEODRIVER=wayland XDG_CONFIG_HOME="$work/$name" \
    timeout "$secs" PPSSPPSDL "$game/EBOOT.PBP" >"$work/$name/ppsspp.log" 2>&1
}

for ((i = 0; i < ${#cases[@]}; i += 2)); do
  pids=()
  for c in "${cases[@]:i:2}"; do
    run_case "${c%%:*}" "${c##*:}" &
    pids+=($!)
  done
  wait "${pids[@]}"
done

failed=0
for c in "${cases[@]}"; do
  name="${c%%:*}"
  log="$work/$name/ppsspp/PSP/GAME/OMORI/log.txt"
  problems=()
  if [ -f "$log" ]; then
    grep -q 'test-psp1000 done' "$log" || problems+=("did not reach the end of the script")
    peak="$(grep -o 'heap used [0-9]*' "$log" | awk '$3 > m { m = $3 } END { print m + 0 }')"
    skipped="$(grep -c 'skipped: out of memory' "$log")"
    [ "$skipped" = 0 ] || problems+=("$skipped assets skipped for lack of memory")
    [ "$peak" -le "$limit_kb" ] || problems+=("peak heap ${peak} KB is over ${limit_kb} KB")
  else
    peak=0
    problems+=("no log written")
  fi
  if [ ${#problems[@]} = 0 ]; then
    printf '%-6s ok, peak heap %s KB\n' "$name" "$peak"
  else
    failed=1
    printf '%-6s failed, peak heap %s KB\n' "$name" "$peak"
    for p in "${problems[@]}"; do printf '       %s\n' "$p"; done
    [ -f "$log" ] && grep 'skipped: out of memory' "$log" | sort | uniq -c | head -5 | sed 's/^/       /'
  fi
done
exit $failed
