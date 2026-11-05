# Building

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `firmware.bin` | the firmware app (platform + app) |
| `loader/ota.bin` | the update loader |
| `firmware.fwsc` | the installable package (app + loader + three JieLi SDK files), identity `FM-1_900` |

## Prerequisites

On Linux x86-64 (and in a Claude Code cloud session) one script does it all:

```sh
sh .claude/skills/fm1-platform/scripts/setup_env.sh           # toolchain, SDK files, Python packages, graphify
sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk   # + Emscripten (the browser emulator)
```

What it installs, if you do it by hand:

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font is rasterised at build time)
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server): `tools/get_toolchain.sh`
  (installs to `~/.jieli/toolchain`)
- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree. Gitee resets connections now and then: retry.

  ```sh
  git clone --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  (cd ~/fw-AC79_AIoT_SDK && git sparse-checkout set cpu/wl82/tools)
  ```

- Node.js (optional: the web tests, the emulator test) and Emscripten (optional, `emcc`: the browser emulator).
- For the command-line installer only: `pip3 install mido python-rtmidi`.

On macOS the JieLi toolchain (Linux x86-64 only) runs in Docker: install Docker Desktop and keep the tree in a folder
Docker can share (e.g. under `/Users`); `tools/build.py` runs each tool in a `linux/amd64` `debian:bookworm-slim`
container.

## Build

```sh
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).
The build refuses an image that breaks the platform's checks: the entry point, no calls from RAM code into the flash,
no reference into the chip's ROM, the app slot, at least 4 KiB of RAM and 8 KiB of pool spare, and register access
only in `firmware/hal/`.

`./build.sh --release 1.0` makes a release build: the app's version string becomes `v1.0` (`APP_VERSION`), the
package `build/firmware-1.0.fwsc`, and `build/release-1.0/` holds what a release ships (the package, the app,
`SHA256SUMS`, `LICENSE`, `LICENSING.md`, `LICENSES/`). The package identity stays `FM-1_900`.

Build options (environment, `0` or `1`; defaults in `firmware/src/firmware.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | the flash driver (`storage_hw.c`) |
| `FELUCCA_OTA` | 1 | the update entry: how a new package gets in over USB. Keep it on |
| `FELUCCA_OTA_DRYRUN` | 0 | `1`: an update stages, asks "success", then undoes everything (a bench test) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |

## Tests

```sh
tests/run_tests.sh
```

Under a minute, after `./build.sh`. See the top of `tests/run_tests.sh` for what it covers; screenshots land in
`build/shots/`.

## Browser emulator

```sh
sh web/emu/build.sh
python3 -m http.server -d build/emu 8000     # open http://localhost:8000/
```

## Install

Only when everything above is green, and with recovery in mind (README.md, "Safety"). From the command line:

```sh
python3 tools/fm1_install.py build/firmware.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
python3 tools/fm1_install.py --stock         # back to M-VAVE's official V15 (vendor/m-vave/)
```

Or with the web installer, from a local copy of the site (Web MIDI needs `localhost` or https; Chrome or Edge):

```sh
python3 web/make_site.py build/firmware.fwsc dev build/site
python3 -m http.server -d build/site 8000    # open http://localhost:8000/webapp/installer/
```
