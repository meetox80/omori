#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version=4.3.3
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="tailwindcss-linux-x64" ;;
  Linux-aarch64) asset="tailwindcss-linux-arm64" ;;
  Darwin-arm64) asset="tailwindcss-macos-arm64" ;;
  Darwin-x86_64) asset="tailwindcss-macos-x64" ;;
  *) echo "no prebuilt tailwindcss for $(uname -s) $(uname -m); install tailwindcss $version and pass TAILWIND=/path/to/tailwindcss"; exit 1 ;;
esac
mkdir -p "$root/_deps"
if [ ! -x "$root/_deps/tailwindcss" ]; then
  curl -fL -o "$root/_deps/tailwindcss" "https://github.com/tailwindlabs/tailwindcss/releases/download/v$version/$asset"
  chmod +x "$root/_deps/tailwindcss"
fi
"$root/_deps/tailwindcss" --help | head -n 1
