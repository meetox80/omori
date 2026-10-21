#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sdk="${WASI_SDK:-$root/_deps/wasi-sdk}"
tailwind="${TAILWIND:-$root/_deps/tailwindcss}"
engines_dir="${ENGINES_DIR:-$root/build}"
engines=("psp/EBOOT.PBP:psp" "vita/eboot.bin:vita" "vita/param.sfo:vita" "3ds/omori.3dsx:3ds" "wii/omori.dol:wii" "ps3/EBOOT.BIN:ps3"
  "ps2/OMORI.ELF:ps2" "xenon/xenon.elf:xbox360")
src="$root/tools/builder"
shared="$root/shared"
engine_src="$root/engine/src"
out="$root/build/web"
gen="$root/build/web-gen"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
for pair in "${engines[@]}"; do
  [ -f "$engines_dir/${pair%%:*}" ] || { echo "engine not found: $engines_dir/${pair%%:*} (run make engines first)"; exit 1; }
done
[ -x "$sdk/bin/clang" ] || { echo "wasi-sdk not found: $sdk (run scripts/setup-wasi-sdk.sh first)"; exit 1; }
[ -x "$tailwind" ] || { echo "tailwindcss not found: $tailwind (run scripts/setup-tailwind.sh first)"; exit 1; }
mkdir -p "$out" "$gen"
find "$out" -mindepth 1 -delete
rm -rf "$gen/obj"
mkdir -p "$gen/obj"
cc -O2 -o "$gen/embed" "$src/tools/embed.c"
fonts=("NotoSans-Fallback.ttf:fallback_noto_sans" "NotoSansSymbols2-Fallback.ttf:fallback_noto_symbols2" "NotoSansMath-Fallback.ttf:fallback_noto_math"
  "NotoSansArabic-Fallback.ttf:fallback_noto_arabic" "NotoSansSC-Fallback.otf:fallback_noto_sc")
for pair in "${fonts[@]}"; do
  "$gen/embed" "$src/third_party/fallback_fonts/${pair%%:*}" "$gen/${pair##*:}.c" "${pair##*:}"
done

target=(--target=wasm32-wasip1 --sysroot="$sdk/share/wasi-sysroot" -O2 -msimd128 -D_WASI_EMULATED_PROCESS_CLOCKS)
eh=(-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false)
sjlj=(-mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false)
builder_flags=(-D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -DMINIZ_NO_STDIO -DOMORI_BUILDER=1 -DOMORI_EXTERNAL_ENGINES -I"$src/src" -I"$src/third_party" -I"$shared"
  -I"$engine_src" -I"$src/third_party/libvpx" -I"$src/third_party/opus/include")
vpx_flags=(-std=c99 "${sjlj[@]}" -D_DEFAULT_SOURCE -I"$src/third_party/libvpx" -w)
opus_flags=(-std=c99 -DOPUS_BUILD -DVAR_ARRAYS -DHAVE_LRINTF -I"$src/third_party/opus/include" -I"$src/third_party/opus/celt"
  -I"$src/third_party/opus/silk" -I"$src/third_party/opus/silk/float" -I"$src/third_party/opus/src" -w)

objects=()
running=0
failed=0
compile() {
  local file="$1" compiler="$2"
  shift 2
  local obj="$gen/obj/$(printf '%s' "${file#"$root"/}" | tr '/' '_').o"
  objects+=("$obj")
  if [ "$running" -ge "$jobs" ]; then
    wait -n || failed=1
    running=$((running - 1))
  fi
  "$compiler" "${target[@]}" "$@" -c "$file" -o "$obj" &
  running=$((running + 1))
}
while IFS= read -r f; do compile "$f" "$sdk/bin/clang" "${vpx_flags[@]}"; done <<< "$(find "$src/third_party/libvpx" -name '*.c' | sort)"
while IFS= read -r f; do compile "$f" "$sdk/bin/clang" "${opus_flags[@]}"; done <<< "$(find "$src/third_party/opus" -name '*.c' | sort)"
for f in "$src"/src/*.c "$src/third_party/miniz.c" "$shared"/rx.c "$shared"/js.c "$shared"/node.c "$shared"/buf.c "$shared"/script.c \
  "$gen"/fallback_noto_*.c; do
  quiet=()
  [ "${f##*/}" = stb_vorbis_impl.c ] && quiet=(-w)
  compile "$f" "$sdk/bin/clang" -std=c99 "${sjlj[@]}" "${builder_flags[@]}" "${quiet[@]}"
done
for f in "$src"/src/*.cpp "$engine_src"/js/value.cpp "$engine_src"/js/vm.cpp "$engine_src"/js/builtins.cpp "$engine_src"/js/random.cpp \
  "$engine_src"/js/regexp.cpp "$engine_src"/js/compile.cpp "$engine_src"/js/dataview.cpp "$engine_src"/core/value.cpp; do
  compile "$f" "$sdk/bin/clang++" -std=c++17 "${eh[@]}" "${builder_flags[@]}"
done
while [ "$running" -gt 0 ]; do
  wait -n || failed=1
  running=$((running - 1))
done
[ "$failed" -eq 0 ] || { echo "compiling the builder for the web failed"; exit 1; }
"$sdk/bin/clang++" "${target[@]}" -fwasm-exceptions "${objects[@]}" -lunwind -lsetjmp -lwasi-emulated-process-clocks -Wl,--max-memory=4294967296 \
  -Wl,-z,stack-size=8388608 -s -o "$out/omori-builder.wasm"
cp -r "$root"/web/. "$out/"
sizes="\"omori-builder.wasm\": $(wc -c < "$out/omori-builder.wasm" | tr -d ' ')"
for pair in "${engines[@]}"; do
  file="${pair%%:*}"
  dest="engines/${pair##*:}/${file##*/}"
  mkdir -p "$out/${dest%/*}"
  cp "$engines_dir/$file" "$out/$dest"
  sizes+=", \"$dest\": $(wc -c < "$out/$dest" | tr -d ' ')"
done
printf '{%s}\n' "$sizes" > "$out/files.json"
"$tailwind" -i "$root/web/style.css" -o "$out/style.css" --minify
ls -la "$out"
