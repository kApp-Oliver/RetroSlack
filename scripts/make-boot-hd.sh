#!/usr/bin/env bash
# Convert System 7.0.1 Disk Tools (HFS volume) into a Snow/BlueSCSI device image
# so a Mac Plus can boot from SCSI (Plus has 800K drives; Garden Disk Tools is 1.44MB).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/assets/disks/macos701/MacOS701/Disk Tools.img}"
OUT="${2:-$ROOT/assets/disks/System701-Boot.hda}"
PAD1440="$ROOT/assets/disks/DiskTools-1440.img"
DJJR="${DJJR:-$ROOT/deps/djjr/bin/djjr}"

if [[ ! -f "$SRC" ]]; then
  echo "ERROR: Missing Disk Tools volume: $SRC" >&2
  echo "Extract assets/disks/MacOS701.sit first (run.sh does this with unar)." >&2
  exit 1
fi

if [[ ! -x "$DJJR" ]]; then
  echo "ERROR: Disk Jockey Jr not found at $DJJR" >&2
  echo "Download: https://diskjockey.onegeekarmy.eu/djjr/" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
"$DJJR" convert to-device "$SRC" "$OUT"
echo "Boot HD: $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"

# Optional: exact 1440 KiB raw for SuperDrive override / Classic / SE FDHD floppy boot
python3 - "$SRC" "$PAD1440" <<'PY'
import sys
from pathlib import Path
src, dst = Path(sys.argv[1]), Path(sys.argv[2])
data = src.read_bytes()
need = 1440 * 1024
if len(data) > need:
    raise SystemExit(f"Disk Tools larger than 1440K: {len(data)}")
dst.write_bytes(data + bytes(need - len(data)))
print(f"Padded floppy: {dst} ({need} bytes)")
PY

echo "Load assets/snow/RetroSlack.snoww (SCSI #6 → $OUT)."
