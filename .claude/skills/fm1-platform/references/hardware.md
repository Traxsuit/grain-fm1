# FM-1 hardware, as the firmware sees it

Sources: Felucca's HAL (`firmware/hal/*.h`), `firmware/app.ld`, `firmware/hal/fm1_flash.h`, the update loader
(`firmware/loader/ldr_core.c`), AL-255/FM-1-RE (reverse engineering of the stock firmware), measurements from
Felucca and M-Vave-OP1 builds.

## Chip and memory

- **MCU:** JieLi AC791N / WL82 family, pi32v2 CPU. Some strings in the stock
  firmware say `JL-BR22`: inherited library naming, not the chip.
- **Toolchain:** JieLi Linux toolchain, clang 4.0.1 for pi32v2 (`~/.jieli/toolchain`, from `pkgman.jieliapp.com`,
  `tools/get_toolchain.sh`). C99 with GNU extensions; no C++ runtime, no libc beyond `firmware/src/libc.c`
  (`memset`, `memcpy`, `memcmp`, `str_*`, `fmt_*`, `rng`). Newer toolchain drops produce slightly different code
  (ISR cost moves by a few %).
- **FPU:** the chip has one: JieLi's SDK builds for WL82 with `-mcpu=r3 -mfprev1`. The kit builds without (soft
  float; the platform's code is integer only). `FM1_FPU` (platform.h, `__attribute__((target("fprev1")))`) turns it
  on for one function; every float helper it calls needs the mark too, or that helper is not inlined and runs as soft
  float. Measured on a unit: a float multiply-add ~34 ns, a divide ~31 ns. objdump shows its 4-byte instructions as
  `3f e5` / `3f f5 <unknown instruction>` (tests/target_budget.py counts the pair once). Soft float in the audio ISR
  is far too slow: integer (Q15 / Q16 fixed point) or FM1_FPU.
- **SDK:** only three files of `fw-AC79_AIoT_SDK` (Apache-2.0) go into each package: `cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`. The firmware itself does not link the SDK.

### SRAM (512 KB, 0x01C00000 .. 0x01C80000), from app.ld

| Region | Address | Size | What |
| --- | --- | --- | --- |
| RAMTEXT | 0x01C00000 | 24 KB | code that runs while the flash is busy (the flash driver, `.ram_text`) |
| (debug) | 0x01C06000 | 8 KB | kept free for RAM-run debugging |
| RAM | 0x01C08000 | 50 KB | `.data` + `.bss`: what the ISRs touch, small variables |
| POOL | 0x01C14800 | 382 KB | `.pool`: big zero-initialised buffers (canvas, delay lines, sample memory, patterns) |
| stacks | 0x01C74000 .. 0x01C7C000 | 32 KB | user and system stacks with 256-byte guard bands |
| NOINIT | 0x01C7C000 | ~15 KB | survives resets and boot-mode entry (panel map, boot guard, crash record, `felucca_dbg`) |
| reserved | 0x01C7FD50 .. | | boot_info, boot-state mailbox, vector table |

The RAM / POOL split is only a budget line in the linker script; both are the same SRAM. Move it in `app.ld` when the
app needs it (POOL must start where RAM ends; build.py checks it).

### Budgets of the kit as shipped (start app; 2026-10)

| | Used | Limit | Note |
| --- | --- | --- | --- |
| App image (XIP) | ~64 KB | 581 564 B | the platform with fonts ~60 KB |
| RAM `.data`+`.bss` | ~5.4 KB | 51 200 B | build.py demands >= 4 096 B free |
| Pool | ~68 KB | 391 168 B | build.py demands >= 8 192 B free; the drawing canvas is 59.5 KB of it |
| Audio ISR static cost `fm1_alnk0_irq` | 867 | +10 % | `tests/target_budget.txt` |
| Scan ISR `fm1_timer5_irq` | 4 898 | +10 % | the key / LED scan and the USB poll |

For scale, a full multi-engine synth (M-Vave-OP1: 4 tracks, 17 engines, FX, sequencer, songs, scenes) uses 531 KB of
image, 42 KB of RAM and 378 KB of pool: the room is generous until it is not. Measure with
`scripts/budget_report.sh` after every change that adds tables, buffers or ISR work.

Real-time budget of the audio ISR: a half buffer (128 frames) every 2.9 ms; what the ISR does must fit well inside
(measured load `audio_cpu_q8`, /256; the start app shows it as CPU %). The main loop only runs between renders, so a
heavy ISR slows the UI; Felucca sheds voices above 85 %. Keep the app's load well below that.

### Flash (1 MB SPI NOR, XIP at 0x02000000)

| Range | What | Who may write |
| --- | --- | --- |
| 0x00000 .. 0x03FFF | boot code (SPL), chip config | nobody (only a full package install) |
| 0x04000 .. 0x92FFF | the app image (`app.ld` XIP: up to 0x8DFBC bytes) | the update loader |
| 0x93000 .. 0x96FFF | where update records may sit (the loader erases sectors that look like one after an install) | nobody |
| 0x97000 .. 0xDFFFF | **the store**: 72 sectors for the app's data (`store.c`: 36 slots, A/B) | the app (`st_*`, `store_*`) |
| 0xE0000 .. 0xE4FFF | update staging (the loader of a package being installed; the update record at 0xE4F00) | the update entry (ota.c) |
| 0xE5000 .. 0xE7FFF | Felucca's autosave and FM6 bank; allowed by `FL_STORE_OK`, but the loader erases sectors here that look like update records: avoid | (app, with care) |
| 0xE8000 | the stock firmware's VM backup | never written |
| 0xFC000 .. 0xFEFFF | settings area (Felucca's globals); allowed by `FL_STORE_OK` | the app (`st_*`) |

The store area is the one the update loader never touches. Every firmware made from the kit, Felucca and M-Vave-OP1
use it, and they replace one another on the same unit: **data in the store can be another firmware's.** Records
carry an app id (store.c) so a firmware reads another's data as empty. An install keeps the store; returning to
the official V15 leaves it too (V15 does not read it).

An erase stalls XIP (the whole CPU) for ~45 ms with interrupts off: storage_hw.c fades the audio out first and the
main loop fades it back in. Erase and write only from the main loop, when the user expects it (a SAVE press, the
transport stopped). A NOR sector takes typically ~100 000 erase cycles: no saving every frame or every knob turn.

## Panel

- **Keys:** 27, F3 .. G5 (key k = k semitones above F3, MIDI note 53 + k); 16 white, 11 black.
  `fm1_in.notes`, bit k = key k held (debounced). One 11-column × 6-row diode matrix behind two 74HC595, scanned by the
  ~10 kHz TIMER5 ISR, asymmetric debounce (press ~2 ms, release ~9 ms).
- **Buttons (14):** printed FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT− OCT+ (`panel.c` `B_*`, names in
  `B_NAME`). `fm1_in.buttons` has the matrix ids; `panel.btn[B_x]` maps a label to its id (`panel_held(B_x)`,
  `panel_mask(B_x)`).
- **Encoders (7):** SELECT, ALGORITHM, PRESETS, KNOB 1..4 (`EN_*`, names in `E_NAME`); `panel_enc(EN_x)` returns the
  detents turned since the last call, + = clockwise. Endless, no positions.
- **MASTER:** a potentiometer on the ADC (`fm1_adc_read`; Felucca's `master.c` reads it as the output level). The
  kit's start app does not use it.
- **LEDs:** one per button and key: `fm1_led_key(id, on)` (ids 0..13 buttons by matrix id, 14..40 keys), levels in
  `hal/fm1_input.h` (lit, dim glow, breath, mid: `fm1_led`, `fm1_led_dim`, `fm1_led_breath`, `fm1_led_mid`).
  PLAY has a second, green LED (Felucca: `LED_PLAY_GREEN`).

## Display, audio, I/O

- **Display:** ST7789-class 240 × 240 RGB565 on SPI1 at 12 MHz with DMA (PC9 CLK, PC10 DO, PC7 CS, PC8 D/C,
  backlight PA2 active low, which is also the key matrix's /OE). A 240 × 122 blit is ~53 KB, ~36 ms; the whole screen
  ~75 ms. The transfer runs while the main loop goes on (`lcd_sync` waits for it before the canvas is reused).
- **Audio:** I²S to an external codec at ~44.1 kHz (`FS`; the clock is really ~44 117.6 Hz), stereo, 24-bit words;
  half buffers of 128 frames (2.9 ms), rendered in blocks of 32 (`CTL`). The app writes Q15 (`±32767`), the platform
  shifts it to 24 bits with a −6 dBFS ceiling. Speaker and headphone out share the codec.
- **USB:** full-speed device, USB-MIDI (class compliant), VID 1209 / PID 0001 (pid.codes test id, as Felucca's),
  product name "Felucca". Felucca also offers USB audio to the host and a CDC serial console (`FELUCCA_UAC`,
  `FELUCCA_CDC` in usb.c); they need Felucca's app parts and are off in the kit.
- **MIDI:** TRS MIDI IN on UART1 (PH8), merged into the same input ring as USB.
- **No audio input** is used by any known firmware.
- **Battery:** ADC channel 3 through a divider (`fm1_adc_read(FM1_ADC_BATT)`).
- **Radio:** the chip has Bluetooth / Wi-Fi; no known firmware enables it, the platform never does.

## Packages and identity

`build.py` writes `build/firmware.fwsc`: app + the update loader + the three SDK files, identity `FM-1_900`; a
release (`./build.sh --release X.Y`) only changes the app's version string. The installers check the identity and
find the FM-1 by its USB name; keep both. The update loader in the package is byte for byte Felucca's / M-Vave-OP1's
(the one proven on real units): `firmware/loader/` is not to be changed.

Installing writes the app area only (0x04000 .. 0x92FFF, through the loader), so the boot code and the store stay. The update entry needs a running main loop (ota_service, usb.ota_req): a firmware whose main loop
hangs can only be replaced through UBOOT.
