#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M-Vave-OP1 contributors
"""A screen from the host tests (PPM, tests/host_platform.h host_png) to a PNG, scaled 2x so it reads like the
FM-1's 1.3" panel on a computer screen.
  tests/shot.py IN.ppm OUT.png"""
import sys

from PIL import Image

Image.open(sys.argv[1]).resize((480, 480), Image.NEAREST).save(sys.argv[2])
