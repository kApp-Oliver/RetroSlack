#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$ROOT/arduino/nse"

if [[ ! -f "$PROJ/secrets.h" ]]; then
  echo "Creating secrets.h from example (edit token before flash)..."
  cp "$PROJ/secrets.h.example" "$PROJ/secrets.h"
fi

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO CLI (pio) not found." >&2
  echo "Install: brew install platformio  OR  pip install platformio" >&2
  echo "Firmware sources are in $PROJ — open in PlatformIO when ready." >&2
  exit 0
fi

# Symlink/copy protocol sources into build include path is via -I in platformio.ini
# Also compile codec as part of src:
mkdir -p "$PROJ/src"
if [[ ! -e "$PROJ/src/rhttp_codec.c" ]]; then
  ln -sf ../../../protocol/rhttp_codec.c "$PROJ/src/rhttp_codec.c"
fi

cd "$PROJ"
pio run -e "${PIO_ENV:-esp32_w5500}"
