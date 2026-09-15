#!/bin/sh
# macOS package of the PC port (the native host architecture).
#
#   tools/package_macos.sh [--30hz]
#
# Produces build/dist/superhangon-macos-ARCH.zip with shangon, a bundled SDL2
# dylib, the licences and the replaceable Nuked-OPN2 source. It contains no
# game code and no ROM data: the game code is translated from the player's ROM
# (baserom.md next to shangon) when it starts.
#
# Requires: cmake, pkg-config, SDL2/SDL3 development files, zip and Apple's
# install_name_tool (Homebrew: brew install cmake pkg-config sdl2 sdl3 zip).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

patches="patches/pc60;patches/relax"
[ "${1:-}" = "--30hz" ] && patches=""

fail() { echo "package_macos: $*" >&2; exit 1; }
for tool in cc cmake pkg-config zip otool install_name_tool; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing $tool"
done
pkg-config --exists sdl2 || fail "missing SDL2 development files (brew install sdl2)"

arch=$(uname -m)
case "$arch" in
  arm64|x86_64) ;;
  *) fail "unsupported macOS architecture: $arch" ;;
esac
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
name="superhangon-macos-$arch"
stage="build/dist/$name"
build="build/macos-$arch"
rm -rf "$stage" "build/dist/$name.zip"
mkdir -p "$stage"

cmake -S port -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$arch" -DSHANGON_OPN2_SHARED=OFF \
  -DSHANGON_PORTABLE=ON -DSHANGON_RUNTIME_TRANSLATION=ON \
  "-DSHANGON_PATCHES=$patches" >/dev/null
cmake --build "$build" -j"$jobs" --target shangon
cmake --install "$build" --prefix "$stage" --component game >/dev/null
strip "$stage/shangon" 2>/dev/null || true

# Homebrew's SDL2 compatibility layer loads SDL3 at runtime. Bundle both
# libraries and keep the SDL2 reference relative to the executable.
sdl_libdir=$(pkg-config --variable=libdir sdl2)
sdl="$sdl_libdir/libSDL2-2.0.0.dylib"
[ -f "$sdl" ] || fail "cannot find SDL2 dylib: $sdl"
sdl_name=${sdl##*/}
cp -L "$sdl" "$stage/$sdl_name"
src_name=$(otool -L "$stage/shangon" | awk '/libSDL2-2[.]0[.]0[.]dylib/{print $1; exit}')
[ -n "$src_name" ] || fail "shangon does not link SDL2"
install_name_tool -change "$src_name" "@loader_path/$sdl_name" "$stage/shangon"

sdl3_libdir=$(pkg-config --variable=libdir sdl3 2>/dev/null || true)
sdl3="$sdl3_libdir/libSDL3.dylib"
[ -f "$sdl3" ] || fail "missing SDL3 runtime required by sdl2-compat (brew install sdl3)"
cp -L "$sdl3" "$stage/libSDL3.dylib"
mkdir -p "$stage/licenses/nuked-opn2" "$stage/licenses/chips" "$stage/licenses/SDL2" "$stage/licenses/SDL3" "$stage/source/nuked-opn2"
cp LICENSE "$stage/licenses/"
cp port/third_party/nuked-opn2/LICENSE port/third_party/nuked-opn2/SOURCE "$stage/licenses/nuked-opn2/"
cp port/third_party/nuked-opn2/ym3438.c port/third_party/nuked-opn2/ym3438.h "$stage/source/nuked-opn2/"
cp port/third_party/chips/LICENSE port/third_party/chips/SOURCE "$stage/licenses/chips/"
sdl_prefix=$(pkg-config --variable=prefix sdl2)
for f in "$sdl_prefix/share/licenses/sdl2/LICENSE.txt" \
         "$sdl_prefix/share/licenses/sdl2-compat/LICENSE.txt" \
         "$sdl_prefix/LICENSE.txt"; do
  if [ -f "$f" ]; then
    cp "$f" "$stage/licenses/SDL2/"
    break
  fi
done
sdl3_prefix=$(pkg-config --variable=prefix sdl3)
for f in "$sdl3_prefix/share/licenses/SDL3/LICENSE.txt" \
         "$sdl3_prefix/share/licenses/sdl3/LICENSE.txt" \
         "$sdl3_prefix/LICENSE.txt"; do
  if [ -f "$f" ]; then
    cp "$f" "$stage/licenses/SDL3/"
    break
  fi
done
cat > "$stage/README.txt" <<EOF
Super Hang-On (Mega Drive) PC port ($(date +%Y-%m-%d), $(git rev-parse --short HEAD), ${patches:-30 Hz})
macOS $arch build using SDL2 and Apple's OpenGL implementation.

This package contains no game code and no ROM data: you need your own ROM.
At the first start the game code is translated from it (shangon.cache).

Setup
  1. Copy your "Super Hang-On (Japan, USA) (En,Ja)" ROM into this folder,
     next to shangon, with the name baserom.md
     (or run ./shangon --rom /path/to/rom.md)
  2. Open Terminal in this folder and run ./shangon

Settings menu: Esc, F1 or the controller Back button (display, screen format,
CRT filters, volume, reset, quit). F3 shows the frame rate counter.
Options: --fullscreen, --window W H, --mute, --vsync / --no-vsync,
--format 4:3|16:9|21:9,
--crt off|scanlines|aperture|slot|shadow.
Controls are in controls.ini in this folder (created on the first run):
arrows / D-pad, Z X C = A B C, right trigger accelerates (B), left trigger
brakes (A), Enter / Start; F5 save state, F8 load, F6/F7 slot, Backspace or
left shoulder rewinds, F11 fullscreen.

Third-party code: SDL2 (zlib, bundled as $sdl_name) and SDL3 (zlib, bundled as
libSDL3.dylib), Nuked-OPN2 (LGPL-2.1, source in source/nuked-opn2), floooh
chips z80.h (zlib).
EOF

linked=$(otool -L "$stage/shangon")
case "$linked" in
  *"@loader_path/$sdl_name"*) ;;
  *) fail "SDL2 reference was not made relative" ;;
esac
(cd build/dist && zip -qr "$name.zip" "$name")
echo "package: build/dist/$name.zip"
