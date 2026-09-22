#!/usr/bin/env bash
# Build libhfs (+ headers) into deps/hfsutils-prefix for Retro68.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$ROOT/deps/hfsutils-prefix"
SRC="$ROOT/deps/hfsutils-src-old"
TGZ=/tmp/hfsutils-3.2.6.orig.tar.gz

mkdir -p "$ROOT/deps"
if [[ ! -f "$TGZ" ]]; then
  curl -fsSL -o "$TGZ" \
    "https://deb.debian.org/debian/pool/main/h/hfsutils/hfsutils_3.2.6.orig.tar.gz"
fi

rm -rf "$SRC"
mkdir -p "$SRC"
tar -xzf "$TGZ" -C "$SRC" --strip-components=1

# Modern Clang: force POSIX headers in os/unix.c
python3 - "$SRC/libhfs/os/unix.c" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
text = p.read_text()
text = re.sub(
    r'# ifdef HAVE_UNISTD_H\n#  include <unistd\.h>\n# else\n.*?# endif\n',
    '# include <sys/types.h>\n# include <unistd.h>\n# include <fcntl.h>\n',
    text, count=1, flags=re.S)
p.write_text(text)
PY

cd "$SRC"
CFLAGS="-Wno-implicit-int -Wno-return-type -Wno-deprecated-non-prototype" \
  ./configure --prefix="$PREFIX" --enable-devlibs >/dev/null
cd libhfs
rm -f os.c
ln -sf os/unix.c os.c
make CFLAGS="-g -O2 -Wno-everything -DHAVE_UNISTD_H=1" >/dev/null
mkdir -p "$PREFIX/include" "$PREFIX/lib"
cp -f hfs.h "$PREFIX/include/"
cp -f libhfs.a "$PREFIX/lib/"
echo "Installed $PREFIX/include/hfs.h and $PREFIX/lib/libhfs.a"
