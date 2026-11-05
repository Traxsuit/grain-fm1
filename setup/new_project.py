#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M-Vave-OP1 contributors
"""Make the repository from the interview: the FM-1 Firmware Kit in template/ becomes this project's firmware.

  python3 setup/new_project.py [--dry-run]

Reads setup/project.json (the interview's answers, written by Claude in the fm1-new-project skill and confirmed by
the user) and docs/SPEC.md (the spec from the same interview), then:

  - moves the kit out of template/ into the repository root
  - firmware/app/app.c: the name, the version, a new app id (its records in the flash)
  - README.md, CLAUDE.md: this project's (from the templates' {{...}} fields), LICENSING.md: its copyright line
  - the options: TRS MIDI IN on or off by default, the GitHub Actions workflow, the code graph (graphify)
  - .claude/settings.json: the project's hooks (the interview's gate goes)
  - docs/PROJECT.json: the answers as they were used; setup/ and template/ removed

It changes nothing if anything is missing or wrong (it says what). project.json:

  {"name": "NOISE BOX",                 shown in the large font: A-Y, digits, space, # + - . / (max 16)
   "title": "Noise Box",                the name in the docs (any characters)
   "description": "One sentence: what it is.",
   "kind": "synth",                     synth | drums | sequencer | sampler | utility | other (free text is fine)
   "version": "0.1",
   "authors": "Jane Doe",               the copyright holder(s) of the new code
   "year": 2026,                        (default: this year)
   "app_id": "0x1a2b3c4d",              (default: a new random one; never the kit's)
   "features": {"trs_midi": true, "ci": true, "graphify": true},
   "confirmed": true}                   only after the user confirmed docs/SPEC.md and these answers
"""
import datetime
import json
import os
import re
import secrets
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL = os.path.join(ROOT, "template")
KIT_APP_ID = 0x3054494B
BOOTSTRAP = {"README.md", "CLAUDE.md", "AGENTS.md"}      # the not-set-up repository's own: replaced by the template's
FEATURES = {"trs_midi": True, "ci": True, "graphify": True}


def p(*parts):
    return os.path.join(ROOT, *parts)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def large_chars():
    m = re.search(r'^L_CHARS = "([^"]*)"', read(os.path.join(TPL, "tools", "gen_aa_font.py")), re.M)
    return set(m.group(1)) if m else set()


def check(cfg):
    """the answers, completed and checked: (cfg, errors)"""
    errors = []
    if not os.path.isdir(TPL) or os.path.isdir(p("firmware")):
        return cfg, ["this repository is already set up (no template/, or firmware/ exists): nothing to do"]
    if cfg.get("confirmed") is not True:
        errors.append('"confirmed" is not true: the user has to confirm docs/SPEC.md and the answers first')
    spec = p("docs", "SPEC.md")
    if not os.path.isfile(spec) or len(read(spec).strip()) < 200:
        errors.append("docs/SPEC.md is missing or nearly empty: write the spec from the interview first")
    name = cfg.get("name", "")
    bad = sorted(set(name) - large_chars())
    if not 1 <= len(name) <= 16 or bad or name != name.strip():
        errors.append(f'"name" {name!r}: 1..16 characters of A-Y, 0-9, space and # + - . / '
                      f"(the large font has no {''.join(bad) or 'leading or trailing space'})")
    cfg.setdefault("title", name.title())
    for k in ("title", "description", "authors"):
        if not str(cfg.get(k, "")).strip():
            errors.append(f'"{k}" is empty')
    if not re.fullmatch(r"\d+\.\d+(\.\d+)?", str(cfg.get("version", ""))):
        errors.append(f'"version" {cfg.get("version")!r}: like 0.1 or 1.0.2')
    cfg.setdefault("year", datetime.date.today().year)
    cfg.setdefault("kind", "other")
    app_id = cfg.get("app_id")
    if app_id is None:
        while True:
            v = secrets.randbits(32)
            if v not in (0, 0xFFFFFFFF, KIT_APP_ID):
                break
        cfg["app_id"] = f"0x{v:08X}"
    else:
        try:
            v = int(str(app_id), 16)
        except ValueError:
            v = -1
        if not 0 < v < 0xFFFFFFFF or v == KIT_APP_ID:
            errors.append(f'"app_id" {app_id!r}: a 32-bit hex value, not 0, not FFFFFFFF, not the kit\'s')
        cfg["app_id"] = f"0x{max(v, 0):08X}"
    feats = dict(FEATURES)
    for k, val in (cfg.get("features") or {}).items():
        if k not in FEATURES or not isinstance(val, bool):
            errors.append(f'"features": {k!r} unknown or not true / false (known: {", ".join(FEATURES)})')
        else:
            feats[k] = val
    cfg["features"] = feats
    for e in os.listdir(TPL):
        if e != "claude-settings.json" and e not in BOOTSTRAP and os.path.exists(p(e)):
            errors.append(f"{e} exists in the repository root and in template/: move it away first")
    return cfg, errors


def option(text, tag, on):
    """<!-- tag --> .. <!-- /tag -->: kept (the markers gone) or removed; on lines of their own or inline"""
    if not on:
        text = re.sub(rf"^<!-- {tag} -->\n.*?^<!-- /{tag} -->\n", "", text, flags=re.S | re.M)
        text = re.sub(rf"<!-- {tag} -->.*?<!-- /{tag} -->", "", text, flags=re.S)
    text = re.sub(rf"^<!-- /?{tag} -->\n", "", text, flags=re.M)
    return re.sub(rf"<!-- /?{tag} -->", "", text)


def fill(text, cfg):
    f = cfg["features"]
    text = option(text, "graphify", f["graphify"])
    text = option(text, "ci", f["ci"])
    for k in ("name", "title", "description", "kind", "version", "authors", "year", "app_id"):
        text = text.replace("{{" + k.upper() + "}}", str(cfg[k]))
    left = re.findall(r"\{\{[A-Z_]+\}\}", text)
    if left:
        raise SystemExit(f"new_project: unfilled template fields {left}")
    return text


def sub_once(text, pattern, repl, what):
    new, n = re.subn(pattern, repl, text, count=1, flags=re.M)
    if n != 1:
        raise SystemExit(f"new_project: {what} not found; the template changed")
    return new


def make(cfg):
    f = cfg["features"]
    settings = json.loads(read(os.path.join(TPL, "claude-settings.json")))
    os.remove(os.path.join(TPL, "claude-settings.json"))
    for e in sorted(os.listdir(TPL)):
        if e in BOOTSTRAP:
            os.remove(p(e))
        shutil.move(os.path.join(TPL, e), p(e))
    os.rmdir(TPL)

    app = read(p("firmware", "app", "app.c"))
    app = sub_once(app, r'^#define APP_NAME "[^"]*"', f'#define APP_NAME "{cfg["name"]}"', "APP_NAME")
    app = sub_once(app, r'^#define APP_VERSION "[^"]*"', f'#define APP_VERSION "v{cfg["version"]}"', "APP_VERSION")
    app = sub_once(app, r"^#define APP_ID 0x[0-9A-Fa-f]+u", f"#define APP_ID {cfg['app_id']}u", "APP_ID")
    write(p("firmware", "app", "app.c"), app)
    if not f["trs_midi"]:
        fw = read(p("firmware", "src", "firmware.c"))
        fw = sub_once(fw, r"^#define FELUCCA_UART 1 ", "#define FELUCCA_UART 0 ", "FELUCCA_UART")
        write(p("firmware", "src", "firmware.c"), fw)

    for doc in ("README.md", "CLAUDE.md", "AGENTS.md"):
        write(p(doc), fill(read(p(doc)), cfg))
    lic = read(p("LICENSING.md"))
    lic = sub_once(lic, r"^Copyright \(C\) 2026 M-Vave-OP1 contributors$",
                   f"Copyright (C) 2026 M-Vave-OP1 contributors\nCopyright (C) {cfg['year']} {cfg['authors']}",
                   "the copyright lines")
    write(p("LICENSING.md"), lic)

    if not f["ci"]:
        shutil.rmtree(p(".github"))
    if not f["graphify"]:
        shutil.rmtree(p(".claude", "skills", "graphify"), ignore_errors=True)
        settings = {}
    write(p(".claude", "settings.json"), json.dumps(settings, indent=2) + "\n")

    os.makedirs(p("docs"), exist_ok=True)
    record = {k: cfg[k] for k in ("name", "title", "description", "kind", "version", "authors", "year", "app_id",
                                  "features")}
    record["made"] = datetime.date.today().isoformat()
    write(p("docs", "PROJECT.json"), json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    shutil.rmtree(p("setup"))


def main():
    dry = "--dry-run" in sys.argv[1:]
    try:
        cfg = json.loads(read(p("setup", "project.json")))
    except FileNotFoundError:
        raise SystemExit("new_project: setup/project.json is missing: the interview writes it (fm1-new-project)")
    except ValueError as e:
        raise SystemExit(f"new_project: setup/project.json is not valid JSON: {e}")
    cfg, errors = check(cfg)
    if errors:
        print("new_project: nothing changed:")
        for e in errors:
            print("  -", e)
        return 1
    f = cfg["features"]
    print(f"new_project: {cfg['title']} ({cfg['name']} v{cfg['version']}, app id {cfg['app_id']}), {cfg['kind']}; "
          f"TRS MIDI {'on' if f['trs_midi'] else 'off'}, CI {'yes' if f['ci'] else 'no'}, "
          f"graphify {'yes' if f['graphify'] else 'no'}")
    if dry:
        print("new_project: --dry-run, nothing changed")
        return 0
    make(cfg)
    print("new_project: done. Next (fm1-new-project, step 4):")
    print("  sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk")
    print("  ./build.sh && tests/run_tests.sh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
