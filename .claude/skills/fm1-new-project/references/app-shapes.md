# App shapes on the FM-1

Sketches to make the interview's options concrete. Each is a starting point, not a rule; mix them. The controls:
27 keys F3..G5 (16 white, 11 black, one row), 14 buttons printed FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
OCT− OCT+, 7 endless encoders SELECT ALGORITHM PRESETS KNOB 1..4, the MASTER pot, a 240 × 240 screen, one LED per key
and button, USB-MIDI in / out, TRS MIDI in, stereo out (speaker, headphones). No audio input.

## Synthesizer

- Keys play notes (OCT− / OCT+ shift octaves); KNOB 1..4 edit four parameters of the current page; PRESETS picks a
  sound, ALGORITHM the engine or a mode, SELECT the page; ENV / LFO / FX / EDIT open their pages; SAVE stores the sound.
- Sound: 4..8 voices of an oscillator / filter / envelope voice in fixed point is a light load; FM with 4..6 operators
  or a wavetable a medium one; physical models and reverbs a heavy one. A delay costs pool (1 s stereo at 16 bits:
  ~176 KB).
- Stores: sounds (a few hundred bytes each: 10 per record), settings.
- From Felucca: `dsp.c`, `voice.c`, an engine (`eng_analog.c`, `eng_fm6.c` + `fm6_core.c`, `eng_wtable.c`), `fx.c`.

## Drum machine / groovebox

- Keys are pads in play mode and the 16 steps (white keys) in step mode, black keys pick the track or the pattern;
  PLAY / REC as their names; SEQ toggles step mode; KNOB 1..4 per-track sound or per-step values.
- Sound: synthesized drums (Felucca's `drum_voice.c`: kick, snare, clap, hats, toms, rim, bell, cymbal) or short
  samples in flash (8-bit or ADPCM, generated tables: ~22 KB per second of 22 kHz mono at 8 bits, half that as 4-bit ADPCM).
- Timing: a sample-counting clock in the ISR, swing, MIDI clock in and out.
- Stores: patterns (16 steps × 8 tracks with velocity is ~0.5 KB), kits, songs (chains of patterns).
- From Felucca / M-Vave-OP1: `seq.c`, `drum_voice.c`, `eng_drum.c`, `kitlane.c`, `song_chain.c`.

## Step sequencer / MIDI controller

- No sound of its own (or a simple monitor voice): keys enter notes and steps, the encoders set gate, velocity,
  probability, CCs; PLAY / REC; MIDI out over USB (to a computer or a USB host) with notes, CCs and clock.
- The audio ISR still runs (the clock lives there: sample-accurate timing), it just writes silence or the monitor.
- Stores: sequences, CC maps.
- From Felucca: `seq.c`, `midi_clock.c`, the arp in `seq.c`.

## Sampler of built-in sounds / rompler

- Sounds generated or converted at build time into `build/gen/*.h` (flash for code: ~580 KB in all, minus the code);
  keys play them pitched or as a kit; KNOB 1..4 start, length, filter, envelope.
- Samples can also live in the store area (36 × 8 KB raw sectors) and come in over USB with a SysEx upload, as
  Felucca's user sample slots (`eng_sample.c`, `tools/fm1_sample_upload.py`): plan that protocol early.

## Utility, toy, game

- A tuner needs audio input (none); a test tone generator, a MIDI monitor, a metronome, a chord trainer, a small game
  on the 240 × 240 screen with the keys as controls all fit.
- Keep the platform's rules: `app_frame` returns every frame, the ISR stays light even if it only clicks.

## Rough costs (for the interview, before measuring)

| Thing | RAM / pool | Flash | Audio ISR |
| --- | --- | --- | --- |
| A simple voice (osc, filter, env), Q15 | ~100 B | a few KB of code | light |
| An FM voice (4..6 operators) | ~200 B | ~5 KB | medium |
| Stereo delay, 1 s, 16 bit | 176 KB pool | – | light |
| A reverb (Freeverb-like) | tens of KB of pool | a few KB | heavy |
| 1 s of 22 kHz mono 8-bit sample | – | 22 KB | light per voice |
| A 16-step, 8-track pattern | ~0.5 KB | – | none |

The memory figures follow from the sizes; the ISR column is an order of magnitude, not a measurement. Measure with
`budget_report.sh` (static cost, memory), the emulator's real-time factor, and on the unit `audio_cpu_q8`. Felucca
runs 8 voices of its engines across 4 parts plus chorus, delay and reverb on this chip, which shows the ceiling.
