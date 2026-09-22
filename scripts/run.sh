#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$ROOT/ROMS/Mac-Plus.ROM"
ROM_RAW="$ROOT/ROMS/Mac-Plus-128k.ROM"
NSE="$ROOT/build/host/nse"
SNOW_BIN="$ROOT/deps/snow/Snow.app/Contents/MacOS/Snow"
BOOT_FLOPPY="${SNOW_BOOT:-$ROOT/assets/disks/boot/DiskTools.img}"
APP_FLOPPY="${SNOW_FLOPPY:-$ROOT/build/mac/RetroSlack.dsk}"
BOOT_HD="${SNOW_BOOT_HD:-$ROOT/assets/disks/System701-Boot.hda}"
WORKSPACE="${SNOW_WORKSPACE:-$ROOT/assets/snow/RetroSlack.snoww}"
TCP_PORT="${NSE_TCP_PORT:-1984}"
SERIAL_SPEC="${NSE_SERIAL:-tcp://127.0.0.1:${TCP_PORT}}"
NSE_PID=""
SNOW_PID=""

cleanup() {
  if [[ -n "$NSE_PID" ]] && kill -0 "$NSE_PID" 2>/dev/null; then
    kill "$NSE_PID" 2>/dev/null || true
  fi
  if [[ -n "${SNOW_OWNED:-}" && -n "$SNOW_PID" ]] && kill -0 "$SNOW_PID" 2>/dev/null; then
    kill "$SNOW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$ROOT/scripts/bootstrap.sh" --check-rom

if [[ ! -x "$NSE" ]]; then
  "$ROOT/scripts/build-host.sh"
fi

# Normalize ROM to raw 128 KiB Mac Plus v3 (Snow requires known SHA-256)
if [[ ! -f "$ROM_RAW" ]] || [[ "$ROM" -nt "$ROM_RAW" ]]; then
  python3 - "$ROM" "$ROM_RAW" <<'PY'
import hashlib, sys
from pathlib import Path
src, dst = Path(sys.argv[1]), Path(sys.argv[2])
data = src.read_bytes()
raw = data[:131072] if len(data) >= 131072 else data
digest = hashlib.sha256(raw).hexdigest()
# Macintosh Plus v3 (Snow allowlist)
expected = "dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7"
dst.write_bytes(raw)
print(f"ROM raw: {dst} ({len(raw)} bytes) sha256={digest[:16]}…")
if len(raw) == 131072 and digest != expected:
    print("WARNING: trimmed ROM is not a known Snow Mac Plus hash; Snow may reject it.", file=sys.stderr)
    print("  Expected Plus v3:", expected[:16] + "…", file=sys.stderr)
elif digest == expected:
    print("ROM OK: Macintosh Plus v3 (Snow-supported)")
PY
fi
USE_ROM="$ROM_RAW"

if [[ -f "$ROOT/.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$ROOT/.env"
  set +a
fi

if [[ ! -x "$SNOW_BIN" ]]; then
  echo "Snow binary not found — run ./scripts/bootstrap.sh" >&2
  exit 1
fi

# Extract System 7.0.1 .sit once if needed
SIT="$ROOT/assets/disks/MacOS701.sit"
EXTRACTED="$ROOT/assets/disks/macos701/MacOS701/Disk Tools.img"
if [[ -f "$SIT" && ! -f "$EXTRACTED" ]]; then
  if command -v unar >/dev/null 2>&1; then
    echo "Extracting $SIT ..."
    mkdir -p "$ROOT/assets/disks/macos701"
    unar -f -o "$ROOT/assets/disks/macos701" "$SIT"
  else
    echo "Install unar (brew install unar) to extract MacOS701.sit" >&2
  fi
fi

# Plus only has 800K drives; MacOS701 Disk Tools is 1.44MB and will not boot as a floppy.
# Convert Disk Tools → Snow SCSI device image (APM + driver + HFS) for HD boot.
if [[ ! -f "$BOOT_HD" ]]; then
  if [[ -f "$EXTRACTED" ]]; then
    "$ROOT/scripts/make-boot-hd.sh" "$EXTRACTED" "$BOOT_HD"
  else
    echo "WARNING: No boot HD at $BOOT_HD and no Disk Tools to convert." >&2
  fi
fi

# Prefer workspace (ROM + SCSI boot HD + app floppy). Fall back to ROM CLI.
FLOPPY_ARGS=()
SNOW_LAUNCH=()
if [[ -f "$WORKSPACE" && -f "$BOOT_HD" ]]; then
  SNOW_LAUNCH=("$WORKSPACE")
  echo "Workspace: $WORKSPACE"
  echo "Boot HD:   $BOOT_HD (Disk Tools via SCSI)"
  if [[ -f "$APP_FLOPPY" ]]; then
    echo "App floppy (via workspace): $APP_FLOPPY"
  fi
elif [[ -f "$BOOT_HD" ]]; then
  SNOW_LAUNCH=("$USE_ROM")
  echo "WARNING: Missing $WORKSPACE — launching ROM only." >&2
  echo "  Attach boot HD in Snow: Drives → SCSI #6 → Load → $BOOT_HD" >&2
  echo "  Then reset the emulated Mac." >&2
else
  SNOW_LAUNCH=("$USE_ROM")
  if [[ -f "$APP_FLOPPY" ]]; then
    FLOPPY_ARGS+=(--floppy "$APP_FLOPPY")
  fi
  echo "WARNING: No SCSI boot HD — expect flashing ? disk until System is available." >&2
fi

echo "Launching Snow..."
echo "  ROM: $USE_ROM"
SNOW_OWNED=1
# macOS /bin/bash 3.2 + set -u: empty "${arr[@]}" is an unbound-variable error.
if [[ ${#FLOPPY_ARGS[@]} -gt 0 ]]; then
  "$SNOW_BIN" "${SNOW_LAUNCH[@]}" \
    --serial-bridge-a "tcp:${TCP_PORT}" \
    "${FLOPPY_ARGS[@]}" &
else
  "$SNOW_BIN" "${SNOW_LAUNCH[@]}" \
    --serial-bridge-a "tcp:${TCP_PORT}" &
fi
SNOW_PID=$!

for _ in $(seq 1 50); do
  if nc -z 127.0.0.1 "$TCP_PORT" 2>/dev/null; then
    break
  fi
  sleep 0.2
done

echo "Starting NSE on $SERIAL_SPEC (token=${SLACK_BOT_TOKEN:+yes})..."
"$NSE" --serial "$SERIAL_SPEC" --verbose &
NSE_PID=$!

echo "Snow pid=$SNOW_PID  NSE pid=$NSE_PID  — Ctrl+C stops NSE/Snow"
wait "$NSE_PID" || true
