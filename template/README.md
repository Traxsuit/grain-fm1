# {{TITLE}}

{{DESCRIPTION}}

Custom firmware for the **M-VAVE FM-1** pocket synthesizer, version {{VERSION}}, in development. What it is and how
it plays: [docs/SPEC.md](docs/SPEC.md).

Built on the **FM-1 Firmware Kit**: the platform of [Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita,
Hügelton Instruments) as [M-Vave-OP1](https://github.com/janschulten/m-vave-op1) runs it.

```
firmware/
  hal/        header-only HAL: every register access lives here          (platform, from Felucca)
  loader/     the update loader that writes a new app                     (platform, from Felucca; do not touch)
  src/        boot, main loop, audio ISR, USB-MIDI, update entry, flash,  (platform)
              display and drawing, panel map, MIDI helpers, record store
  app/        app.h: the contract; app.c and its files: this firmware
docs/         SPEC.md (what the firmware does), PROJECT.json (how the repository was set up)
tests/        host tests: the platform's update path, input, USB, store; the app on a simulated FM-1
web/          the web installer (with "Return to official V15") and the browser emulator
tools/        build, package, install, font generators
.claude/      Claude Code skills: fm1-platform (rules, budgets, tests, safety)<!-- graphify -->, graphify (code graph)<!-- /graphify -->
```

## Build, test, try

```sh
sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk   # once: toolchain, SDK files, Emscripten
./build.sh                                                     # build/firmware.fwsc
tests/run_tests.sh                                             # host tests, screenshots in build/shots/
sh web/emu/build.sh && python3 -m http.server -d build/emu 8000   # the emulator: http://localhost:8000/
```

Details: [BUILDING.md](BUILDING.md). With Claude Code, the `fm1-platform` skill holds the rules for every change.

## Safety

The FM-1 has **one firmware bank and no recovery button**. A firmware that does not start needs extra hardware to
recover. So the working order is always **host tests → browser emulator → careful install**, never the reverse.

- The platform keeps the ways back: the boot guard sends the unit to the chip's boot mode (`WL80UBOOT`) after two
  failed boots; OCT− and OCT+ held 5 s does the same on purpose; the update entry accepts a new package over USB
  while the firmware runs. Do not remove or bypass them, and do not change `firmware/loader/`, the boot guard, the
  package identity (`FM-1_900`) or the update path.
- Until this firmware has run on a real FM-1, have the recovery hardware at hand for an install:
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (a Seeed XIAO RP2040 on the FM-1's USB lines,
  which reads and writes the flash from the boot mode). Back up the flash first.
- Way back while a firmware still starts: `python3 tools/fm1_install.py --stock`, the web installer's "Return to
  official V15", or M-VAVE's own updater.

## Licence

GPL-3.0-only, as Felucca. Copyright (C) {{YEAR}} {{AUTHORS}}; the platform: Leo Kuroshita (Hügelton Instruments) and
the M-Vave-OP1 contributors. Keep the licence headers, credit Felucca (and M-Vave-OP1) wherever you publish, and give
your users the source. Details and third-party parts: [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. This firmware is not affiliated with or endorsed by them,
nor by Hügelton Instruments.
