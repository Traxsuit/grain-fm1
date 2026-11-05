---
name: fm1-platform
description: Build, extend, test and safely ship custom firmware for the M-VAVE FM-1 pocket synthesizer (JieLi AC791N / WL82, pi32v2) made from the FM-1 Firmware Kit (Felucca's platform, an app in firmware/app/). Use this skill for any work on such a firmware - a screen, a sound engine, a sequencer, MIDI, saving to flash, the app contract (app_init, app_frame, app_audio), RAM / flash / audio-ISR budgets, the JieLi toolchain, build.sh or tests/run_tests.sh failures, the browser emulator, the web installer, installing a .fwsc on a real FM-1 or recovering one that no longer starts - even when the user only says "FM1", "M-Vave", "Firmware", "fwsc" or "meine Firmware". For setting up a brand-new firmware from the kit, use fm1-new-project first.
---

# FM-1 platform

A firmware made from the kit is C against a header-only HAL, built with JieLi's clang for pi32v2, packaged as a
`.fwsc` and installed over USB-MIDI. It has two layers:

- **The platform** (`firmware/src/`, `hal/`, `loader/`): Felucca's boot path, boot-loop guard, update loader and
  update entry, USB-MIDI, flash driver and record store, display and drawing, panel map, audio output. Proven on real
  units in Felucca and M-Vave-OP1. Change it only on purpose.
- **The app** (`firmware/app/`): everything that makes the instrument what it is, behind the contract in
  `firmware/app/app.h` (`app_init`, `app_frame`, `app_audio`, `app_audio_stop`, `app_fault_name`).

Licence: GPL-3.0-only, derived from Felucca by Leo Kuroshita (Hügelton Instruments). Keep the licence headers on
every file, add `Copyright (C) 2026 <the project's contributors>` to files you write, credit Felucca (and
M-Vave-OP1) in anything published. Code ported from elsewhere keeps its licence: add it to LICENSING.md.

The FM-1 has one firmware bank and no recovery button. A firmware that does not start needs extra hardware to
recover. So the working order is always: **host tests → browser emulator → careful install**, never the reverse.

## Where things are

| Need | Read |
| --- | --- |
| Chip, memory map, flash map, panel, display, audio, USB, packages | [references/hardware.md](references/hardware.md) |
| How the platform works, the app contract, contexts, drawing, input, LEDs, MIDI, the store, what is reserved | [references/platform.md](references/platform.md) |
| Step-by-step recipes: a screen, an engine, a sequencer clock, saving, MIDI out, a generated table, porting from Felucca | [references/recipes.md](references/recipes.md) |
| The test loop, the host platform, the layout lint, budgets, the emulator and Playwright | [references/testing.md](references/testing.md) |
| Who calls what, what a change reaches (the code graph: `graphify query / path / explain / affected`) | `CLAUDE.md` |

Read the reference that matches the task before editing code; they record the traps that cost time.

## Set up once per machine or container

```sh
sh .claude/skills/fm1-platform/scripts/setup_env.sh          # toolchain, AC79 SDK files, Python deps, graphify
sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk  # + Emscripten for the browser emulator
```

The script retries the Gitee clone (it resets connections now and then) and fetches only the three SDK files the
package needs. Linux x86-64 runs the JieLi toolchain natively; macOS needs Docker (BUILDING.md). Run every command
from the repository root.

## The loop for every change

1. **Plan against the budgets** before writing code (hardware.md): the audio ISR, RAM (`.data` + `.bss`, ~50 KB),
   the pool (~380 KB), flash for code (~580 KB), the store's 36 slots of 4 KB.
2. **Write the change in the code's own style.** Comments are dense prose that say *why* and name the numbers; new
   code reads the same. One `.c` per subsystem in `firmware/app/`, included by `app.c` (unity build: platform.md).
3. **Write or extend a host test first or alongside** (`tests/app_test.c`, or `tests/<feature>_test.c` registered
   in `tests/run_tests.sh`). Prove it can fail: break the code on purpose once and watch it go red.
4. **Build:** `./build.sh`. It refuses an image that breaks the platform's checks and reports image, RAM and pool.
   `sh .claude/skills/fm1-platform/scripts/budget_report.sh` prints those plus the ISR cost and the flash left.
5. **Run the whole suite:** `tests/run_tests.sh > build/tests.log 2>&1` (under a minute). It keeps going after a
   failure; the last line is `ALL HOST TESTS PASSED` or `HOST TESTS FAILED`. Find failures with
   `grep -nE "FAIL|OVER BUDGET|HOST TESTS" build/tests.log`.
6. **Look at it:** the test's screens in `build/shots/`, and the browser emulator (`sh web/emu/build.sh`, serve
   `build/emu`, `scripts/emu_drive.js`). Read the PNGs; a screen that only passes the lint can still look wrong.
7. **Install only when all of the above is green**, and only with the user's go-ahead (see Safety).

## Rules that keep the firmware healthy

- **Register access only in `firmware/hal/`.** The build checks `src/`, `app/` and `loader/`.
- **The audio ISR must stay cheap.** `app_audio` runs in it, 4 calls of 32 frames every 2.9 ms. O(1) work per block;
  no divides in sample loops; rare paths in `__attribute__((noinline))` functions; no drawing, no flash, no waiting.
  `python3 tests/target_budget.py`: each listed function may grow at most 10 % over `tests/target_budget.txt` (add
  the app's render functions there; `BUDGET_UPDATE=1` after an intended change).
- **Main loop ↔ ISR:** the main loop never edits state the ISR is using mid-block. It sets a `volatile` request field
  that the ISR takes at the start of a block (as `tone.stop_req` in the start app); the ISR publishes results the
  same way. MIDI in is drained in one place only (`midi_in_take`), normally the ISR.
- **Big buffers go to the pool:** `static T buf[N] __attribute__((section(".pool")));` (zeroed at boot, nothing else
  initialises it). Design state so all-zero is a valid empty state.
- **Lazy drawing:** a full-screen blit costs ~75 ms (SPI at 12 MHz). Redraw only what changed: hash every value a screen shows into
  its signature (`screen_sig` in the start app), or the screen will not update; redraw everything when the platform
  sets `app_redraw`.
- **Stored formats are append-only.** New fields at the end, the length tells a reader what is there; ids and enum
  values that were ever stored are never reused or reordered. Records carry the app's own `APP_ID`; check every
  loaded value (another firmware, an older version, a damaged sector).
- **Do not touch `firmware/loader/`, the boot guard and UBOOT paths in `main.c`, the update entry (`ota.c`,
  `ota_hw.c`, the SysEx path in `usb.c`), the package identity `FM-1_900` or the USB product name** without the user
  asking: they decide whether a bad image can still be replaced over USB. The installers find the FM-1 by its USB
  name (it must keep "Felucca" or "FM-1") and check the identity.
- **Reserved gestures:** OCT− + OCT+ held 5 s enters UBOOT (recovery). An app must not give that combination
  another meaning that is held that long.

## Safety: installing and recovering

- Ask before installing anything on a real FM-1, and say what is being installed. A firmware that has never run on
  hardware (the kit's start app, any new app) is a first install: say so, and recommend having the recovery hardware
  (FM-1-transporter, below) at hand.
- Install: `python3 tools/fm1_install.py build/firmware.fwsc` (needs `mido` and `python-rtmidi`), or the web
  installer: `python3 web/make_site.py build/firmware.fwsc dev build/site`, serve `build/site`, open
  `http://localhost:8000/webapp/installer/` in Chrome or Edge.
- Way back while the firmware still starts: `python3 tools/fm1_install.py --stock` (or the web installer's "Return
  to official V15", or M-VAVE's updater).
- If it no longer starts: the boot guard sends the unit to the chip's boot mode (USB device `WL80UBOOT`) after two
  boots that die in their first 30 s; OCT− + OCT+ held 5 s does the same while the firmware runs. From there
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) with a Seeed XIAO RP2040 on the FM-1's USB
  lines reads and writes the flash. Back the flash up first, then write the official `FM-1.fwsc`.
- A firmware that starts but hangs later (after 30 s) is not caught by the boot guard: the watchdog resets it, and
  the update entry still works while the main loop runs. Keep the main loop returning every frame.
- Without the recovery hardware, treat every install as possibly final: emulator and host tests first.

## Reporting back

Tell the user what changed, what was tested (suite result, budgets before → after, screenshots), and what was not
(anything only real hardware can show: timing feel, levels, the speaker, LED brightness, the first boot of a new
app). Translate for the user's language; code, comments and docs in the repository stay in English unless the user
asks otherwise.
