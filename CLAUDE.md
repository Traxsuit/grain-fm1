# A new FM-1 firmware: not set up yet

This repository is the FM-1 Firmware Kit before its setup (the kit is in `template/`). **The interview comes first.**

- Load the `fm1-new-project` skill (`.claude/skills/fm1-new-project/SKILL.md`) and start its interview with the user
  in your first answer, whatever they asked; if they asked for something else, say that the interview comes first
  and why (the repository is made from it), then start it.
- Until `python3 setup/new_project.py` has made the repository, only `docs/SPEC.md` and `setup/project.json` may be
  written; a hook (`setup/gate.py`) refuses every other file change in the repository. Do not work around it (no
  shell edits of other files, no moving `template/` by hand).
- `setup/project.json` gets `"confirmed": true` only after the user has read and confirmed the spec and the answers
  in this conversation.

Talk to the user in their language; the repository's files are in English. The generator replaces this file with
the project's own CLAUDE.md.
