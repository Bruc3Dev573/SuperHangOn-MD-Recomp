#!/bin/sh
# Makes the data of the runtime translation build from your ROM, one code set
# per directory of port/data: the block structure of the game code
# (code_map.c: addresses and instruction counts, no code) and the bytes the
# source overlays add (rom_patch.c).
#   port/data/60hz      patches/pc60 + patches/relax, 60 logic ticks per second
#   port/data/120hz     the same overlays at 120 ticks per second
#   port/data/original  no overlays (the 30 Hz game)
# Run it after changing the overlays or symbols.txt.
#   tools/update_code_map.sh
# Needs: rom/baserom.md, python3, make, tools/bin/vasmm68k_mot.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
make_set() {   # name, overlays, TickShift
  out=port/data/$1
  mkdir -p "$out"
  make -s patched "PATCHES=$2" NAME=codemap "DEFINES=TickShift=$3" >/dev/null
  python3 tools/recomp/recomp.py build/codemap.md build/codemap.lst build/codemap_code.c \
    --code-map "$out/code_map.c" --code-set "$1" >/dev/null
  python3 tools/recomp/romdiff.py rom/baserom.md build/codemap.md "$out/rom_patch.c" "$1"
  echo "$out: $(grep -c '^  {0x' "$out/code_map.c") blocks"
}
make_set 60hz "patches/pc60 patches/relax" 1
make_set 120hz "patches/pc60 patches/relax" 2
make_set original "" 1
