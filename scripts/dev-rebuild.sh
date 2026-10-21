#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export PSPDEV="${PSPDEV:-$root/_deps/pspdev}"
export PATH="$PSPDEV/bin:$PATH"
cmake --build "$root/build/desktop-psp" -j"$(nproc)" 2>&1 | grep -E "error|warning:" || true
cmake --build "$root/build/psp" -j"$(nproc)" 2>&1 | grep -E "error|warning:" || true
[ -d "$root/build/builder-native" ] || cmake -S "$root/tools/builder" -B "$root/build/builder-native" >/dev/null
cmake --build "$root/build/builder-native" -j"$(nproc)" 2>&1 | grep -E "error|warning:" || true
for d in "$root/_work/dist" "$root/dist"; do
  eboot="$d/PSP/GAME/OMORI/EBOOT.PBP"
  [ -f "$eboot" ] && "$root/build/builder-native/omori-builder" --update-engine "$eboot" --engine "$root/build/psp/EBOOT.PBP" >/dev/null
done
echo rebuilt
