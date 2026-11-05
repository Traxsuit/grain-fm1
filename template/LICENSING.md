# Licensing

This kit, and every firmware made from it, is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). Its platform code comes from **Felucca** by Leo Kuroshita (@kurogedelic),
Hügelton Instruments (<https://github.com/hugelton/Felucca>), through **M-Vave-OP1**
(<https://github.com/janschulten/m-vave-op1>). A firmware built from the kit is a derived work of Felucca: it stays
GPL-3.0-only, keeps every licence header, and credits Felucca wherever it is published.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Copyright (C) 2026 M-Vave-OP1 contributors

## What is GPL-3.0-only

Every file that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (the platform in `src/`, the HAL, the update loader, the app in `app/`)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, browser emulator) and their tests: `web/`
- the host tests: `tests/`
- the Claude Code skills `fm1-platform` and `fm1-new-project`: `.claude/skills/`

If you distribute a firmware made from this kit, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with it inside.

When you add code ported from elsewhere, keep its licence header and add a row below (and its licence text to
`LICENSES/`). MIT, BSD and Apache-2.0 code can be combined with GPL-3.0; GPL-2.0-only cannot.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter Tight font by The Inter Project Authors: the UI text, rasterised into the firmware at build time (`tools/gen_aa_font.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| X0X by charlesvestal (<https://github.com/charlesvestal/fm1-x0x>), a Felucca fork: the design of the browser emulator (the worklet, the device clock driven by its audio); its files credit it in their headers | GPL-3.0 | `web/emu/` |
| graphify by Safi Shamsi and the Graphify contributors (<https://github.com/Graphify-Labs/graphify>): its Claude Code skill (a development aid for Claude's code graph: CLAUDE.md; nothing of it is in the firmware or the web pages) | Apache-2.0 (parts also MIT) | `.claude/skills/graphify/` (with its `LICENSE`, `LICENSE-MIT` and `NOTICE`) |
| JieLi AC79 SDK by JieLi Technology: three of its files go into every `.fwsc` package (below); none are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt` |
| M-VAVE's official FM-1 firmware V15, unmodified, for the installers' return to the stock firmware; never part of a package | Apache-2.0 | `vendor/m-vave/` (its `README.md`), `LICENSES/Apache-2.0.txt` |

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` (with `tools/fm1pkg_make.py`) holds three unmodified files from the JieLi
AC79 SDK (<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`). They are read from your SDK checkout
at build time (see BUILDING.md); no SDK files are in this tree.

| File in the package | What it is |
| --- | --- |
| `uboot.boot` | the first-stage boot loader (SPL) |
| `cfg_tool.bin` | the chip configuration block |
| `eq_cfg_hw.bin` | the default hardware EQ table |

They are licensed under the Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`), not under the GPL, and their
copyright stays with JieLi Technology. Apache-2.0 files may be distributed together with GPL-3.0 code; the firmware
itself stays GPL-3.0-only. Every distribution of a package carries the licence texts with it: the release folder
(`tools/build.py --release`) and the site (`web/make_site.py`, next to the package in `firmware/` and linked from the
installer page).

## No vendor material in the firmware

No M-VAVE material is part of the firmware. It links no vendor code (in particular not M-VAVE's binary FM1 SDK) and
contains no vendor data, and the packages hold only the firmware and the three SDK files above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners. Firmware made from this kit is independent firmware that runs on FM-1 hardware. It is not
affiliated with, endorsed by or supported by those owners, nor by Hügelton Instruments.

## Radio

The platform never enables the Bluetooth / Wi-Fi radio of the hardware.
