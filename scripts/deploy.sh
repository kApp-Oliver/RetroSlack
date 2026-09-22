#!/usr/bin/env bash
# Stream a Retro68 MacBinary to a Plus running HelloMacintosh, then remind
# the operator to start NSE on the same serial for Slack.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${DEPLOY_BIN:-$ROOT/build/mac/RetroSlack.bin}"
SERIAL="${HM_SERIAL:-${NSE_SERIAL:-}}"
SNOW=0

usage() {
  echo "Usage: $0 [--snow] [--serial DEV] [FILE.bin]" >&2
  echo "  Default FILE: build/mac/RetroSlack.bin" >&2
  echo "  Snow:         --snow  (tcp://127.0.0.1:1984)" >&2
  echo "  Hardware:     --serial /dev/cu.usbserial-…  or \$HM_SERIAL" >&2
  echo "HelloMacintosh must be frontmost on the Plus. Stop NSE first." >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --snow) SNOW=1; shift ;;
    --serial)
      [[ $# -ge 2 ]] || usage
      SERIAL="$2"
      shift 2
      ;;
    -h|--help) usage ;;
    *)
      BIN="$1"
      shift
      ;;
  esac
done

if [[ ! -f "$BIN" ]]; then
  echo "Building Mac apps..."
  "$ROOT/scripts/build-mac.sh"
fi
if [[ ! -f "$BIN" ]]; then
  echo "ERROR: missing $BIN" >&2
  exit 1
fi

HM_ARGS=()
if [[ "$SNOW" -eq 1 ]]; then
  HM_ARGS+=(--serial tcp://127.0.0.1:1984)
elif [[ -n "$SERIAL" ]]; then
  HM_ARGS+=(--serial "$SERIAL")
fi

echo "Deploying $(basename "$BIN") via HelloMacintosh..."
# macOS /bin/bash 3.2 + set -u: empty "${arr[@]}" is an unbound-variable error.
if [[ ${#HM_ARGS[@]} -gt 0 ]]; then
  "$ROOT/scripts/hellomacintosh" "${HM_ARGS[@]}" run "$BIN"
else
  "$ROOT/scripts/hellomacintosh" run "$BIN"
fi

echo
echo "Guest is launching. Start NSE on the same serial for Slack:"
if [[ "$SNOW" -eq 1 ]]; then
  echo "  ./build/host/nse --serial tcp://127.0.0.1:1984 --verbose"
elif [[ -n "$SERIAL" ]]; then
  echo "  ./build/host/nse --serial $SERIAL --baud 19200 --verbose"
else
  echo "  ./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose"
fi
echo "Stop NSE before the next hellomacintosh stop/run (one process owns USB)."
