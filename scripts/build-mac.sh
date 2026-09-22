#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN="$ROOT/deps/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
BUILD="$ROOT/build/mac"

if [[ ! -f "$TOOLCHAIN" ]]; then
  if [[ -n "${RETRO68:-}" && -f "$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake" ]]; then
    TOOLCHAIN="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake"
  elif [[ -n "${RETRO68:-}" && -f "$RETRO68/cmake/retro68.toolchain.cmake" ]]; then
    TOOLCHAIN="$RETRO68/cmake/retro68.toolchain.cmake"
  else
    echo "Retro68 toolchain not found. Run ./scripts/bootstrap.sh first." >&2
    echo "Expected: $ROOT/deps/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake" >&2
    exit 1
  fi
fi

mkdir -p "$BUILD"
cmake -S "$ROOT/mac" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DRHTTP_DEBUG="${RHTTP_DEBUG:-OFF}"
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "Mac apps built under $BUILD"
ls -la "$BUILD"/*.bin "$BUILD"/*.dsk 2>/dev/null || ls -la "$BUILD"
