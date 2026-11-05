# How the platform works, and what the app gets

## One compilation unit

`firmware/src/firmware.c` `#include`s every `.c` in dependency order; each file may use what the files above it
define. The HAL (`firmware/hal/*.h`) is header-only. Order:

```
HAL headers → platform.h → libc.c lcd.c gfx.c panel.c → usb.c midi_uart.c midi.c
→ app/app.h → audio.c → storage_hw.c store.c → ota.c ota_hw.c → app/app.c → main.c
```

- The app is one file as far as the build knows: `firmware/app/app.c`. Split a bigger app into files in
  `firmware/app/` and `#include` them from `app.c` (the include path has `firmware/app`); the host tests and the
  emulator then pick them up without edits (they include `app.c` too).
- Everything is `static`. Names are short and prefixed by subsystem (`tone_`, `seq_`, `ui_`; the platform's: `cv_`
  canvas, `lcd_`, `st_` / `store_` flash, `ota_`, `usb_`, `midi_`, `panel_`, `fm1_` HAL).
- `app.h` declares the five hooks before `audio.c` and `main.c` use them, and `app_redraw`.
- Generated headers (`build/gen/`: fonts, palettes, keycaps) come from `tools/gen_*.py` in `tools/build.py`
  `generate()`; an app adds its own generators there (wavetables, samples, lookup tables).
- `tools/size_fns.py` builds the main-loop code with `minsize` and keeps everything an interrupt reaches at `-Os`
  (from the IR, automatically): the app does not need to sort its functions.

## Boot

`fm1_cstart` (main.c): the watchdog (~8 s), the reset reason, **the boot guard** (two boots in a row that die in their
first 30 s enter UBOOT), the vector table, `.bss` and `.pool` zeroed, `.data` and `.ram_text` copied, the memory
guards. Then `fm1_main`: the flash checked (`flash_ok`), staging left by an update cleaned, the GREY palette, the LCD,
the input scan, the ADC, `panel_init`, **`app_init()`**, the audio (the ISR starts calling `app_audio`), USB, TRS MIDI,
TIMER5 (the scan and the USB poll), interrupts on.

`app_init` runs with the display on and the input HAL ready but before the audio and USB run: draw the first
screen, load what was saved (`store_load`), set up tables. Keep it short (well under a second; the boot guard's 30 s
count from power-on).

## The main loop

Every pass (main.c): feed the watchdog; give the sound back after a flash erase; retry USB if it did not come up;
clear the boot guard after 30 s; the **UBOOT hold** (OCT− + OCT+: a countdown over the whole screen from 2 s, UBOOT
at 5 s, letting go hands the screen back with `app_redraw = 1`); the **update entry** (`ota_service` answers the
updater's handshake; an upgrade request stops the app's sound with `app_audio_stop()`, shows the progress and only
returns if nothing was committed: `app_redraw = 1`); the UBOOT soft key from USB; then **`app_frame()`**; then it
waits until 15 ms have passed since the pass began (~60 frames a second at most; a slow frame just makes the next
come at once).

`app_frame` must return every frame: a loop that waits for something inside it stops the watchdog feed (reset
after ~8 s), the update entry and UBOOT. Long work (a big redraw, a save) is fine once in a while; spread anything
that takes seconds over frames.

## The audio ISR

`fm1_alnk0_irq` (audio.c), every half buffer (2.9 ms): `app_audio(out, CTL)` four times (32 frames each), then the
Q15 → 24-bit shift (−6 dBFS ceiling), the fade around flash erases, the load measurement (`audio_cpu_q8`, /256 of a
half; `audio_max_us`, `felucca_dbg.max_us`). TIMER5 (the scan, 10 kHz) nests into it; USB polls wait until it ends.

`app_audio(int32_t *out, uint32_t n)`: write `n` stereo frames, `out[2i]` left, `out[2i + 1]` right, in Q15
(±32767; values beyond are not clipped for you: saturate if a mix can exceed it). Everything that makes sound happens
here: read the keys (`fm1_in.notes`, written by the scan ISR, safe to read), drain MIDI (`midi_in_take`), run the
sequencer clock, render the voices. Rules:

- O(1) work per block; per-sample loops tight; no divides in them (shifts, reciprocal tables); `noinline` for rare
  paths (a note-on, a parameter change) so the hot loop stays small.
- No drawing, no flash, no waiting, nothing from the main loop's world that can block.
- State the ISR owns lives in a `static struct`; the main loop requests changes through `volatile` fields the ISR
  takes at the start of a block (`tone.stop_req` in the start app). Values the ISR only reads (a parameter) may be
  written directly if a torn read is harmless (a 32-bit aligned store is atomic).
- `app_audio_stop()` (main loop, before an update or UBOOT): request silence and the end of every note.

## Input

- `fm1_in.notes`: bit k = note key k held (0 = F3, MIDI 53 + k), debounced.
- `fm1_in.buttons`: bit = matrix button id; through the label map: `panel_held(B_PLAY)`, `panel_mask(B_PLAY)`,
  `B_NAME[B_PLAY]` (`"PLAY"`). A press is a bit set now and not in the last frame (the start app's `last_buttons`);
  `fm1_input_edges(&released)` (hal/fm1_input.h) collects edges between frames if a frame can be slow.
- `panel_enc(EN_K1)`: detents since the last call (+ = clockwise); SELECT, ALGORITHM, PRESETS, KNOB 1..4.
- Hold times and double taps: with `fm1_ms` (milliseconds since boot) in `app_frame`.
- The MASTER pot: `fm1_adc_read(FM1_ADC_...)` (hal/fm1_adc.h; Felucca's master.c filters it).

## LEDs

`fm1_led_key(id, on)`: ids 0..13 the buttons by matrix id (`panel.btn[B_x]`), 14..40 the note keys. The HAL also has
a dim glow, a breathing level and a mid level per LED (`fm1_led_dim[]`, `fm1_led_breath[]`, `fm1_led_mid[]` per scan
column, `fm1_led_dim_level`) and a power-on sweep (`fm1_led_anim_start`, hal/fm1_led_anim.h); Felucca's
`ui_input.c ui_leds` shows how they combine. Set the LEDs every frame from the app's state.

## MIDI

- In: `midi_in_take(&pkt, &source)` (midi.c), from **one** context (normally the start of `app_audio`). `pkt` is a
  USB-MIDI event packet: byte 0 cable << 4 | CIN, byte 1 status, bytes 2..3 data (`(pkt >> 8) & 0xF0` the message
  type, `pkt >> 16 & 0x7F` note, `pkt >> 24 & 0x7F` velocity). Channel messages and the clock (F8, FA, FB, FC) arrive;
  SysEx does not (usb.c keeps it for the update protocol). `MIDI_LOST`: the ring overflowed, events are gone:
  release every note. `source` 1 = USB, 2 = TRS. A note-on with velocity 0 is a note-off.
- Out: `midi_out(status, d1, d2)`: a channel message to the USB host, from one context (ISR or main loop). The
  ring is 64 events; it drops when the host is not reading.
- Your own SysEx protocol (an editor): Felucca's `editor.c` hooks the frames usb.c assembles (`ota_frame_get`) before
  `ota_service` takes them; keep the update protocol's frames (`F0 00 59 ...`, `F0 22 24 35 ...`) working.

## The screen

- `lcd_fill(x, y, w, h, colour)` fills a rectangle on the panel directly (a background, a clear).
- **The canvas** (gfx.c): `cv_begin(w, h, bg)` starts an off-screen strip (at most 240 × 124 pixels, `CV_MAX`, in
  the pool), the `cv_*` calls draw into it in strip coordinates, `cv_blit(x, y)` sends it to the panel by DMA. Draw a
  taller area in several strips (`cv_oy` shifts a drawing's y). Primitives: `cv_rect`, `cv_rrect` (rounded, r ≤ 8),
  `cv_frame`, `cv_line`, `cv_line_t` (thick), `cv_text` / `cv_text_on` / `cv_text_in` (centred in a width) /
  `cv_text_r` (right-aligned) / `cv_text_fit` (ellipsised), `cv_keycap` / `cv_key_hint` (button badges), `cv_alpha`
  (an alpha bitmap: your own icons).
- **Fonts:** `AF_S` 12 px, `AF_M` 15 px, `AF_L` 28 px (Inter Tight, anti-aliased, kerned). `text_w(f, s)` measures.
  `AF_S` and `AF_M` have printable ASCII and a few Latin-1 letters (`ÄÖÜäöü°©·`, the ellipsis); **`AF_L` is sparse:
  space, `#+-./`, digits and `A`..`Y`** (`tools/gen_aa_font.py L_CHARS`; add characters there; a missing glyph draws
  as a space).
- **Colours:** the theme tokens `T_BG`, `T_SURF`, `T_TEXT`, `T_MID`, `T_DIM`, `T_THEME`, `T_ACCENT`, `T_SEL`,
  `T_RAISE`, `T_REC`, `T_LINE` … (gfx.c), from the palette `palette_set(i)` picks (`UI_PALETTES`, `NPALETTES`,
  `UI_GREY_INDEX`; tools/gen_ui_palettes.py). Use tokens, not literals, so every palette works.
- `draw_text_line(x, y, w, font, s, fg, bg, align)` / `draw_text_box`: one-shot lines (they wait for the transfer).
- **Lazy drawing** (the pattern every Felucca screen uses): keep a signature of what a screen shows; each frame,
  compute it, and draw only if it changed. A value drawn but not in the signature never updates. Redraw everything
  when `app_redraw` is set (and clear it): the platform drew over the screen.
- The host tests' layout lint (`tests/host_platform.h`) counts text cut or outside its strip and rounded cells
  outside their strip (`host_clipped`); keep it 0.

## Saving

`store.c`: 36 slots of up to 4 076 bytes, each two sectors written in turn with a CRC-32 and a sequence number.
`store_save(slot, APP_ID, &data, sizeof data)` (0 = saved and read back), `store_load(slot, APP_ID, &data, sizeof
data)` (the length, or −1: none, damaged, or another app's). The newest valid copy wins; a save cut by a power loss
leaves the one before. `APP_ID` is the app's own 32-bit tag (app.c): another firmware's records read as empty.

- Load with defaults first, then the record over them: a shorter (older) record leaves the newer fields at their
  defaults. Check every loaded value's range.
- Main loop only; ~50 ms with the sound faded out and back; not every frame (flash wear).
- Bigger data (samples, long patterns): the raw calls `st_read(off, p, n)`, `st_erase(off)` (4 KB sector),
  `st_prog(off, p, n)` inside the store area (`STORE_LO` .. `STORE_HI`, and slots the app does not use for records),
  with a commit record of your own written last. `flash_ok` 0 (an unexpected flash chip): saving refused.

## Time

`fm1_ms` (ms since boot, from TIMER4 via the scan ISR), `fm1_ticks()` (TIMER4, 24 MHz, `FM1_TICKS_PER_US`),
`fm1_delay_ms` (busy wait: main loop only, short). In the ISR, count samples (`FS` per second) instead.

## Diagnostics

A fatal exception (bus error, guard hit, bad instruction) shows `app_fault_name()` and the CPU record on a blue screen
for 4 s, then resets (the boot guard counts it in the first 30 s). `felucca_dbg` (audio.c, `.noinit`) keeps the
ISR's worst time, late halves, boots, the main loop's last stage across resets. Felucca's CDC serial console
(console.c, `fm1t memr`) reads such records over USB; it is not in the kit.

## Reserved by the platform

- OCT− + OCT+ held 5 s: UBOOT. SysEx `F0 22 24 35 7D F7` (UBOOT) and `F0 22 24 35 7F F7` (upgrade), the
  M-UPGRADE frames.
- The update entry and loader, the boot guard, the package identity `FM-1_900`, the USB product name: see SKILL.md.
- Interrupts: ALNK0 (audio, priority 3) and TIMER5 (scan + USB, priority 4). An app needing another timer adds it
  in the HAL (`hal/fm1_timer.h`) at a lower priority.
- `.noinit`: `bootguard`, `fm1_crash`, `panel`, `felucca_dbg`. An app's own `.noinit` data must check a magic.

## What Felucca and M-Vave-OP1 have that the kit does not

The kit keeps the platform; Felucca's instrument is not in it. Port from
[Felucca](https://github.com/hugelton/Felucca) or [M-Vave-OP1](https://github.com/janschulten/m-vave-op1)
(both GPL-3.0-only; some parts keep MIT / Apache licences: see their LICENSING.md) when an app needs:

| What | Where (`firmware/src/`) |
| --- | --- |
| DSP helpers: oscillators, filters, envelopes, `mulq16`, tables | `dsp.c`, `tools/gen_tables.py` |
| Synth engines (analog, FM6, sample, drums, physical models, Plaits models, 303) | `eng_*.c`, `engines.c`, `fm6_core.c`, `drum_voice.c`, `plaits_*.c` |
| Voices and their shared budget, voice stealing | `voice.c` |
| Send effects (chorus, delay, reverbs), master | `fx.c`, `fx1.c`, `eq.c` |
| Step sequencer, arp, swing, ratchets, MIDI clock in | `seq.c`, `midi_clock.c`, `song_chain.c` |
| Pages, cards, menus, held layers, help hints, confirm dialogs | `ui*.c`, `menu_items.c`, `params.c` |
| Projects, presets, the autosave in flash | `storage.c`, `project.c`, `upreset.c`, `opstore.c` |
| Icons (the Fukiai font) | `icons.c`, `tools/gen_aa_icons.py` |
| USB audio to the host, the CDC serial console, a USB disk | `usb.c` (`FELUCCA_UAC`, `FELUCCA_CDC`, `FELUCCA_DISK`), `console.c`, `msc.c` |
| The MASTER pot as output level | `master.c` |
| The web editor (SysEx) | `editor*.c`, `web/editor.html` |

Ported code goes into `firmware/app/` (not `src/`), adapted to the app contract: its ISR part called from
`app_audio`, its UI from `app_frame`, its storage through `store.c` or the raw `st_*` in the store area. Keep the
original licence header and add the source to LICENSING.md.
