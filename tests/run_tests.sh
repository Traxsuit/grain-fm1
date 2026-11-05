#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 M-Vave-OP1 contributors
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
# It keeps going after a failure; the last line is ALL HOST TESTS PASSED or HOST TESTS FAILED. Under a minute.
#
# PLATFORM (from Felucca / M-Vave-OP1, the code that decides whether a bad image can still be replaced):
#   input        the key / button debounce and encoders of hal/fm1_input.h against a simulated scan (input_test.c)
#   TRS MIDI     the running-status parser (midi_uart.c) and usb.c's SysEx frames (midi_uart_test.c)
#   USB          the descriptors of this build (MIDI only: no console, no audio input) (usb_desc_test.c)
#   update       ota.c's M-UPGRADE entry with this build's package against a fake updater and a simulated NOR
#                (ota_test.c); the update loader installing this build over another app (ldr_test.c); the
#                installer CLI against a simulated FM-1, including the return to official V15 (install_test.py)
#   store        store.c's records on a simulated NOR: A/B, CRC, a save cut at any byte, another app's (store_test.c)
#   ISR budget   the audio and scan ISRs' loop cost in the pi32v2 disassembly, +10 % (target_budget.py)
# APP (firmware/app/; change these with the app):
#   app          the app on the host platform: screens, layout lint, LEDs, its record in flash, sound, MIDI
#                (app_test.c; build/shots/)
# WEB:
#   installer    fm1pkg.js / fm1ota.js against Python and a simulated FM-1, the page assembled (web/test_web.mjs)
#   emulator     the app to WebAssembly (web/emu/build.sh) and driven without a browser (web/emu/emu_test.mjs)
# A sanitizer pass (ASan / UBSan) repeats the update path, the store and the app test when the compiler has them.
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
CC="${CC:-cc} -O1 -ffp-contract=off -Wall -Wno-unused-function"
APP_FLAGS="-Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/firmware.fwsc ] || { echo "run ./build.sh first"; exit 1; }

# ---------------------------------------------------------------- platform
$CC -w -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser, USB SysEx frames" "$OUT/midi_uart_test"

$CC -DT_CDC=0 -DT_UAC=0 -DT_LAYOUT=0 -DT_ON=1 -DT_48K=1 -o "$OUT/usb_desc_test" tests/usb_desc_test.c
run "USB descriptors: MIDI only (no console, no audio input)" "$OUT/usb_desc_test"

$CC -w -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/firmware.fwsc

head -c 30000 build/firmware.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -w -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/firmware.fwsc

$CC -o "$OUT/store_test" tests/store_test.c
run "store: records in flash (A/B, CRC, a save cut at any byte, another app's data, limits)" "$OUT/store_test"

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

run "ISR budget: target cost of the audio and scan ISRs (pi32v2 disassembly)" python3 tests/target_budget.py \
    build/firmware.dis tests/target_budget.txt

# ---------------------------------------------------------------- app
# shellcheck disable=SC2086
$CC $APP_FLAGS -o "$OUT/app_test" tests/app_test.c -lm
run "app: screens, layout, LEDs, its record in flash, sound, MIDI (build/shots/)" "$OUT/app_test"

# ---------------------------------------------------------------- sanitizers
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g"
if echo 'int main(void){return 0;}' | ${CC%% *} $SAN -x c -o "$OUT/san_probe" - 2>/dev/null; then
    A="$OUT/asan"
    mkdir -p "$A"
    SCC="${CC%% *} -O1 $SAN -w -ffp-contract=off"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    $SCC -o "$A/ldr_test" tests/ldr_test.c
    run "ASan/UBSan: update loader" "$A/ldr_test" "$OUT/old.fwsc" build/firmware.fwsc
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry" "$A/ota_test" build/firmware.fwsc
    $SCC -o "$A/store_test" tests/store_test.c
    run "ASan/UBSan: store" "$A/store_test"
    # shellcheck disable=SC2086
    $SCC $APP_FLAGS -o "$A/app_test" tests/app_test.c -lm
    run "ASan/UBSan: app" "$A/app_test"
else
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
fi

# ---------------------------------------------------------------- web
if command -v node >/dev/null 2>&1; then
    run "installer: package format, update protocol, the page" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi
if ! command -v emcc >/dev/null 2>&1; then
    echo "== skip the browser emulator (no emcc: . ~/emsdk/emsdk_env.sh)"
elif ! command -v node >/dev/null 2>&1; then
    echo "== skip the browser emulator (no node)"
else
    run "browser emulator: the app to WebAssembly (build/emu)" sh web/emu/build.sh
    run "browser emulator: boot, screen, keys, buttons, knobs, MIDI, speed" node web/emu/emu_test.mjs build/emu/emu.wasm
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
