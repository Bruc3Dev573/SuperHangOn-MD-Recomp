#!/bin/sh
# Build the playable browser target. The ROM is preloaded into Emscripten's
# virtual filesystem as /baserom.md and is never copied into the repository.
#
#   tools/build_wasm.sh --rom PATH [--build DIR]
set -e
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rom=""
build="$root/build/wasm"
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) [ $# -ge 2 ] || { echo "build_wasm: --rom needs a path" >&2; exit 1; }; rom=$2; shift 2 ;;
    --build) [ $# -ge 2 ] || { echo "build_wasm: --build needs a directory" >&2; exit 1; }; build=$2; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "build_wasm: unknown option $1" >&2; exit 1 ;;
  esac
done
[ -n "$rom" ] || { echo "build_wasm: pass --rom PATH" >&2; exit 1; }
[ -f "$rom" ] || { echo "build_wasm: no such ROM: $rom" >&2; exit 1; }
command -v emcmake >/dev/null 2>&1 || { echo "build_wasm: emcmake is required (install the Emscripten SDK)" >&2; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "build_wasm: emcc is required (install the Emscripten SDK)" >&2; exit 1; }

cd "$root"
emcmake cmake -S port -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DSHANGON_RUNTIME_TRANSLATION=ON -DSHANGON_PATCHES='patches/pc60;patches/relax' \
  -DSHANGON_ROM="$rom"
cmake --build "$build" --target shangon
printf 'built browser target: %s/shangon.html\n' "$build"
printf 'serve this directory over HTTP; the ROM is in shangon.data\n'
