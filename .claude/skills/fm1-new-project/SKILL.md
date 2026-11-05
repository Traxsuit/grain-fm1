---
name: fm1-new-project
description: Make a new FM-1 firmware repository from an interview - the mandatory first step of a repository made from the FM-1 Firmware Kit (template/ and setup/ present, "not set up yet"). Interview the user about the instrument (what it is, sound, controls, screens, what it stores, MIDI, name, authors), write docs/SPEC.md and setup/project.json, have the user confirm them, run setup/new_project.py to make the repository, then build the firmware skeleton from the spec with tests and emulator screenshots. Use it at the start of every session in such a repository, whatever the user asked first, and when the user wants to start, plan or set up a new firmware for the M-VAVE FM-1 ("neue Firmware", "eigenes Instrument", "FM1", "M-Vave"); also to finish the setup while firmware/app/ still holds the kit's start app.
---

# A new firmware from the FM-1 Firmware Kit

A repository made from the kit starts **not set up**: the kit waits in `template/`, `setup/` holds the generator
and the gate, and hooks refuse every file change but the interview's own files. The repository is made from the
interview: the answers become `docs/SPEC.md` and `setup/project.json`, the user confirms them, `setup/new_project.py`
makes the repository, and the firmware's skeleton is built from the spec. Talk to the user in their language; the
repository's files are in English. The rules of `.claude/skills/fm1-platform/SKILL.md` apply to everything here (read
it before step 5).

## 0. Where are we?

- `template/` and `setup/` exist: not set up. Start the interview (step 1) in your first answer, even if the user
  asked for something else: say in one sentence that the repository is made from the interview, then ask.
- No `template/`, but `firmware/app/app.c` still holds the kit's start app (a name screen, a triangle wave, SAVE
  keeps the knobs; `docs/PROJECT.json` exists): the repository is made, the skeleton is not: go on at step 5.
- Otherwise: this skill is done; use fm1-platform.

While the interview runs, the tools can install in the background (it takes minutes, Gitee may need a retry):
`sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk` (works before the setup; it only installs, outside
the repository).

## 1. Interview

Ask with the question tool (AskUserQuestion), at most four questions at a time, each standing on its own, with
concrete options and a recommended one ("Other" takes free text). Go on until every item below has an answer; when an
answer opens a new question, ask it. Do not ask what the code can decide (file layout, fixed-point formats). Say
early when a wish does not fit the hardware or the budgets (audio input, many voices of a heavy engine, long samples,
wireless) and offer what does. [references/app-shapes.md](references/app-shapes.md) has sketches and rough costs of
the usual kinds on the FM-1's controls: use them to make options concrete.

1. **What is it?** One sentence in the user's words, then the kind: synthesizer, drum machine / groovebox, sampler of
   built-in sounds, step sequencer / MIDI controller (sound optional), utility or toy, something else.
2. **Sound:** none / mono / how many voices; the engine; effects (delay, reverb, chorus cost RAM and ISR time); what
   the knobs shape. The FM-1 has **no audio input**.
3. **Playing and controls:** what the 27 keys do (notes, steps, pads, modes); each of the 14 buttons (printed FX SCL
   ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT− OCT+: reuse their meaning where it fits); the 7 encoders (SELECT,
   ALGORITHM, PRESETS, KNOB 1..4); held buttons, double taps. OCT− + OCT+ held 5 s is taken (recovery).
4. **Screens:** which screens or pages, what each shows (240 × 240), the look (a palette, light or dark).
5. **Sequencing and time:** none, a step sequencer (steps, tracks, length), patterns, songs, an arpeggiator; tempo,
   MIDI clock in.
6. **What it keeps over a power-off:** settings, sounds, patterns, projects, how many of each (the store: 36 records
   of 4 KB; bigger plans now).
7. **MIDI:** in over USB and TRS (notes, CCs, clock); out over USB. TRS MIDI IN can be left off.
8. **The repository:** the instrument's name on the screen (capitals A-Y, digits, space, `# + - . /`, at most 16;
   the large font has no Z and no lowercase), its title in the docs, a one-sentence description, the version to start
   at (0.1), the authors for the copyright lines, whether it gets the GitHub Actions workflow (CI) and the code graph
   (graphify) (both recommended).

## 2. The spec and the setup file

Write **`docs/SPEC.md`** (English) from the answers, in this order: what it is (a paragraph); a controls table (every
key, button, encoder and gesture: what it does, per mode); the screens (one subsection each, what is drawn where);
the sound (voices, engine, parameters with ranges, effects); timing and sequencing; what is stored (records, slots);
MIDI in and out; open questions; out of scope for v1; the milestones (the skeleton first, then features in an order
where each can be tested and heard in the emulator).

Write **`setup/project.json`** (fields at the top of `setup/new_project.py`, an example in
`setup/project.example.json`) with `"confirmed": false`, and check it: `python3 setup/new_project.py --dry-run`
(it lists every problem, e.g. a name the large font cannot draw).

## 3. Confirmation

Show the user a short summary of the spec and the setup (name, title, description, version, authors, options) and
the file paths, and ask whether it is right. Change what they correct, show again. Only after they confirm in this
conversation: set `"confirmed": true` in `setup/project.json`. Never set it on your own judgement.

## 4. Make the repository

```sh
python3 setup/new_project.py
```

It moves the kit out of `template/`, writes the name, version and a new app id into `firmware/app/app.c`, the
project's README.md, CLAUDE.md and copyright line, applies the options (TRS MIDI, CI, graphify), writes
`docs/PROJECT.json` and removes `setup/` and `template/` (the gate goes with it). If it refuses, fix what it lists.
Then: `sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk` (if not done), `./build.sh`,
`. ~/emsdk/emsdk_env.sh; tests/run_tests.sh > build/tests.log 2>&1`: the last line must say `ALL HOST TESTS PASSED`
(the kit's start app under the new name). If not, fix the environment first (never the app's fault here) and tell the
user what was missing. Commit this state ("Set up <title> from the FM-1 Firmware Kit") if the user works with git.

## 5. The skeleton

Replace the start app with the skeleton of the spec, in `firmware/app/` (fm1-platform `references/recipes.md`):

- `app.c`: the five hooks and the includes; each part in its own file (`sound.c`, `ui.c`, `seq.c`, `data.c`, as the
  spec needs), included in dependency order. New files: the SPDX header and `Copyright (C) <year> <authors>`.
- Every control from the controls table wired to at least a visible effect (a value on screen, a note), so the
  emulator shows the whole surface; features not built yet say so on screen ("SOON") rather than doing nothing.
- The sound path complete for one voice or one sound if the spec has sound: keys and MIDI in, the note-off rules,
  `app_audio_stop`; the full engine comes in the milestones.
- The stored record(s) with defaults, loaded in `app_init`, saved where the spec says (store.c, `APP_ID`).
- The platform's patterns: lazy drawing with a signature per screen and `app_redraw`; ISR state behind `volatile`
  requests; all-zero valid start state for pool buffers.
- Do not touch the package identity, the USB name, `firmware/loader/`, the boot guard or the update path.

## 6. Tests

Rewrite `tests/app_test.c` for the new app (keep its helpers and its kinds of checks): every screen drawn and
`host_clipped == 0` at its widest values with a PNG in `build/shots/`; the controls table's main rows driven and
checked; the stored record (save, power cycle, another app's record, damaged values); the sound (pitch, level, no
click, silence after release, note-offs) if there is sound. Prove each new check can fail once. Add the render
functions to `tests/target_budget.txt` and run `BUDGET_UPDATE=1 python3 tests/target_budget.py`. Change `STEPS` in
`.claude/skills/fm1-platform/scripts/emu_drive.js` to walk through the new screens.

## 7. Green, and shown

1. `./build.sh` and `tests/run_tests.sh`: `ALL HOST TESTS PASSED`, nothing skipped that can run here.
2. `sh web/emu/build.sh`, serve `build/emu`, `emu_drive.js`: no console errors; look at every PNG.
3. Show the user: the screenshots (or the emulator page, published if they want to try it), the budgets
   (`budget_report.sh`), the next milestone of the spec. Update `docs/SPEC.md` where the build taught something.
4. Commit ("<title>: the skeleton from docs/SPEC.md"); push as the user's instructions or environment say.

## 8. Before the first install

The skeleton has never run on an FM-1. Say so plainly, and before any install: everything green, the user's explicit
go-ahead, the recovery hardware (FM-1-transporter) ideally at hand, the way back (`fm1_install.py --stock`)
explained. Then follow fm1-platform's Safety section, and ask the user to tell you what the unit showed.
