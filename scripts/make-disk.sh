#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/assets/disks"
FLOPPY="$OUT_DIR/RetroSlack-800K.dsk"
BUILD_MAC="$ROOT/build/mac"

mkdir -p "$OUT_DIR"

if [[ ! -f "$BUILD_MAC/RetroSlack.bin" ]]; then
  echo "Building Mac apps first..."
  "$ROOT/scripts/build-mac.sh"
fi

if ! command -v hformat >/dev/null 2>&1; then
  echo "hfsutils required (brew install hfsutils)" >&2
  exit 1
fi

copy_one_dsk() {
  local src="$1"
  local dst="$2"
  if [[ -f "$src" ]]; then
    cp -f "$src" "$dst"
    echo "Copied $(basename "$src") -> $dst"
  fi
}

copy_one_dsk "$BUILD_MAC/RetroSlack.dsk" "$FLOPPY"
copy_one_dsk "$BUILD_MAC/RHTTPTest.dsk" "$OUT_DIR/RHTTPTest-800K.dsk"

hcopy_bin() {
  local bin="$1"
  local dest="$2"
  if [[ -f "$bin" ]]; then
    hcopy -m "$bin" "$dest" || hcopy -r "$bin" "$dest" || true
  fi
}

if [[ -f "$FLOPPY" ]]; then
  ls -la "$FLOPPY"
  if [[ -f "$BUILD_MAC/RetroSlack.dsk" ]]; then
    exit 0
  fi
fi

# Fallback: create HFS floppy and hcopy MacBinary/AppleDouble
dd if=/dev/zero of="$FLOPPY" bs=1024 count=800 status=none
hformat -l RetroSlack "$FLOPPY" >/dev/null
hmount "$FLOPPY"
trap 'humount || true' EXIT
hmkdir Apps || true
hcopy_bin "$BUILD_MAC/RetroSlack.bin" :Apps/RetroSlack
hcopy_bin "$BUILD_MAC/RHTTPTest.bin" :Apps/RHTTPTest
hls :Apps || hls
echo "Wrote $FLOPPY"
