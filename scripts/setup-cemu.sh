#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) ;;
  *) echo "no prebuilt Cemu for $(uname -s) $(uname -m); install Cemu and pass CEMU=/path/to/Cemu"; exit 1 ;;
esac
dest="$root/_deps/cemu"
if [ ! -x "$dest/Cemu" ]; then
  tag="$(curl -fsSL https://api.github.com/repos/cemu-project/Cemu/releases/latest | sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' | head -1)"
  curl -fL -o "$root/_deps/cemu.zip" "https://github.com/cemu-project/Cemu/releases/download/v$tag/cemu-$tag-ubuntu-22.04-x64.zip"
  rm -rf "$dest" "$root/_deps/cemu-unpack"
  mkdir -p "$root/_deps/cemu-unpack"
  unzip -qo "$root/_deps/cemu.zip" -d "$root/_deps/cemu-unpack"
  inner="$(dirname "$(find "$root/_deps/cemu-unpack" -name Cemu -type f | head -1)")"
  mv "$inner" "$dest"
  rm -rf "$root/_deps/cemu-unpack" "$root/_deps/cemu.zip"
  chmod +x "$dest/Cemu"
fi
mkdir -p "$dest/portable"
echo "Cemu ready in $dest"
