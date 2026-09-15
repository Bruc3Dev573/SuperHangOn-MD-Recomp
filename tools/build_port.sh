#!/bin/sh
# Super Hang-On PC port: build from source (Linux).
#
#   tools/build_port.sh [--install] [--prefix DIR] [--30hz] [--recompile [--rom PATH]]
#
#   --install     install into PREFIX (default ~/.local): bin/shangon and a
#                 desktop entry; put your ROM next to it (bin/baserom.md)
#   --30hz        build the original 30 Hz game logic (no overlays)
#   --recompile   recompile the game code to C at build time (needs the ROM,
#                 python3 and the vasm assembler, fetched once) instead of
#                 translating it from the ROM when the game starts
#   --rom PATH    with --recompile: Super Hang-On (Japan, USA) (En,Ja) ROM,
#                 copied to rom/baserom.md (SHA1 checked)
#
# Needs: a C compiler, make, cmake, pkg-config, SDL2 development files.
# Linux (Debian/Ubuntu): sudo apt install build-essential cmake pkg-config libsdl2-dev
# (--recompile also: python3 curl)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

rom=""; install=0; prefix="$HOME/.local"; patches="patches/pc60;patches/relax"; recompile=0
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) rom=$2; shift 2 ;;
    --install) install=1; shift ;;
    --prefix) prefix=$2; shift 2 ;;
    --30hz) patches=""; shift ;;
    --recompile) recompile=1; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
done

fail() { echo "build_port: $*" >&2; exit 1; }
sha1() { sha1sum "$1" | awk '{print $1}'; }
ncpu=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

for tool in cc make cmake pkg-config; do
  command -v $tool >/dev/null 2>&1 || fail "missing $tool (see the dependency list at the top of this script)"
done
pkg-config --exists sdl2 || fail "missing SDL2 development files (Debian/Ubuntu: libsdl2-dev)"

if [ $recompile = 1 ]; then
  command -v python3 >/dev/null 2>&1 || fail "missing python3"
  expected=$(awk 'NR==1{print $1}' rom/baserom.sha1)
  if [ -n "$rom" ]; then
    [ -f "$rom" ] || fail "no such file: $rom"
    actual=$(sha1 "$rom")
    [ "$actual" = "$expected" ] || fail "$rom is not the expected ROM (SHA1 $actual, expected $expected)"
    mkdir -p rom
    cp "$rom" rom/baserom.md
  fi
  [ -f rom/baserom.md ] || fail "put your ROM in rom/baserom.md or pass --rom PATH"
  actual=$(sha1 rom/baserom.md)
  [ "$actual" = "$expected" ] || fail "rom/baserom.md is not the expected ROM (SHA1 $actual)"

  # vasm (Motorola syntax 68000 assembler), built once into tools/bin
  if [ ! -x tools/bin/vasmm68k_mot ]; then
    command -v curl >/dev/null 2>&1 || fail "missing curl (needed once to fetch vasm)"
    echo "== fetching and building vasm"
    tmp=$(mktemp -d)
    curl -sL -o "$tmp/vasm.tar.gz" http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
    tar xzf "$tmp/vasm.tar.gz" -C "$tmp"
    make -C "$tmp/vasm" CPU=m68k SYNTAX=mot >/dev/null
    mkdir -p tools/bin
    cp "$tmp/vasm/vasmm68k_mot" tools/bin/
    rm -rf "$tmp"
  fi
  echo "== disassembly check"
  make -s verify
  translation=OFF
else
  translation=ON
fi

echo "== building the port (overlays: ${patches:-none, original 30 Hz logic})"
cmake -S port -B build/port -DCMAKE_BUILD_TYPE=Release "-DSHANGON_PATCHES=$patches" \
  -DSHANGON_RUNTIME_TRANSLATION=$translation >/dev/null
cmake --build build/port -j"$ncpu" --target shangon

if [ $install = 1 ]; then
  echo "== installing into $prefix"
  cmake --install build/port --prefix "$prefix" --component game >/dev/null
  echo "installed: $prefix/bin/shangon (put your ROM next to it as baserom.md)"
else
  echo "built: build/port/shangon   (put your ROM next to it as baserom.md)"
fi
