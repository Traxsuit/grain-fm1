#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M-Vave-OP1 contributors
# Set up a Linux x86-64 machine to build and test FM-1 firmware made from the FM-1 Firmware Kit. Run from the
# repository root.
#   sh .claude/skills/fm1-platform/scripts/setup_env.sh           toolchain, SDK files, Python packages
#   sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk   + Emscripten (browser emulator, wasm2js)
# Idempotent: what is there already is kept. Works before the repository is set up too (the kit still in template/:
# the interview can run while this installs), and then skips the code graph.
set -e
ROOT=.
[ -d template ] && [ ! -d firmware ] && ROOT=template
SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
SDK_TAG=AC79NN_SDK_V1.2.1_2023-12-13

retry() {                                  # retry "$@" up to 5 times, 2 4 8 16 s apart (Gitee resets connections)
    n=0
    until "$@"; do
        n=$((n + 1))
        [ "$n" -ge 5 ] && return 1
        sleep $((1 << n))
    done
}

if [ -x "$TC/pi32v2/bin/clang" ]; then
    echo "toolchain: $TC (kept)"
else
    retry sh "$ROOT/tools/get_toolchain.sh"
fi

if [ -f "$SDK/cpu/wl82/tools/uboot.boot" ]; then
    echo "sdk: $SDK (kept)"
else
    rm -rf "$SDK"
    retry git clone -q --depth 1 --filter=blob:none --sparse --branch "$SDK_TAG" \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$SDK"
    (cd "$SDK" && git sparse-checkout set cpu/wl82/tools)
    ls "$SDK/cpu/wl82/tools/uboot.boot" "$SDK/cpu/wl82/tools/cfg_tool.bin" "$SDK/cpu/wl82/tools/cfg/eq_cfg_hw.bin"
fi

python3 -c "import PIL, fontTools" 2>/dev/null || pip3 install -q Pillow fonttools
python3 -c "import mido, rtmidi" 2>/dev/null || pip3 install -q mido python-rtmidi > /dev/null 2>&1 || \
    echo "note: mido / python-rtmidi missing (only tools/fm1_install.py needs them; python-rtmidi needs ALSA headers)"

# graphify (CLAUDE.md): the code's knowledge graph for Claude, built locally from the AST (no LLM, no key); not
# needed to build or test, so a failure here only says so
GRAPHIFY_VER=0.9.97
if [ ! -d .claude/skills/graphify ] || [ "$ROOT" != . ]; then
    echo "graphify: skipped (not part of this project, or the repository is not set up yet)"
elif ! command -v graphify >/dev/null 2>&1; then
    if command -v uv >/dev/null 2>&1; then uv tool install -q "graphifyy==$GRAPHIFY_VER"
    elif command -v pipx >/dev/null 2>&1; then pipx install -q "graphifyy==$GRAPHIFY_VER"
    else pip3 install -q "graphifyy==$GRAPHIFY_VER"; fi > /dev/null 2>&1 || true
fi
if [ ! -d .claude/skills/graphify ] || [ "$ROOT" != . ]; then
    :
elif command -v graphify >/dev/null 2>&1; then
    { graphify extract . --code-only && graphify cluster-only . --no-label; } > /dev/null 2>&1 &&
        echo "graphify: graphify-out/ (graph.json, GRAPH_REPORT.md)" || echo "note: graphify could not build the graph"
else
    echo "note: graphify missing (only Claude's code graph needs it: CLAUDE.md)"
fi

if [ "$1" = "--emsdk" ]; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        echo "emsdk: $HOME/emsdk (kept)"
    else
        retry git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
        (cd "$HOME/emsdk" && ./emsdk install latest && ./emsdk activate latest)
    fi
    echo "emsdk: run '. $HOME/emsdk/emsdk_env.sh' in the shell that builds the emulator"
fi
echo "ready: ./build.sh, then tests/run_tests.sh"
