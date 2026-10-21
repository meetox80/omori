#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
console="${1:?usage: setup-devkitpro.sh wii|3ds}"
dest="$root/_deps/devkitpro"
cache="$root/_deps/dkp-cache"
case "$console" in
  wii)
    want=(devkitPPC devkitppc-rules devkitppc-cmake ogc-cmake wii-cmake libogc libfat-ogc wii-pkg-config ppc-zlib ppc-libogg ppc-libvorbis
      ppc-libjpeg-turbo gamecube-tools general-tools dkp-cmake-common-utils)
    compiler="$dest/devkitPPC/bin/powerpc-eabi-gcc"
    ;;
  3ds)
    want=(devkitARM devkitarm-rules devkitarm-cmake libctru citro3d 3ds-cmake 3dstools picasso tex3ds 3ds-pkg-config 3ds-zlib 3ds-libogg
      3ds-libvorbisidec 3ds-libjpeg-turbo general-tools dkp-cmake-common-utils)
    compiler="$dest/devkitARM/bin/arm-none-eabi-gcc"
    ;;
  *) echo "unknown console $console (use wii or 3ds)" >&2; exit 1 ;;
esac
ua="pacman/6.1.0 (Linux x86_64) libalpm/14.0.0"
base="https://pkg.devkitpro.org/packages"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) host="linux/x86_64" ;;
  Linux-aarch64) host="linux/aarch64" ;;
  Darwin-x86_64) host="osx/x86_64" ;;
  Darwin-arm64) host="osx/arm64" ;;
  *) echo "no devkitPro packages for $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac

if [ -x "$compiler" ] && [ -f "$dest/.complete-$console" ]; then
  "$compiler" --version | head -1
  exit 0
fi

mkdir -p "$cache/db/libs" "$cache/db/host" "$dest"
fetch() { curl -fsSL -A "$ua" -o "$2" "$1"; }
fetch "$base/dkp-libs.db" "$cache/libs.db"
fetch "$base/$host/dkp-${host%%/*}.db" "$cache/host.db"
rm -rf "$cache/db/libs"/* "$cache/db/host"/*
tar xzf "$cache/libs.db" -C "$cache/db/libs"
tar xzf "$cache/host.db" -C "$cache/db/host"

declare -A where file deps
index() {
  local repo="$1" url="$2" d name
  for d in "$cache/db/$repo"/*/; do
    name="$(awk '/^%NAME%/{getline; print; exit}' "$d/desc")"
    [ -n "${where[$name]:-}" ] && continue
    where[$name]="$url"
    file[$name]="$(awk '/^%FILENAME%/{getline; print; exit}' "$d/desc")"
    deps[$name]="$(awk '/^%DEPENDS%/{p=1; next} /^%/{p=0} p && NF' "$d/desc" | sed -E 's/[<>=].*//' | tr '\n' ' ')"
  done
}
index host "$base/$host"
index libs "$base"

declare -A seen
order=()
visit() {
  local n="$1" d
  [ -n "${seen[$n]:-}" ] && return
  seen[$n]=1
  if [ -z "${where[$n]:-}" ]; then return; fi
  for d in ${deps[$n]}; do visit "$d"; done
  order+=("$n")
}
for n in "${want[@]}"; do
  if [ -z "${where[$n]:-}" ]; then echo "package $n not found" >&2; exit 1; fi
  visit "$n"
done

for n in "${order[@]}"; do
  f="${file[$n]}"
  if [ ! -s "$cache/$f" ]; then
    echo "fetching $f"
    fetch "${where[$n]}/$f" "$cache/$f.part"
    mv "$cache/$f.part" "$cache/$f"
  fi
  tar xf "$cache/$f" -C "$dest" --strip-components=2 --wildcards 'opt/devkitpro/*' 2>/dev/null || tar xf "$cache/$f" -C "$dest" --strip-components=2
done
touch "$dest/.complete-$console"
"$compiler" --version | head -1
