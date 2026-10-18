#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version=34
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="wasi-sdk-$version.0-x86_64-linux" ;;
  Linux-aarch64) asset="wasi-sdk-$version.0-arm64-linux" ;;
  Darwin-arm64) asset="wasi-sdk-$version.0-arm64-macos" ;;
  Darwin-x86_64) asset="wasi-sdk-$version.0-x86_64-macos" ;;
  *) echo "no prebuilt wasi-sdk for $(uname -s) $(uname -m); install wasi-sdk $version and pass WASI_SDK=/path/to/wasi-sdk"; exit 1 ;;
esac
mkdir -p "$root/_deps"
if [ ! -x "$root/_deps/wasi-sdk/bin/clang" ]; then
  curl -fL -o "$root/_deps/wasi-sdk.tar.gz" "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$version/$asset.tar.gz"
  mkdir -p "$root/_deps/wasi-sdk"
  tar -xzf "$root/_deps/wasi-sdk.tar.gz" -C "$root/_deps/wasi-sdk" --strip-components=1
  rm -f "$root/_deps/wasi-sdk.tar.gz"
fi
"$root/_deps/wasi-sdk/bin/clang" --version | head -n 1
