#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ps3dev="${PS3DEV:-$root/_deps/ps3dev}"
cg="${CG_LIB_DIR:-$root/_deps/cg/usr/lib64}"
src="$root/engine/src/platform/ps3/shaders"
if [ ! -f "$cg/libCg.so" ]; then
  mkdir -p "$root/_deps/cg"
  curl -fL -o "$root/_deps/cg/cg.tgz" https://developer.download.nvidia.com/cg/Cg_3.1/Cg-3.1_April2012_x86_64.tgz
  tar -xzf "$root/_deps/cg/cg.tgz" -C "$root/_deps/cg"
  rm -f "$root/_deps/cg/cg.tgz"
fi
for f in "$src"/*.vcg; do LD_LIBRARY_PATH="$cg" "$ps3dev/bin/cgcomp" -v "$f" "${f%.vcg}.vpo"; done
for f in "$src"/*.fcg; do LD_LIBRARY_PATH="$cg" "$ps3dev/bin/cgcomp" -f "$f" "${f%.fcg}.fpo"; done
ls -la "$src"/*.vpo "$src"/*.fpo
