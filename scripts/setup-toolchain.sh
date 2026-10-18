#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/_deps"
cd "$root/_deps"
case "$(uname -s)" in
  Darwin) asset="pspdev-macos-latest-$(uname -m).tar.gz" ;;
  *) asset="pspdev-ubuntu-latest-x86_64.tar.gz" ;;
esac
if [ ! -x pspdev/bin/psp-gcc ]; then
  curl -fL -o pspdev.tar.gz "https://github.com/pspdev/pspdev/releases/latest/download/$asset"
  tar xzf pspdev.tar.gz
fi
"$root/_deps/pspdev/bin/psp-gcc" --version | head -1
