#!/bin/sh
# Browser package of the PC port.
#
#   tools/package_wasm.sh
#
# Produces build/dist/superhangon-wasm.zip with the HTML, JavaScript and
# WebAssembly files. It contains no game code and no ROM data: the page asks
# for the player's ROM and keeps it in the browser's local storage.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

fail() { echo "package_wasm: $*" >&2; exit 1; }
command -v zip >/dev/null 2>&1 || fail "missing zip"

name=superhangon-wasm
stage="build/dist/$name"
build="build/wasm-package"
rm -rf "$stage" "build/dist/$name.zip" "$build"
mkdir -p "$stage"
tools/build_wasm.sh --build "$build"
cp "$build/shangon.html" "$build/shangon.js" "$build/shangon.wasm" "$stage/"
cp LICENSE "$stage/"
mkdir -p "$stage/licenses/nuked-opn2" "$stage/licenses/chips" "$stage/source/nuked-opn2"
cp port/third_party/nuked-opn2/LICENSE port/third_party/nuked-opn2/SOURCE "$stage/licenses/nuked-opn2/"
cp port/third_party/nuked-opn2/ym3438.c port/third_party/nuked-opn2/ym3438.h "$stage/source/nuked-opn2/"
cp port/third_party/chips/LICENSE port/third_party/chips/SOURCE "$stage/licenses/chips/"
cat > "$stage/README.txt" <<EOF
Super Hang-On browser build ($(date +%Y-%m-%d), $(git rev-parse --short HEAD))

This package contains no game code and no ROM data. Serve this directory over
HTTP(S), open shangon.html, and choose your own "Super Hang-On (Japan, USA)
(En,Ja)" No-Intro ROM. The page validates the file size, stores the ROM in
browser-local storage, and starts the game. It will be reused on later visits.

Keyboard, browser gamepad and touch controls are supported. Browser storage
requires HTTP(S); opening the page directly as file:// is not supported.
EOF
(cd build/dist && zip -qr "$name.zip" "$name")
echo "package: build/dist/$name.zip"
