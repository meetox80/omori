#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="ps3dev-linux-X64.tar.gz" ;;
  Darwin-arm64) asset="ps3dev-macos-ARM64.tar.gz" ;;
  Darwin-x86_64) asset="ps3dev-macos-X64.tar.gz" ;;
  *) echo "no prebuilt ps3dev for $(uname -s) $(uname -m); install ps3toolchain and pass PS3DEV=/path/to/ps3dev"; exit 1 ;;
esac
export PS3DEV="$root/_deps/ps3dev"
export PSL1GHT="$PS3DEV/psl1ght"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
portlibs="$PS3DEV/portlibs/ppu"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ ! -x "$PS3DEV/ppu/bin/ppu-gcc" ]; then
  mkdir -p "$PS3DEV"
  tag="$(curl -fsSL "https://api.github.com/repos/ps3dev/ps3toolchain/releases?per_page=1" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -1)"
  curl -fL -o "$tmp/ps3dev.tar.gz" "https://github.com/ps3dev/ps3toolchain/releases/download/$tag/$asset"
  tar -xzf "$tmp/ps3dev.tar.gz" -C "$PS3DEV" --strip-components=1
fi

if [ ! -f "$PSL1GHT/ppu_rules" ]; then
  git clone -q --depth 1 https://github.com/ps3dev/PSL1GHT "$tmp/psl1ght"
  (cd "$tmp/psl1ght" && make install-ctrl && make -j"$jobs" && make install) >"$tmp/psl1ght.log" 2>&1 || { tail -40 "$tmp/psl1ght.log"; exit 1; }
fi

fetch() { curl -fsSL -o "$tmp/$2" "$1" && tar -xf "$tmp/$2" -C "$tmp"; }
cross=(--host=powerpc64-ps3-elf --prefix="$portlibs" --disable-shared --enable-static)
export CFLAGS="-O2 -mcpu=cell"

if [ ! -f "$portlibs/lib/libz.a" ]; then
  fetch https://zlib.net/fossils/zlib-1.3.1.tar.gz zlib.tar.gz
  (cd "$tmp/zlib-1.3.1" && CHOST=powerpc64-ps3-elf CC=ppu-gcc AR=ppu-ar RANLIB=ppu-ranlib ./configure --prefix="$portlibs" --static && make -j"$jobs" libz.a && make install) >"$tmp/zlib.log" 2>&1 || { tail -40 "$tmp/zlib.log"; exit 1; }
fi
if [ ! -f "$portlibs/lib/libogg.a" ]; then
  fetch https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.gz ogg.tar.gz
  (cd "$tmp/libogg-1.3.5" && CC=ppu-gcc AR=ppu-ar RANLIB=ppu-ranlib ./configure "${cross[@]}" && make -j"$jobs" && make install) >"$tmp/ogg.log" 2>&1 || { tail -40 "$tmp/ogg.log"; exit 1; }
fi
if [ ! -f "$portlibs/lib/libvorbisfile.a" ]; then
  fetch https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz vorbis.tar.gz
  (cd "$tmp/libvorbis-1.3.7" && CC=ppu-gcc AR=ppu-ar RANLIB=ppu-ranlib OGG_CFLAGS="-I$portlibs/include" OGG_LIBS="-L$portlibs/lib -logg" ./configure "${cross[@]}" --disable-oggtest --disable-docs --disable-examples && make -j"$jobs" && make install) >"$tmp/vorbis.log" 2>&1 || { tail -40 "$tmp/vorbis.log"; exit 1; }
fi
"$PS3DEV/ppu/bin/ppu-gcc" --version | head -1
