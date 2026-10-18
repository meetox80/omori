#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version=0.13.0
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="zig-linux-x86_64-$version" ;;
  Linux-aarch64) asset="zig-linux-aarch64-$version" ;;
  Darwin-arm64) asset="zig-macos-aarch64-$version" ;;
  Darwin-x86_64) asset="zig-macos-x86_64-$version" ;;
  *) echo "no prebuilt zig for $(uname -s) $(uname -m); install zig $version and pass ZIG=/path/to/zig"; exit 1 ;;
esac
mkdir -p "$root/_deps"
if [ ! -x "$root/_deps/zig/zig" ]; then
  curl -fL -o "$root/_deps/zig.tar.xz" "https://ziglang.org/download/$version/$asset.tar.xz"
  mkdir -p "$root/_deps/zig"
  tar -xJf "$root/_deps/zig.tar.xz" -C "$root/_deps/zig" --strip-components=1
  rm -f "$root/_deps/zig.tar.xz"
fi
"$root/_deps/zig/zig" version
