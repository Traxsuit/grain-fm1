#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M-Vave-OP1 contributors
# The app in the browser: firmware/app/ on the host platform (web/emu/emu.c, tests/host_platform.h), compiled to
# WebAssembly with Emscripten, plus the page.
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, emu.wasm}
# Run after one ./build.sh (build/gen: the fonts and palettes). Serve build/emu over HTTP (AudioWorklet and fetch do
# not work from file://): python3 -m http.server -d build/emu 8000, then http://localhost:8000/
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/ui_fonts.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten: . ~/emsdk/emsdk_env.sh)"; exit 1; }
NAME=$(sed -n 's/^#define APP_NAME "\([^"]*\)".*/\1/p' firmware/app/app.c)
VERSION=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' firmware/app/app.c)
mkdir -p build/emu
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sFILESYSTEM=0 \
    -o build/emu/emu.wasm web/emu/emu.c
sed -e "s/@NAME@/$NAME/g" -e "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/emu.wasm | tr -d ' ') B wasm, $NAME $VERSION)"
