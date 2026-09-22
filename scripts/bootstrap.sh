#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
ROM="$ROOT/ROMS/Mac-Plus.ROM"
HFS_PREFIX="$DEPS/hfsutils-prefix"
HELLOMACINTOSH_REPO="${HELLOMACINTOSH_REPO:-https://github.com/kApp-Oliver/HelloMacintosh.git}"

check_rom() {
  if [[ ! -f "$ROM" ]]; then
    echo "ERROR: Missing ROM at $ROM" >&2
    echo "Place a Macintosh Plus ROM there (gitignored). Never commit ROMs." >&2
    return 1
  fi
  local sz
  sz=$(wc -c < "$ROM" | tr -d ' ')
  echo "ROM ok: $ROM ($sz bytes)"
  if [[ "$sz" -ne 131072 ]]; then
    echo "NOTE: Snow only accepts raw 128 KiB Plus dumps (known SHA-256)." >&2
    echo "      run.sh will trim larger dumps to ROMS/Mac-Plus-128k.ROM (first 128 KiB)." >&2
  fi
}

if [[ "${1:-}" == "--check-rom" ]]; then
  check_rom
  exit 0
fi

echo "== RetroSlack bootstrap =="
echo "Secrets (.env, arduino/nse/secrets.h) and Apple ROM/System images stay local."

if [[ ! -f "$ROM" ]]; then
  echo "NOTE: no ROM at $ROM — Snow emulator path will not run until you add one."
  echo "      Real Plus + serial deploy does not need a ROM in this tree."
else
  check_rom || true
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Install from https://brew.sh then re-run." >&2
  exit 1
fi

echo "-- Homebrew packages --"
brew_pkgs=(cmake ninja curl hfsutils boost bison gmp mpfr libmpc flex texinfo pkg-config)
for p in "${brew_pkgs[@]}"; do
  if brew list --versions "$p" >/dev/null 2>&1; then
    echo "  $p already installed"
  else
    brew install "$p"
  fi
done

mkdir -p "$DEPS/bin"
if [[ ! -x "$DEPS/bin/nproc" ]]; then
  printf '#!/bin/sh\nexec sysctl -n hw.ncpu\n' > "$DEPS/bin/nproc"
  chmod +x "$DEPS/bin/nproc"
fi
export PATH="$DEPS/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:/opt/homebrew/bin:$PATH"

echo "-- HelloMacintosh (loader + serial CLI, separate repo) --"
HM="$("$ROOT/scripts/hello-macintosh-dir.sh" --clone)"
echo "  checkout: $HM"
echo "  upstream: $HELLOMACINTOSH_REPO"

echo "-- libhfs (developer libs for Retro68) --"
if [[ ! -f "$HFS_PREFIX/include/hfs.h" || ! -f "$HFS_PREFIX/lib/libhfs.a" ]]; then
  echo "  Building libhfs into $HFS_PREFIX ..."
  "$ROOT/scripts/build-libhfs.sh"
else
  echo "  libhfs present"
fi

echo "-- Snow emulator --"
SNOW_DIR="$DEPS/snow"
SNOW_APP="$SNOW_DIR/Snow.app"
if [[ -d "$SNOW_APP" ]]; then
  echo "  Snow.app present"
else
  mkdir -p "$SNOW_DIR"
  TMPZIP="$SNOW_DIR/Snow.MacOS.zip"
  echo "  Downloading Snow macOS universal build..."
  if curl -fsSL -L -o "$TMPZIP" \
    "https://github.com/twvd/snow/releases/latest/download/Snow.MacOS.App.Bundle.universal.zip"; then
    unzip -qo "$TMPZIP" -d "$SNOW_DIR"
    rm -f "$TMPZIP"
    if [[ ! -d "$SNOW_APP" ]]; then
      FOUND=$(find "$SNOW_DIR" -maxdepth 3 -name 'Snow.app' -type d | head -1 || true)
      if [[ -n "$FOUND" && "$FOUND" != "$SNOW_APP" ]]; then
        mv "$FOUND" "$SNOW_APP"
      fi
    fi
    echo "  Snow installed at $SNOW_APP"
  else
    echo "  WARNING: could not download Snow. Install manually from https://snowemu.com/ into deps/snow/" >&2
  fi
fi

echo "-- Retro68 (68K toolchain) --"
RETRO68_SRC="$DEPS/Retro68"
RETRO68_BUILD="$DEPS/Retro68-build"
GCC68="$RETRO68_BUILD/toolchain/bin/m68k-apple-macos-gcc"
CMAKE68="$RETRO68_BUILD/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
if [[ -x "$GCC68" && -f "$CMAKE68" ]]; then
  echo "  Retro68 m68k toolchain present"
else
  if [[ ! -d "$RETRO68_SRC/.git" ]]; then
    git clone --recursive https://github.com/autc04/Retro68.git "$RETRO68_SRC"
  else
    git -C "$RETRO68_SRC" submodule update --init --recursive
  fi
  echo "  Building Retro68 toolchain (can take 30–90+ minutes)..."
  mkdir -p "$RETRO68_BUILD" "$DEPS/bin"
  printf '#!/bin/sh\nexec sysctl -n hw.ncpu\n' > "$DEPS/bin/nproc"
  chmod +x "$DEPS/bin/nproc"
  export PATH="$DEPS/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:/opt/homebrew/bin:$PATH"
  "$ROOT/scripts/build-libhfs.sh"
  (
    cd "$RETRO68_BUILD"
    bash "$RETRO68_SRC/build-toolchain.bash" --no-ppc --no-carbon
  )
  echo "  Retro68 installed under $RETRO68_BUILD/toolchain"
fi

export RETRO68="${RETRO68:-$RETRO68_BUILD/toolchain}"

echo "-- Host NSE --"
"$ROOT/scripts/build-host.sh"

echo "-- HelloMacintosh host CLI --"
"$HM/scripts/build-host.sh"

echo
echo "Bootstrap complete."
echo
echo "This tree builds RetroSlack. Serial deploy uses HelloMacintosh:"
echo "  https://github.com/kApp-Oliver/HelloMacintosh"
echo "  checkout: $HM"
echo
echo "Emulator (needs ROM + System disk — docs/SETUP.md):"
echo "  make mac && make run"
echo
echo "Real Plus over serial (HelloMacintosh must already be running on the Mac):"
echo "  make mac"
echo "  make deploy                      # or: make deploy SERIAL=/dev/cu.usbserial-…"
echo "  ./build/host/nse --serial /dev/cu.usbserial-XXXX --baud 19200 --verbose"
echo
echo "One-time loader install lives in the HelloMacintosh repo (make mac / make run there)."
echo "Copy .env.example → .env for Slack tokens. Never commit .env or ROMs."
