#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 M-Vave-OP1 contributors
"""Make the site (any static host, GitHub Pages, or a local server for testing):

  index.html                  redirect to the installer
  firmware/firmware-VER.fwsc  the package (+ LICENSE, LICENSING.md, LICENSES/: the package holds
                              JieLi SDK files under Apache-2.0, see LICENSING.md)
  firmware/FM-1_015.fwsc      M-VAVE's official V15 (Apache-2.0, vendor/m-vave/), for the installer's
                              return to V15 without a separate download
  webapp/installer/index.html installer.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/emu/                 the browser emulator (build/emu of web/emu/build.sh), when built

  web/make_site.py build/firmware.fwsc VERSION OUT_DIR [NAME]

The package must be one made by tools/build.py (the platform's own loader, no vendor files). Its identity
(FM-1_9xx) is read from the package; the device must report it after the install. Try it locally:
  python3 web/make_site.py build/firmware.fwsc dev build/site && python3 -m http.server -d build/site 8000
  then http://localhost:8000/webapp/installer/ in Chrome or Edge (WebMIDI needs localhost or https).
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def app_name():
    m = re.search(r'^#define APP_NAME "([^"]*)"', (ROOT / "firmware/app/app.c").read_text(), re.M)
    return m.group(1) if m else "Firmware"


def main(pkg, version, out, name=None):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    name = name or app_name()
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not one of this platform's packages (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # the loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: not made by tools/build.py (no loader marker); the site ships only those "
                         "(a package patched from an official one carries vendor files)")
    html = (HERE / "installer.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    file = f"firmware-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    stock = ROOT / "vendor" / "m-vave" / "FM-1_015.fwsc"
    meta = json.dumps({"name": name, "version": version, "product": product, "pkg": "../../firmware/" + file,
                       "stock": "../../firmware/FM-1_015.fwsc" if stock.exists() else ""})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"installer.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, fw = out / "webapp" / "installer", out / "firmware"
    for d in (inst, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("firmware-*.fwsc"):          # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / file)
    if stock.exists():                              # (its notice beside it: vendor/m-vave/README.md)
        shutil.copy(stock, fw / "FM-1_015.fwsc")
        shutil.copy(stock.parent / "README.md", fw / "FM-1_015-README.md")
    lic = ROOT / "LICENSES"                         # the package holds JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with the firmware's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the installer links this folder: static hosts list no folders
        '<!doctype html><meta charset="utf-8"><title>Licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(ROOT / doc, fw / doc)
    emu = ROOT / "build" / "emu"
    if (emu / "emu.wasm").exists():
        shutil.copytree(emu, out / "webapp" / "emu", dirs_exist_ok=True)
    (out / "index.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>{name}</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        f'<a href="webapp/installer/">{name} installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), firmware/{file} ({len(raw)} B, {product})")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
