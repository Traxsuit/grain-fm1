# A new FM-1 firmware: not set up yet (for Codex and other agents)

This repository is the FM-1 Firmware Kit before its setup (the kit is in `template/`). **The interview comes first**,
exactly as for Claude Code (`CLAUDE.md`):

1. Read `.claude/skills/fm1-new-project/SKILL.md` and follow it from step 0: interview the user in their language
   (what the firmware is, sound, controls, screens, what it stores, MIDI, name, authors), write `docs/SPEC.md` and
   `setup/project.json`, have the user confirm both.
2. Until `python3 setup/new_project.py` has made the repository, change no other file than `docs/SPEC.md` and
   `setup/project.json` (Claude Code enforces this with a hook, `setup/gate.py`; you keep to it yourself).
3. Set `"confirmed": true` only after the user confirmed in the conversation; then run the generator and go on with
   the skill's later steps. `.claude/skills/fm1-platform/SKILL.md` holds the rules for every change after that.
