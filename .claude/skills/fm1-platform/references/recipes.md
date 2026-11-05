# Recipes

Each recipe lists every place that has to change. The start app (`firmware/app/app.c`) has a small example of most
of them; Felucca and M-Vave-OP1 have big ones (platform.md, "What Felucca and M-Vave-OP1 have").

## Name, version, id

`firmware/app/app.c`: `APP_NAME` (drawn in `AF_L`: capitals, digits, `#+-./` and space only, or add the glyphs to
`tools/gen_aa_font.py L_CHARS`), `APP_VERSION` (`./build.sh --release X.Y` sets it), `APP_ID` (a new random 32-bit
value per app: its records in the store). Then `web/emu/build.sh` and `web/make_site.py` pick the name up. Never change
`FELUCCA_ID` / `PRODUCT` (`FM-1_900`) or the USB product name.

## Split the app into files

`firmware/app/<part>.c`, `#include "<part>.c"` from `app.c` in dependency order (what a file uses above it). Keep
`app.c` for the five hooks and the includes. The host tests, the emulator and `tools/size_fns.py` see every file in
`firmware/app/` without edits.

## A screen

1. A draw function per area, each into a canvas strip: `cv_begin(240, h, T_BG)` (h ≤ 124), `cv_*`, `cv_blit(0, y)`.
2. A signature of everything the screen shows (FNV hash as `screen_sig`); `app_frame` draws only when it changes,
   everything when `app_redraw` is set.
3. Several screens (pages): a `page` field, one signature and one set of draw functions per page, a page change forces
   a full draw. Felucca's `params.c PAGES[]` and `ui_draw.c` show a full page system with cards, a panel and a footer
   (header 0..24, four cards 28..72, the panel 76..198, footer 202..240).
4. Text that can grow (names, lists): `text_w` to measure, `cv_text_fit` / `text_fit` to ellipsise, never let it
   run out of its strip.
5. Test: draw it at its widest values in `app_test.c` and check `host_clipped == 0`; save a PNG and look at it; check
   the emulator.

## A sound engine (in the ISR)

1. Its state in a `static struct` (voices, phases, envelopes); big tables `const` (flash) or generated
   (`tools/gen_*.py` → `build/gen/*.h`, called from `tools/build.py generate()`); big buffers in `.pool`.
2. `app_audio` renders it: per block, take requests (`volatile` fields from the main loop), take MIDI, update
   envelopes / modulation once per block, then the per-sample loop. Fixed point (Q15 samples, Q16 or Q24 phases and
   gains: `(int32_t)((int64_t)a * b >> 16)` where products can exceed 32 bits), or floats in an `FM1_FPU` function.
3. Polyphony: a fixed voice array, the oldest released voice stolen first; Felucca's `voice.c` does it with a shared
   budget. Every note-on needs its note-off (a key up, MIDI off, `MIDI_LOST`, `app_audio_stop`, a panic): store the
   pitch that sounds so the off matches.
4. Feedback paths (filters, delays, reverbs) round towards zero: `(x * g + (x < 0 ? 32767 : 0)) >> 15`; plain
   `>> 15` floors and leaves a −1 limit cycle (hum, DC) after the sound stops. Test that tails end in exact silence.
5. Saturate the mix to ±32767 before it leaves `app_audio`.
6. Budget: the render function's name in `tests/target_budget.txt`, `BUDGET_UPDATE=1`; check the emulator's
   real-time factor and, on the unit, `audio_cpu_q8`.
7. Test: pitch (zero crossings), level, no click (largest sample step at a note start and end), silence after the
   release, note-off matching, many notes at once (voice stealing), `MIDI_LOST`.

## A sequencer clock

In `app_audio`, count samples: `phase += n * tempo_step` per block, a step when it passes the step length (in
samples: `FS * 60 / bpm / 4` for 16ths; keep the remainder so tempo stays exact). The transport (start, stop) as
`volatile` requests from the main loop, taken at a block start; a stop sends every sounding note its off. MIDI clock
in: F8 (24 per quarter) from `midi_in_take` with `source`; Felucca's `midi_clock.c` smooths it. Swing, ratchets,
patterns and songs: Felucca's `seq.c`, `song_chain.c`.

## Saving settings or a project

1. A `struct` of plain fields (no pointers), the version-independent part first; new fields appended at the end.
2. Load in `app_init`: defaults first, then `store_load(slot, APP_ID, &s, sizeof s)` over them; clamp or reset every
   field that can be out of range.
3. Save on purpose (a SAVE press, leaving a menu, the transport stopped): `store_save(...)`, show the result (the
   start app's SAVED / FAILED). Not on every change, not from the ISR, not while a note should keep sounding (the
   erase fades the audio ~50 ms).
4. More than 4 076 bytes: several slots (a project in slots 1..4, say) or the raw `st_*` calls in unused slots'
   sectors with your own commit record written last.
5. Test: round trip, a power cycle (`host_boot` again), an older shorter record, another app's record, damaged
   values, `flash_ok` 0. Felucca's `tests/storage_test.c` and M-Vave-OP1's `tests/opstore_test.c` test torn writes and
   damaged copies on a NOR image.

## MIDI out

`midi_out(0x90 | ch, note, vel)` from one context (the ISR, where notes happen, keeps timing). Note-offs as
`0x80 | ch` or velocity 0. The USB host only; TRS MIDI is in only. A key that sends MIDI while it plays a voice: both
from the same place in `app_audio`.

## A held-button gesture

In `app_frame`: the time a button went down (`fm1_ms`), a hold when `fm1_ms - t0 > 400` while still down, a tap when
it comes up before that; double taps by the time between two taps (Felucca: ~300 ms). Never OCT− + OCT+ held (UBOOT
at 5 s).

## Using the FPU for one function

```c
static FM1_FPU float fast_tanh(float x) { ... }        /* every float helper it calls marked too */
static FM1_FPU void voice_render(int32_t *out, uint32_t n) { ... }
```

Check the disassembly (`build/firmware.dis`) for `3f e5` / `3f f5` instructions and no calls to soft-float helpers
(`__addsf3`, `__mulsf3`, ...) in the loop. The host build ignores the mark (plain float).

## Porting a part of Felucca or M-Vave-OP1

1. Copy the file(s) into `firmware/app/`, with their licence headers; add the source to LICENSING.md.
2. Find what it needs from Felucca's core (`core.h`: `track_t`, `song`, `P_*` parameters, `trk_note_on`) and cut it
   to the app's own state, or bring the minimum along. Felucca's code assumes its unity build order: include in the
   same order.
3. Its ISR part from `app_audio`, its UI from `app_frame`, its storage through `store.c`.
4. Budgets after each step; its tests (Felucca's `tests/`) ported to the host platform where they fit.

## A generated table

`tools/gen_<name>.py OUT.h` writes a `static const` table (Python, exact integers: no floats in the header if it
must be bit-exact), registered in `tools/build.py generate()` (`cmds`); the app includes `"<name>.h"` (build/gen is
on the include path, for the host tests and the emulator too).
