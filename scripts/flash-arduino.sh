#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$ROOT/arduino/nse"

"$ROOT/scripts/build-arduino.sh"

if ! command -v pio >/dev/null 2>&1; then
  exit 1
fi

cd "$PROJ"
pio run -e "${PIO_ENV:-esp32_w5500}" -t upload
echo "Flash complete. Monitor: pio device monitor -b 115200"
