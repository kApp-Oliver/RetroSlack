#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/host"
mkdir -p "$BUILD"
cmake -S "$ROOT/host/nse" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD" -j
echo "Built: $BUILD/nse"
echo "Test:  $BUILD/test_rhttp_codec"
