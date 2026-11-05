# GRAIN-FM1

A silent USB MIDI controller firmware for the M-VAVE FM-1 that drives Ableton Live's Granulator III with pages of knobs, a parameter overview and a grain visualiser.

Custom firmware for the M-VAVE FM-1, made from the FM-1 Firmware Kit: Felucca's platform (GPL-3.0-only, via
M-Vave-OP1) in `firmware/src/`, `hal/` and `loader/`, this firmware in `firmware/app/`. What it does and how it plays:
`docs/SPEC.md` (keep it current); how the repository was set up: `docs/PROJECT.json`.

- **Any firmware work:** load the `fm1-platform` skill (`.claude/skills/fm1-platform/SKILL.md`) first: it holds the
  app contract, the budgets, the test loop and the safety rules for installing on a real FM-1.
- While `firmware/app/` still holds the kit's start app (a name screen, a triangle wave, SAVE keeps the knobs), the
  repository's setup is not finished: continue the `fm1-new-project` skill at its step 5 (the skeleton from
  docs/SPEC.md).

Code, comments and docs in the repository are in English; talk to the user in their language.

## graphify

This project can have a knowledge graph of its code at `graphify-out/` (graphify, `.claude/skills/graphify/`): the
functions, the files that hold them and who calls whom, with community structure and god nodes. It is built
locally from the code alone (tree-sitter, no LLM, no API key) and is not in git (`.gitignore`): rebuild it in a
fresh checkout or container, ~10 s:

```sh
uv tool install graphifyy==0.9.97          # or pipx / pip; setup_env.sh does this
graphify extract . --code-only             # graphify-out/graph.json
graphify cluster-only . --no-label         # GRAPH_REPORT.md, graph.html
```

Rules:
- When graphify-out/graph.json exists, run `graphify query "<question>"` before your first grep, search or
  source-file read for a codebase question, and put this rule in every subagent prompt that explores code. Read
  raw files only for lines the graph or an earlier read already located. Use `graphify path "<A>" "<B>"` for
  relationships, `graphify explain "<concept>"` for focused concepts and `graphify affected "<X>"` for what a change
  to X reaches. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface
  enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
- What the graph cannot see here: the firmware is a unity build (`firmware/src/firmware.c` includes the other `.c`
  files and `firmware/app/app.c`), so most calls between files are matched by name and marked INFERRED; a few
  macro-heavy files parse only in part. What the audio ISR reaches (`app_audio` and what it calls) and calls through
  function pointers are not in it. For those, and for anything about budgets, timing or stored formats, the code and
  the `fm1-platform` references are the authority.
