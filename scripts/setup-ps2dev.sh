#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/_deps"
cd "$root/_deps"
case "$(uname -s)-$(uname -m)" in
  Darwin-x86_64) asset="ps2dev-macos-15-intel.tar.gz" ;;
  Darwin-*) asset="ps2dev-macos-latest.tar.gz" ;;
  Linux-aarch64) asset="ps2dev-ubuntu-24.04-arm.tar.gz" ;;
  *) asset="ps2dev-ubuntu-latest.tar.gz" ;;
esac
if [ ! -x ps2dev/ee/bin/mips64r5900el-ps2-elf-gcc ]; then
  curl -fL -o ps2dev.tar.gz "https://github.com/ps2dev/ps2dev/releases/latest/download/$asset"
  tar xzf ps2dev.tar.gz
  rm -f ps2dev.tar.gz
fi
"$root/_deps/ps2dev/ee/bin/mips64r5900el-ps2-elf-gcc" --version | head -1
ports="$root/_deps/ps2dev/ps2sdk/ports"
tremor_rev=820fb3237ea81af44c9cc468c8b4e20128e3e5ad
if [ ! -f "$ports/lib/libvorbisidec.a" ] || [ ! -f "$ports/include/tremor/ivorbisfile.h" ]; then
  work="$(mktemp -d)"
  trap 'rm -rf "$work"' EXIT
  curl -fL -o "$work/tremor.tar.gz" "https://gitlab.xiph.org/xiph/tremor/-/archive/$tremor_rev/tremor-$tremor_rev.tar.gz"
  tar xzf "$work/tremor.tar.gz" -C "$work"
  src="$work/tremor-$tremor_rev"
  ee="$root/_deps/ps2dev/ee/bin/mips64r5900el-ps2-elf"
  sdk="$root/_deps/ps2dev/ps2sdk"
  objs=()
  for f in block codebook floor0 floor1 info mapping0 mdct registry res012 sharedbook synthesis vorbisfile window; do
    "$ee-gcc" -O2 -G0 -DNDEBUG -Wno-incompatible-pointer-types -I"$ports/include" -I"$sdk/ee/include" -I"$sdk/common/include" -I"$src" -c "$src/$f.c" -o "$work/$f.o"
    objs+=("$work/$f.o")
  done
  "$ee-ar" rcs "$ports/lib/libvorbisidec.a" "${objs[@]}"
  mkdir -p "$ports/include/tremor"
  cp "$src/ivorbiscodec.h" "$src/ivorbisfile.h" "$ports/include/tremor/"
fi
