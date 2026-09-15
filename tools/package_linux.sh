#!/bin/sh
# Linux binary package of the PC port (x86_64 or the host architecture).
#
#   tools/package_linux.sh [--30hz]
#
# Produces build/dist/superhangon-linux-ARCH.tar.gz with shangon, the
# Nuked-OPN2 shared library (LGPL-2.1, replaceable, in lib/) and the licence
# files. It contains no game code and no ROM data: the game code is translated
# from the player's ROM, next to the executable (baserom.md), when it starts.
#
# Requires what tools/build_port.sh requires. The target machines need SDL2
# (Debian/Ubuntu: libsdl2-2.0-0).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

patches="patches/pc60;patches/relax"
[ "$1" = "--30hz" ] && patches=""


arch=$(uname -m)
name=superhangon-linux-$arch
stage=build/dist/$name
rm -rf "$stage" "build/dist/$name.tar.gz"
mkdir -p "$stage"

cmake -S port -B build/package -DCMAKE_BUILD_TYPE=Release -DSHANGON_OPN2_SHARED=ON -DSHANGON_PORTABLE=ON -DSHANGON_RUNTIME_TRANSLATION=ON "-DSHANGON_PATCHES=$patches" >/dev/null
cmake --build build/package -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" --target shangon
cmake --install build/package --prefix "$stage" --component game >/dev/null
strip "$stage/shangon" 2>/dev/null || true

mkdir -p "$stage/licenses/nuked-opn2" "$stage/licenses/chips" "$stage/source/nuked-opn2"
cp port/third_party/nuked-opn2/LICENSE port/third_party/nuked-opn2/SOURCE "$stage/licenses/nuked-opn2/"
cp port/third_party/nuked-opn2/ym3438.c port/third_party/nuked-opn2/ym3438.h "$stage/source/nuked-opn2/"
cp port/third_party/chips/LICENSE port/third_party/chips/SOURCE "$stage/licenses/chips/"

cat > "$stage/README.txt" <<EOF
Super Hang-On (Mega Drive) PC port ($(date +%Y-%m-%d), $(git rev-parse --short HEAD), ${patches:-30 Hz})

This package contains no game code and no ROM data: you need your own ROM.
At the first start the game code is translated from it (shangon.cache,
shangon120.cache at 120 fps).

Setup
  1. Copy your "Super Hang-On (Japan, USA) (En,Ja)" ROM into this folder,
     next to shangon, with the name baserom.md
     (or run ./shangon --rom /path/to/rom.md)
  2. Run ./shangon   (needs SDL2: sudo apt install libsdl2-2.0-0)

Settings menu: Esc, F1 or the controller Back button (display, screen format,
frame rate 60 or 120, CRT filters, volume, reset, quit). F3 shows the frame rate counter.
Options: --fullscreen, --window W H, --mute, --vsync / --no-vsync,
--format 4:3|16:9|21:9,
--crt off|scanlines|aperture|slot|shadow.
Controls are in controls.ini in this folder
(created on the first run): arrows / D-pad, Z X C = A B C, right trigger
accelerates (B), left trigger brakes (A), Enter / Start; F5 save state,
F8 load, F6/F7 slot, Backspace or left shoulder rewinds, F11 fullscreen.

Third-party code: Nuked-OPN2 (LGPL-2.1, lib/libnuked_opn2.so, source in
source/nuked-opn2; you may replace the library), floooh chips z80.h (zlib).
Licence texts in licenses/.
EOF

tar -czf "build/dist/$name.tar.gz" -C build/dist "$name"
echo "package: build/dist/$name.tar.gz"
