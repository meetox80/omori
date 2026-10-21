#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
decaf="$root/_deps/decaf"
rev=e6c528a20a41c34e0f9eb91dd3da40f119db2dee
src="$root/engine/src/platform/wiiu/shaders"
asm="$decaf/build/obj/latte-assembler"
if [ ! -x "$asm" ]; then
  if [ ! -d "$decaf/src/.git" ]; then
    rm -rf "$decaf/src"
    git init -q "$decaf/src"
    git -C "$decaf/src" remote add origin https://github.com/decaf-emu/decaf-emu.git
  fi
  git -C "$decaf/src" fetch -q --depth 1 origin "$rev"
  git -C "$decaf/src" checkout -q FETCH_HEAD
  for m in pugixml libbinrec spdlog cereal ovsocket gsl-lite addrlib excmd cnl catch cpp-peglib fmt glslang tomlplusplus imgui; do
    git -C "$decaf/src" submodule update -q --init --depth 1 "libraries/$m"
  done
  cmake -S "$decaf/src" -B "$decaf/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DDECAF_BUILD_TOOLS=ON -DDECAF_QT=OFF -DDECAF_VULKAN=OFF -DDECAF_FFMPEG=OFF -DDECAF_GIT_VERSION=OFF -DDECAF_PCH=OFF \
    -DCMAKE_CXX_FLAGS="-include cstdint -include cstring -include stdexcept -w"
  cmake --build "$decaf/build" --target latte-assembler -j"$(nproc)"
fi
for f in "$src"/*.psh; do "$asm" assemble --psh="$f" "${f%.psh}.gsh"; done
for f in "$src"/*.vsh; do "$asm" assemble --vsh="$f" "${f%.vsh}.gsh"; done
ls -la "$src"/*.gsh
