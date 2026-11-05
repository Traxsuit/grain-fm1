# GRAIN-FM1 — specification

Firmware for the M-VAVE FM-1 (Felucca platform / FM-1 Firmware Kit, GPL-3.0). Written with Claude Code's assistance.

## What it is

GRAIN-FM1 turns the FM-1 into a USB MIDI controller for Ableton Live's Granulator III (Max for Live). It makes no
sound: the synth engine stays silent. The four performance knobs send CCs to Live, which the user maps to
Granulator III parameters in Live's MIDI map mode; the 27 keys pass notes through. The screen shows the four
parameters of the current page, zooms in on the knob being turned, and draws a grain visualiser computed from the
firmware's own parameter state. Every page and parameter comes from **one data table** (`firmware/app/params.c`),
made to be edited by hand.

## Hardware facts that shape the design (from reading the platform code)

- KNOB 1..4, SELECT, ALGORITHM and PRESETS are **endless rotary encoders** (relative steps, `panel_enc`), not
  potentiometers. The firmware therefore *owns* every parameter value; a click changes the stored value and the new
  absolute value is sent. There is no physical position that can disagree with the stored value, so classic hard
  pickup and its ghost marker do not apply (the only potentiometer is MASTER, the volume).
- Keys have no velocity. MIDI out goes over USB only; the out queue holds 64 events.
- No full framebuffer: strips up to 240 x 124 are drawn off-screen and blitted; redraw is lazy (a signature per area).
- SysEx never reaches the app (usb.c keeps it for the update protocol).

## Controls

| Control | Action |
|---|---|
| KNOB 1..4 | Change parameter 1..4 of the current page by one step per click (accelerated when turned fast), clamped to its min..max; send its CC (absolute, 7-bit) at most once per frame per knob with the latest value. Marks the knob "touched" (highlight + touch zoom). |
| SELECT encoder | Next / previous page (wraps). |
| PRESETS encoder | Global MIDI channel 1..16 (shown in an overlay for ~1 s). Per-parameter channel in the table overrides it when set. |
| ALGORITHM encoder | Nothing yet: shows "SOON". |
| FX, SCL, ENV, LFO, EDIT, GLO (page buttons) | Jump to page 1..6 (when the table has that page; else "SOON"). Page LEDs: current page lit. |
| HOME | Page 1. |
| SAVE | Save the state (values, page, channel, octave) now; status "SAVED" / "SAVE FAILED". |
| OCT− / OCT+ | Shift the key range by an octave (−3 .. +3), shown on screen. Both held 5 s: reserved (recovery). |
| ARP, SEQ, PLAY, REC | Nothing yet: "SOON". |
| 27 keys (F3..G5) | Note on (velocity 100) / note off on the global channel, transposed by the octave. A note-off is always sent with the note number of its note-on, even if the octave changed while held. |
| MIDI in (USB) | v1: CC on the global channel whose number matches a parameter updates that parameter's value (no echo back out). This is the v2 hook; see below. |

## Screens (240 x 240, one screen)

- **Header (top ~24 px):** page name in the page's colour, MIDI channel, octave, USB status.
- **Overview (4 slots, 2 x 2 or 4 columns):** each slot has the parameter name, its value (the table's display
  units) and a small arc showing value within min..max in the page colour. The knob touched last is highlighted;
  the other three are dimmed until ~3 s after the last turn (about 1.5 s after the zoom has faded; all normal when idle).
- **Touch zoom (overlay):** turning a knob enlarges that parameter over the overview: a large number (`AF_L`) and a
  mini graph of its recent values (the last ~2 s); it fades back after ~1.2 s without a turn.
- **Grain visualiser (bottom strip, ~240 x 70):** a static pseudo-waveform; a playhead at Position; a band around it
  as wide as Spray; dots inside the band appearing at a rate set by Frequency (grain density), each sized by Grain
  Size. Driven from local parameter values (read from the table by role, whatever page is shown).
- **Overlays:** channel change, "SOON", save status.

Look: the HI-CON palette (black, white text), set in `ui_init` (the platform boots in GREY, which would turn every
mixed colour grey); each page gets its own colour from the table (its slots, arcs, header, zoom and visualiser band).

## The parameter table

One table; each row: page, slot (1..4), name (on screen, up to ~10 chars), CC number, MIDI channel (0 = global),
min, max, default, display format (raw 0..127, percent, Hz, ms...), role (none / position / spray / frequency /
grain size, used by the visualiser). Pages: name and colour. Values are 0..127 on the wire; min/max limit the range
sent. v1 contents (names to be finalised against Granulator III):

| Page | Colour | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
|---|---|---|---|---|---|
| 1 GRAIN (FX) | amber | Filter Freq CC 20 | Position CC 21 | Grain Size CC 22 | Variation CC 23 |
| 2 SPRAY (SCL) | ice | Spray CC 24 | Frequency CC 25 | (placeholder) CC 26 | (placeholder) CC 27 |

Defaults 64 except where the table says otherwise. Duplicate CC/channel pairs are refused by a test.

## Sound

None. `app_audio` writes silence; `app_audio_stop` releases nothing audible but sends note-offs for held keys.

## Timing and sequencing

None in v1. The MIDI clock is ignored.

## What is stored

One record (store.c, this app's `APP_ID`): every parameter value, current page, global channel, octave, and a table
version number (a changed table resets to its defaults). Loaded at boot (every value clamped to its row's min..max);
saved by SAVE and automatically ~5 s after the last change.

## MIDI

- Out (USB): CC per parameter; note on/off from the keys. No running status issues (USB MIDI packets).
- In (USB): CC → parameter update (v1 hook). TRS MIDI IN off.

### v2 (designed, not built)

A Max for Live device sends Granulator III's parameter values back as the same CCs, so the screen follows
automation and mouse edits: the in path above, already in v1. Optional downsampled waveform via SysEx needs the
platform's usb.c to pass a manufacturer ID (e.g. 0x7D non-commercial) SysEx to the app; that touches the update path
and is a separate, carefully reviewed step. Alternative without SysEx: waveform as a burst of CCs/NRPNs.

## Notes from the build

- Incoming MIDI is taken in the audio ISR and applied by the main loop's next frame (one frame, <= ~16 ms, later).
- A value changed on the FM-1 whose CC has not left yet loses to a CC from Live arriving in the same frame.
- Keys held through `app_audio_stop` (an update, UBOOT) stay silent until let go, so no note-on is sent without its off.
- The ISR-cost budget (`tests/target_budget.py`) and the firmware image need the JieLi toolchain, which this cloud
  environment could not download (pkgman.jieliapp.com and gitee.com refused by its network policy); the host tests,
  the sanitizer pass and the browser emulator ran.

## Open questions

- Granulator III's real parameter names and which CCs to use.
- Pages 3..6 and their colours.
- Whether 7-bit resolution is enough for Position and Filter Freq (14-bit CC pairs possible later).

## Out of scope for v1

Sound, on-device editing of CC/channel, sequencer/arpeggiator, MIDI clock, SysEx, TRS MIDI, relative CC modes.

## Milestones

1. (done) Skeleton: table, two pages, controls wired, CCs and notes out, overview, store; host tests for the CC mapping
   (clamping, channel, one CC per frame, note-off after an octave change) and the incoming-CC hook.
2. (done, in the skeleton) Touch zoom and dimming, with timing tests.
3. (done, first version) Grain visualiser; next: a real waveform once v2 sends one.
4. Emulator walkthrough and screenshots; budgets.
5. First hardware install, only after backup / recovery to stock V15 is walked through and the unit's version checked.
