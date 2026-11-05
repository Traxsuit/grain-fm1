# M-VAVE FM-1 V15 recovery image (Apache-2.0)

`FM-1_015.fwsc` is M-VAVE's official FM-1 firmware V15, unmodified, as distributed in M-VAVE's
FM1 Open development kit (<https://github.com/m-vave/fm1_open>, `sdk/firmware/FM-1_015.fwsc`,
release V13, 2026-10-17). M-VAVE distributes it under the Apache License, Version 2.0
(`../../LICENSES/Apache-2.0.txt`; FM1 Open `THIRD_PARTY.md`, "Boot/update resources and recovery
firmware"). Copyright stays with M-VAVE (珠海笙科智能科技有限公司). It is not modified here and is not
part of this firmware: the installers offer it only to return an FM-1 to the stock
firmware.

- size 699 956 bytes
- SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` (the same file as M-VAVE's
  FM-1.fwsc V15 download; `web/fm1pkg.js` STOCK_V15_SHA256, `tools/fm1_install.py` STOCK_V15_SHA256)

Returning to V15 replaces the firmware; V15 cannot read what this firmware stored. The installer does
not back that up: an app that stores music or sounds should give its users a way to save them first.
