# FM1-Firmware-VibeCoding-Template

This is a repo-template to start your own firmware with Claude Code or Codex.

A repository made from it is the **FM-1 Firmware Kit** before its first step: a starting point for your own firmware on the
**M-VAVE FM-1** pocket synthesizer, built on the platform of [Felucca](https://github.com/hugelton/Felucca)
(Leo Kuroshita, Hügelton Instruments).

It is made from an interview, not by hand:

0. **Use this template** (GitHub: "Use this template", or copy the files into a new, empty repository).
1. **Open the new repository in Claude Code** (claude.ai/code, the desktop app or the CLI). Claude starts the interview
   at once: what the instrument is, its sound, what the keys, buttons and knobs do, its screens, what it stores, MIDI,
   its name and who makes it. (Until then it refuses to change any other file: `.claude/settings.json`, `setup/gate.py`.)
2. From your answers it writes the spec (`docs/SPEC.md`) and the setup (`setup/project.json`) and asks you to confirm
   both.
3. Then `python3 setup/new_project.py` makes the repository: the kit moves out of `template/`, gets your firmware's
   name, version, id, README, licence lines and options. Claude builds the first skeleton of your firmware from the
   spec, with tests, and shows it to you in the browser emulator.

| Here now | What it is |
| --- | --- |
| `template/` | the kit: the platform (boot path, update loader, USB-MIDI, flash, display, panel, audio), a start app, tests, the web installer and emulator, tools |
| `setup/` | the interview's gate and the generator (removed once the repository is made) |
| `.claude/skills/` | `fm1-new-project` (the interview and the setup), `fm1-platform` (the rules for every later change), `graphify` (the code graph) |
| `LICENSE` | GPL-3.0, as Felucca; details in `template/LICENSING.md` |

With Codex: `AGENTS.md` asks for the same interview first (without the hook that enforces it).

Without an agent: write `docs/SPEC.md` and `setup/project.json` yourself (`setup/project.example.json`, the
fields are explained at the top of `setup/new_project.py`), set `"confirmed": true`, run
`python3 setup/new_project.py`, then follow `BUILDING.md`.

The FM-1 has one firmware bank and no recovery button: nothing is installed on a device during the setup, and the
first install of a new firmware should be done with recovery hardware at hand (the generated README explains).

M-VAVE and FM-1 are trademarks of their respective owners. This kit is not affiliated with or endorsed by them, nor
by Hügelton Instruments.
