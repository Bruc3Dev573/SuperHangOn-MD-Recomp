#!/bin/sh
# Windows (x86_64) package of the PC port, cross-compiled on Linux.
#
#   tools/package_windows.sh [--30hz]
#
# Produces build/dist/superhangon-windows-x86_64.zip: shangon.exe, SDL2.dll,
# libnuked_opn2.dll (LGPL-2.1, replaceable, source included), README and
# licences. It contains no game code and no ROM data: the game code is
# translated from the player's ROM (baserom.md next to shangon.exe) when it
# starts.
#
# Requires: cmake, MinGW-w64
# (Debian/Ubuntu: sudo apt install gcc-mingw-w64-x86-64), curl, zip.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

SDL_VERSION=2.32.4
SDL_SHA256=c2ec09788ab99b23b8e8e472775e5f728da549ae27898280cedbb15da87f47c1

patches="patches/pc60;patches/relax"
[ "$1" = "--30hz" ] && patches=""

fail() { echo "package_windows: $*" >&2; exit 1; }
command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || fail "missing MinGW-w64 (sudo apt install gcc-mingw-w64-x86-64)"
command -v zip >/dev/null 2>&1 || fail "missing zip"

# SDL2 development files for MinGW (pinned release, checksum verified)
sdl=tools/sdl2-mingw/SDL2-$SDL_VERSION/x86_64-w64-mingw32
if [ ! -d "$sdl" ]; then
  mkdir -p tools/sdl2-mingw
  archive=tools/sdl2-mingw/SDL2-devel-$SDL_VERSION-mingw.tar.gz
  curl -sL -o "$archive" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
  echo "$SDL_SHA256  $archive" | sha256sum -c - >/dev/null || fail "SDL2 archive checksum mismatch"
  tar xzf "$archive" -C tools/sdl2-mingw
fi

name=superhangon-windows-x86_64
stage=build/dist/$name
rm -rf "$stage" "build/dist/$name.zip"
mkdir -p "$stage"

cmake -S port -B build/win64 -DCMAKE_TOOLCHAIN_FILE="$root/port/cmake/mingw-w64-x86_64.cmake" \
  -DSDL2_MINGW_ROOT="$root/$sdl" -DSDL2_DIR="$root/$sdl/lib/cmake/SDL2" \
  -DCMAKE_BUILD_TYPE=Release -DSHANGON_OPN2_SHARED=ON -DSHANGON_RUNTIME_TRANSLATION=ON "-DSHANGON_PATCHES=$patches" >/dev/null
cmake --build build/win64 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" --target shangon
cmake --install build/win64 --prefix "$stage" --component game >/dev/null
x86_64-w64-mingw32-strip "$stage/shangon.exe" "$stage/libnuked_opn2.dll"
cp "$sdl/bin/SDL2.dll" "$stage/"

mkdir -p "$stage/licenses/nuked-opn2" "$stage/licenses/chips" "$stage/licenses/SDL2" "$stage/source/nuked-opn2"
cp port/third_party/nuked-opn2/LICENSE port/third_party/nuked-opn2/SOURCE "$stage/licenses/nuked-opn2/"
cp port/third_party/nuked-opn2/ym3438.c port/third_party/nuked-opn2/ym3438.h "$stage/source/nuked-opn2/"
cp port/third_party/chips/LICENSE port/third_party/chips/SOURCE "$stage/licenses/chips/"
for f in LICENSE.txt README-SDL.txt; do
  [ -f "tools/sdl2-mingw/SDL2-$SDL_VERSION/$f" ] && cp "tools/sdl2-mingw/SDL2-$SDL_VERSION/$f" "$stage/licenses/SDL2/"
done

sed 's/$/\r/' > "$stage/README.txt" <<EOF
Super Hang-On (Mega Drive) PC port ($(date +%Y-%m-%d), $(git rev-parse --short HEAD), ${patches:-30 Hz})

This package contains no game code and no ROM data: you need your own ROM.
At the first start the game code is translated from it (shangon.cache).

Setup
  1. Copy your "Super Hang-On (Japan, USA) (En,Ja)" ROM into this folder,
     next to shangon.exe, with the name baserom.md (or start shangon.exe
     from a command prompt with --rom C:\\path\\to\\rom.md)
  2. Run shangon.exe

Settings menu: Esc, F1 or the controller Back button (display, screen format,
CRT filters, volume, reset, quit). F3 shows the frame rate counter.
Options: --fullscreen, --window W H, --mute, --vsync / --no-vsync,
--format 4:3|16:9|21:9,
--crt off|scanlines|aperture|slot|shadow.
Controls are in controls.ini in this folder (created on
the first run): arrows / D-pad, Z X C = A B C, right trigger accelerates (B),
left trigger brakes (A), Enter / Start; F5 save state, F8 load, F6/F7 slot,
Backspace or left shoulder rewinds, F11 fullscreen.

Third-party code: SDL2 (zlib), Nuked-OPN2 (LGPL-2.1, libnuked_opn2.dll, source
in source\\nuked-opn2; you may replace the DLL), floooh chips z80.h (zlib).
Licence texts in licenses\\.
EOF

(cd build/dist && zip -qr "$name.zip" "$name")
echo "package: build/dist/$name.zip"
