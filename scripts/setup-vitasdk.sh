#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/_deps"
export VITASDK="$root/_deps/vitasdk"
if [ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl -fL -o "$tmp/bootstrap-vitasdk.sh" https://raw.githubusercontent.com/vitasdk/vdpm/master/bootstrap-vitasdk.sh
  bash "$tmp/bootstrap-vitasdk.sh" --install-dir "$VITASDK"
fi
VDPM_NONINTERACTIVE=1 "$VITASDK/bin/vdpm" install zlib libogg libvorbis
"$VITASDK/bin/arm-vita-eabi-gcc" --version | head -1
