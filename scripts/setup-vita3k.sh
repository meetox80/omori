#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="ubuntu-latest.zip" ;;
  Linux-aarch64) asset="ubuntu-aarch64-latest.zip" ;;
  *) echo "no prebuilt Vita3K for $(uname -s) $(uname -m); install Vita3K and pass VITA3K=/path/to/Vita3K"; exit 1 ;;
esac
mkdir -p "$root/_deps"
if [ ! -x "$root/_deps/vita3k/Vita3K" ]; then
  curl -fL -o "$root/_deps/vita3k.zip" "https://github.com/Vita3K/Vita3K/releases/download/continuous/$asset"
  mkdir -p "$root/_deps/vita3k"
  unzip -qo "$root/_deps/vita3k.zip" -d "$root/_deps/vita3k"
  rm -f "$root/_deps/vita3k.zip"
  chmod +x "$root/_deps/vita3k/Vita3K"
fi
mkdir -p "$root/_deps/vita3k/portable"
"$root/_deps/vita3k/Vita3K" -v | tail -1
