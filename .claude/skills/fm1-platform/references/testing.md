# Testing

## The suite

```sh
./build.sh                               # first: build/gen (fonts, palettes) and build/firmware.fwsc
. ~/emsdk/emsdk_env.sh                   # optional: then run_tests.sh also builds and tests the browser emulator
tests/run_tests.sh > build/tests.log 2>&1
grep -nE "FAIL|OVER BUDGET|HOST TESTS|== skip" build/tests.log
```

Under a minute. It runs everything even after a failure and ends with `ALL HOST TESTS PASSED` or `HOST TESTS FAILED`.
Each check prints `<what> ... ok|FAIL`; the comment block at the top of `run_tests.sh` lists the groups. A `== skip`
line means a tool is missing (node, emcc, a sanitizer): say so when you report.

| Group | File | What breaks it |
| --- | --- | --- |
| input | `tests/input_test.c` | changes to `hal/fm1_input.h` |
| TRS MIDI, SysEx frames | `tests/midi_uart_test.c` | `midi_uart.c`, usb.c's SysEx path |
| USB descriptors | `tests/usb_desc_test.c` (+ `usb_desc_v10.h`) | usb.c descriptors (the MIDI-only device as a host parses it; the update SysEx path on EP1) |
| update entry | `tests/ota_test.c` | `ota.c`, the package format |
| update loader | `tests/ldr_test.c` | `loader/`, the package format |
| store | `tests/store_test.c` | `store.c` |
| installer CLI | `tests/install_test.py` | `tools/fm1_install.py`, the package |
| ISR budget | `tests/target_budget.py`, `target_budget.txt` | more work in `app_audio` / the ISRs |
| app | `tests/app_test.c` | the app: change it with the app |
| sanitizers | ASan / UBSan builds of the update path, the store, the app | memory errors, undefined behaviour |
| web | `web/test_web.mjs` | `web/fm1pkg.js`, `fm1ota.js`, `installer.html`, `make_site.py` |
| emulator | `web/emu/emu_test.mjs` | the app or `tests/host_platform.h` under WebAssembly |

The platform's groups should never change with an app; when one fails after an app change, the app reached into the
platform (or broke the build's checks).

## The host platform (`tests/host_platform.h`)

The app runs on the computer against the real `gfx.c`, `panel.c`, usb.c's MIDI rings, `midi.c` and `store.c`, with
the hardware replaced by plain state. A test includes it and drives the app:

- `host_boot()`: power-on (the GREY palette, the panel map, USB configured, `app_init`); the flash (`nor[]`) is
  erased once and then kept, so `memset` the app's state and call `host_boot()` again for a power cycle.
- `host_frame()`: one main-loop pass (`app_frame`, `fm1_ms` + 16); `host_blits` / `host_pixels` count what it sent.
- Input: `fm1_in.notes = 1u << k` (keys), `host_press(B_PLAY, 1)` / `0`, `host_turn(EN_K1, steps)`.
- Sound: `host_render(buf, frames)` (a multiple of 32) calls `app_audio`; `buf` is stereo Q15.
- MIDI: `host_midi(0x90, 60, 100)` into the input ring; `host_midi_out(&pkt)` takes what the app sent.
- Screen: `host_fb[240 * 240]` (RGB565), `host_png("build/shots/x.png")` (2x, via `tests/shot.py`, Pillow);
  `host_led[41]` (0..13 buttons by matrix id, 14..40 keys).
- Flash: `nor[]`, `host_nor_erase()`, `flash_ok`, `host_erases`.
- The layout lint: `host_clipped` counts text cut or outside its canvas strip and rounded cells outside it
  (`host_clip_what` names the last); check it after drawing every screen at its widest values.
- An app calling a HAL function the host lacks fails to compile here: add a stub to `host_platform.h` (plain state
  the test can set and read), as `fm1_adc_read` and `fm1_input_edges` are.

`tests/app_test.c` is the start app's test and the template: screens (drawn, lazy, redrawn after `app_redraw`, the
layout lint at the widest values, the large font covering the name), LEDs, the record in flash (save, power cycle,
another app's record, values out of range, no flash), sound (pitch by zero crossings, peak, no click as the largest
sample step, note-off, a lost MIDI stream, `app_audio_stop`). When the app changes, change these checks with it on
purpose, never by loosening them.

One test alone (the HAL's inline assembly needs optimisation on, so keep `-O1`):

```sh
cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm && build/host/app_test
```

A new test file: `tests/<feature>_test.c`, a `$CC ... -o "$OUT/x" tests/x.c -lm` line and a `run "what" "$OUT/x"`
line in `run_tests.sh` next to a similar one, and a line in its comment block.

**Prove a new test can fail:** break the code on purpose once (copy the file aside, change a line, rebuild, run,
restore) and watch it go red. A test that cannot fail tests nothing.

## Budgets

- `./build.sh` fails on less than 4 KB RAM or 8 KB pool headroom, an image over the app slot, a call from RAM code
  into the flash, a ROM reference, or register access outside `hal/`.
- `python3 tests/target_budget.py`: the static loop cost of `fm1_alnk0_irq` (with `app_audio` inlined into it, or
  called), `fm1_timer5_irq`, `aud_fade_half`, and every function named in `tests/target_budget.txt`, +10 % allowed.
  Name the app's render functions there (`name 0`) and run `BUDGET_UPDATE=1 python3 tests/target_budget.py` once to
  set their lines; after an intended change of the sound code, the same.
- `sh .claude/skills/fm1-platform/scripts/budget_report.sh`: everything above plus the flash left for code.
- Real CPU load is measured on the unit (`audio_cpu_q8`); the static cost and the emulator's speed (emu_test.mjs
  prints the real-time factor) are proxies. A static cost that jumps (a loop restructured into a nested one by a new
  branch) is worth a look even under +10 %.

## The browser emulator

`sh web/emu/build.sh` (needs `emcc`) builds `build/emu/{index.html, worklet.js, emu.wasm}`: the app on the host
platform as WebAssembly, the panel drawn by the page, driven by an AudioWorklet (the audio drives the clock; a
main-loop pass every 16 ms of audio). Serve it from localhost (AudioWorklet needs a secure context):

```sh
python3 -m http.server -d build/emu 8765 &
NODE_PATH=$(npm root -g) node .claude/skills/fm1-platform/scripts/emu_drive.js http://127.0.0.1:8765/ build/emu_shots
```

`scripts/emu_drive.js` (Playwright) switches it on, runs its `STEPS` (keys by computer key `z`..`/` and `q`..`p`,
buttons by printed label, knobs by role with arrow keys, waits, shots) and saves the screen and the page; edit
`STEPS` for the workflow to check, then read the PNGs. It prints console errors. Gotchas: a focused knob takes the
keyboard (the script blurs it after turning); the page sends a key or button change at most every 24 ms of device
time (the FM-1 debounces), so hold a key a few hundred ms.

Not emulated: the boot path, the flash across page reloads, USB enumeration, updates, UBOOT, the crash screen, real
timing (the WebAssembly runs far faster than the chip). Those are covered by the platform's host tests, or only by
the unit.

`build/emu` can be published as it is (any static host), or as a claude.ai Artifact page with `emu.wasm` and
`worklet.js` as supporting files.

## CI

`.github/workflows/ci.yml` runs the setup script, `./build.sh` and `tests/run_tests.sh` on every push and pull
request, and keeps `build/firmware.fwsc`, `build/shots/` and the test log as artifacts. The toolchain and the SDK
come from JieLi's servers and Gitee: a failure there is the network, not the code (re-run).
