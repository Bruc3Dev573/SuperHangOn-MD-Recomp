#!/bin/sh
# Build the playable browser target. The ROM is selected in the generated page
# and kept in the browser's IndexedDB-backed virtual filesystem.
#
#   tools/build_wasm.sh [--build DIR]
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build="$root/build/wasm"
while [ $# -gt 0 ]; do
  case "$1" in
    --build) [ $# -ge 2 ] || { echo "build_wasm: --build needs a directory" >&2; exit 1; }; build=$2; shift 2 ;;
    -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
    *) echo "build_wasm: unknown option $1" >&2; exit 1 ;;
  esac
done
command -v emcmake >/dev/null 2>&1 || { echo "build_wasm: emcmake is required (install the Emscripten SDK)" >&2; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "build_wasm: emcc is required (install the Emscripten SDK)" >&2; exit 1; }

cd "$root"
emcmake cmake -S port -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DSHANGON_RUNTIME_TRANSLATION=ON -DSHANGON_PATCHES='patches/pc60;patches/relax' \
  -DSHANGON_WASM_SHELL="$root/tools/wasm_shell.html"
cmake --build "$build" --target shangon
printf 'built browser target: %s/shangon.html\n' "$build"
printf 'serve this directory over HTTP(S); choose the ROM in the page\n'
