#!/usr/bin/env bash
# Resolve or clone the HelloMacintosh repo (loader + hellomacintosh CLI).
# Prints the absolute checkout path.
#
# Search order:
#   1. $HELLOMACINTOSH_DIR
#   2. $ROOT/HelloMacintosh
#   3. sibling ../HelloMacintosh
#
# Clone URL: $HELLOMACINTOSH_REPO (default: github.com/kApp-Oliver/HelloMacintosh)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_REPO="https://github.com/kApp-Oliver/HelloMacintosh.git"
REPO="${HELLOMACINTOSH_REPO:-$DEFAULT_REPO}"

is_hello_macintosh() {
  local d="$1"
  [[ -x "$d/scripts/hellomacintosh" || -f "$d/host/hellomacintosh/main.c" ]]
}

abs_dir() {
  (cd "$1" && pwd)
}

candidates=()
if [[ -n "${HELLOMACINTOSH_DIR:-}" ]]; then
  candidates+=("$HELLOMACINTOSH_DIR")
fi
candidates+=("$ROOT/HelloMacintosh")
candidates+=("$(dirname "$ROOT")/HelloMacintosh")

found=""
for d in "${candidates[@]}"; do
  if [[ -d "$d" ]] && is_hello_macintosh "$d"; then
    found="$(abs_dir "$d")"
    break
  fi
done

if [[ -n "$found" ]]; then
  echo "$found"
  exit 0
fi

if [[ "${1:-}" != "--clone" ]]; then
  echo "HelloMacintosh not found." >&2
  echo "It is a separate repo (loader + serial CLI). Clone it, then re-run:" >&2
  echo "  git clone $REPO \"$ROOT/HelloMacintosh\"" >&2
  echo "Or set HELLOMACINTOSH_DIR to an existing checkout." >&2
  echo "Docs: https://github.com/kApp-Oliver/HelloMacintosh" >&2
  exit 1
fi

dest="$ROOT/HelloMacintosh"
if [[ -n "${HELLOMACINTOSH_DIR:-}" ]]; then
  dest="$HELLOMACINTOSH_DIR"
fi
if [[ -e "$dest" && ! -d "$dest/.git" ]]; then
  echo "ERROR: $dest exists but is not a git checkout." >&2
  exit 1
fi
echo "Cloning HelloMacintosh into $dest ..." >&2
git clone "$REPO" "$dest"
abs_dir "$dest"
