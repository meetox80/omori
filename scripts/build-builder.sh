#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
zig="${ZIG:-$root/_deps/zig/zig}"
engines_dir="${ENGINES_DIR:-$root/build}"
engines=("psp/EBOOT.PBP:embedded_psp_eboot" "vita/eboot.bin:embedded_vita_eboot" "vita/param.sfo:embedded_vita_sfo"
  "3ds/omori.3dsx:embedded_3ds_engine" "wii/omori.dol:embedded_wii_dol" "ps3/EBOOT.BIN:embedded_ps3_eboot" "ps2/OMORI.ELF:embedded_ps2_elf"
  "xenon/xenon.elf:embedded_xenon_elf")
src="$root/tools/builder"
shared="$root/shared"
engine_src="$root/engine/src"
out="$root/build/builder"
gen="$out/gen"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
mkdir -p "$gen"
for pair in "${engines[@]}"; do
  [ -f "$engines_dir/${pair%%:*}" ] || { echo "engine not found: $engines_dir/${pair%%:*} (run make engines first)"; exit 1; }
done
cc -O2 -o "$gen/embed" "$src/tools/embed.c"
for pair in "${engines[@]}"; do
  "$gen/embed" "$engines_dir/${pair%%:*}" "$gen/${pair##*:}.c" "${pair##*:}"
done
fonts=("NotoSans-Fallback.ttf:fallback_noto_sans" "NotoSansSymbols2-Fallback.ttf:fallback_noto_symbols2" "NotoSansMath-Fallback.ttf:fallback_noto_math"
  "NotoSansArabic-Fallback.ttf:fallback_noto_arabic" "NotoSansSC-Fallback.otf:fallback_noto_sc")
for pair in "${fonts[@]}"; do
  "$gen/embed" "$src/third_party/fallback_fonts/${pair%%:*}" "$gen/${pair##*:}.c" "${pair##*:}"
done

builder_flags=(-D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -DMINIZ_NO_STDIO -DOMORI_BUILDER=1 -I"$src/src" -I"$src/third_party" -I"$shared"
  -I"$engine_src" -I"$src/third_party/libvpx" -I"$src/third_party/opus/include")
vpx_flags=(-std=c99 -D_DEFAULT_SOURCE -I"$src/third_party/libvpx" -w)
opus_flags=(-std=c99 -DOPUS_BUILD -DVAR_ARRAYS -DHAVE_LRINTF -I"$src/third_party/opus/include" -I"$src/third_party/opus/celt"
  -I"$src/third_party/opus/silk" -I"$src/third_party/opus/silk/float" -I"$src/third_party/opus/src" -w)
mapfile -t vpx_src < <(find "$src/third_party/libvpx" -name '*.c' | sort)
mapfile -t opus_src < <(find "$src/third_party/opus" -name '*.c' | sort)
c_src=("$src"/src/*.c "$src/third_party/miniz.c" "$shared"/rx.c "$shared"/js.c "$shared"/node.c "$shared"/buf.c "$shared"/script.c
  "$gen"/embedded_*.c "$gen"/fallback_noto_*.c)
cpp_src=("$src"/src/*.cpp "$engine_src"/js/value.cpp "$engine_src"/js/vm.cpp "$engine_src"/js/builtins.cpp "$engine_src"/js/random.cpp
  "$engine_src"/js/regexp.cpp "$engine_src"/js/compile.cpp "$engine_src"/js/dataview.cpp "$engine_src"/core/value.cpp)

targets=("${@:-x86_64-linux-musl aarch64-linux-musl x86_64-windows-gnu x86_64-macos aarch64-macos}")
for t in ${targets[@]}; do
  name="omori-builder-$t"
  libs="-lm"
  case "$t" in
    *windows*) name="$name.exe"; libs="-lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid -lcomctl32" ;;
    *linux*) libs="-lm -lpthread -static" ;;
  esac
  obj="$gen/obj-$t"
  rm -rf "$obj"
  mkdir -p "$obj"
  objects=()
  running=0
  failed=0
  compile() {
    local file="$1" tool="$2"
    shift 2
    local o="$obj/$(printf '%s' "${file#"$root"/}" | tr '/' '_').o"
    objects+=("$o")
    if [ "$running" -ge "$jobs" ]; then
      wait -n || failed=1
      running=$((running - 1))
    fi
    "$zig" "$tool" -target "$t" -O2 "$@" -c "$file" -o "$o" &
    running=$((running + 1))
  }
  for f in "${vpx_src[@]}"; do compile "$f" cc "${vpx_flags[@]}"; done
  for f in "${opus_src[@]}"; do compile "$f" cc "${opus_flags[@]}"; done
  for f in "${c_src[@]}"; do compile "$f" cc -std=c99 "${builder_flags[@]}"; done
  for f in "${cpp_src[@]}"; do compile "$f" c++ -std=c++17 "${builder_flags[@]}"; done
  while [ "$running" -gt 0 ]; do
    wait -n || failed=1
    running=$((running - 1))
  done
  [ "$failed" -eq 0 ] || { echo "compiling the builder for $t failed"; exit 1; }
  "$zig" c++ -target "$t" -s "${objects[@]}" -o "$out/$name" $libs
  rm -f "$out/${name%.exe}.pdb"
  echo "built $out/$name"
done
