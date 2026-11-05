#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M-Vave-OP1 contributors
"""The interview comes first: Claude Code hooks (.claude/settings.json) of a repository that is not set up yet.

  gate.py session   SessionStart: tells Claude to start the interview (its output goes into Claude's context)
  gate.py guard     PreToolUse (Edit, Write, NotebookEdit): refuses every file change in the repository but the
                    interview's own files (docs/SPEC.md, setup/project.json) until setup/new_project.py has made
                    the repository; exit 2 with the reason on stderr is how a hook refuses

Both do nothing once the repository is set up (no template/ any more); new_project.py removes this file and the
hooks then."""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWED = {"docs/SPEC.md", "setup/project.json"}


def set_up():
    return not os.path.isdir(os.path.join(ROOT, "template"))


def session():
    if set_up():
        return 0
    print("""This repository is a new FM-1 firmware that is NOT SET UP YET (the kit is still in template/).
Before anything else, load the fm1-new-project skill (.claude/skills/fm1-new-project/SKILL.md) and run its interview
with the user, in the user's language: what the firmware is, its sound, controls, screens, what it stores, MIDI, its
name and authors. The repository is made from the answers: docs/SPEC.md and setup/project.json, confirmed by the user,
then python3 setup/new_project.py. Until then, no other file in the repository may be changed (a hook refuses it).
If the user asks for something else first, tell them the interview comes first and offer to start it.""")
    return 0


def guard():
    if set_up():
        return 0
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return 0
    inp = data.get("tool_input") or {}
    path = inp.get("file_path") or inp.get("notebook_path") or ""
    if not path:
        return 0
    full = os.path.realpath(path if os.path.isabs(path) else os.path.join(data.get("cwd") or ROOT, path))
    root = os.path.realpath(ROOT)
    if not (full == root or full.startswith(root + os.sep)):
        return 0                                    # outside the repository (scratch files): not ours to guard
    rel = os.path.relpath(full, root).replace(os.sep, "/")
    if rel in ALLOWED:
        return 0
    sys.stderr.write(
        f"Refused: {rel} cannot change yet. This repository is not set up: run the interview first (fm1-new-project "
        "skill). Only docs/SPEC.md and setup/project.json may be written before python3 setup/new_project.py has "
        "made the repository from the confirmed answers.\n")
    return 2


if __name__ == "__main__":
    sys.exit({"session": session, "guard": guard}.get(sys.argv[1] if len(sys.argv) > 1 else "", lambda: 0)())
