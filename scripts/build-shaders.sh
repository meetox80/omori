#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-$root/_deps/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
vita3k="${VITA3K:-$root/_deps/vita3k/Vita3K}"
src="$root/engine/src/platform/vita/shaders"
shacccg="$root/_deps/shacccg/libshacccg.suprx"
shacccg_sha256=188adb8c34c3983d63244c32d981fd7b518a8dcfd200f5886233fface6b5fa40
if [ ! -f "$shacccg" ]; then
  mkdir -p "$(dirname "$shacccg")"
  curl -fL -o "$shacccg" https://github.com/AnimMouse/SceShaccCg/raw/main/libshacccg.suprx
fi
echo "$shacccg_sha256  $shacccg" | sha256sum -c --quiet -
cmake -S "$root/tools/gxpc" -B "$root/build/gxpc" -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/build/gxpc" >/dev/null
fs="$(dirname "$vita3k")/portable/fs"
app="$fs/ux0/app/OMRI00099"
work="$fs/ux0/data/gxpc"
rm -rf "$app" "$work"
mkdir -p "$app" "$work/in" "$work/out" "$fs/ur0/data"
unzip -qo "$root/build/gxpc/gxpc.vpk" -d "$app"
cp "$shacccg" "$fs/ur0/data/libshacccg.suprx"
cp "$src"/*.cg "$work/in/"
cp "$src/shaders.txt" "$work/jobs.txt"
timeout 300 "$vita3k" --frontend sdl -r OMRI00099 >"$root/build/gxpc/vita3k.log" 2>&1 || true
cat "$work/log.txt"
[ -f "$work/done" ] && ! grep -q failed "$work/log.txt"
cp "$work/out/"*.gxp "$src/"
