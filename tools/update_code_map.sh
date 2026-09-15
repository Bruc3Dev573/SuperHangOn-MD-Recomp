#!/bin/sh
# Makes the data of the runtime translation build (port/data/OVERLAYS/) from
# your ROM: the block structure of the game code (code_map.c: addresses and
# instruction counts, no code) and the bytes the source overlays add
# (rom_patch.c). Run it after changing the overlays or symbols.txt.
#   tools/update_code_map.sh
# Needs: rom/baserom.md, python3, make, tools/bin/vasmm68k_mot.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
for set in "patches/pc60 patches/relax" ""; do
  tag=$(echo "$set" | sed 's|patches/||g; s| |+|g')
  [ -n "$tag" ] || tag=original
  out=port/data/$tag
  mkdir -p "$out"
  make -s patched "PATCHES=$set" NAME=codemap >/dev/null
  python3 tools/recomp/recomp.py build/codemap.md build/codemap.lst build/codemap_code.c --code-map "$out/code_map.c" >/dev/null
  python3 tools/recomp/romdiff.py rom/baserom.md build/codemap.md "$out/rom_patch.c"
  echo "$out: $(grep -c '^  {0x' "$out/code_map.c") blocks"
done
