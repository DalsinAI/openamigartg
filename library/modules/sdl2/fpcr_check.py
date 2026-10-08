#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""fpcr_check.py OBJDUMP FILE...: GCC 16's -m68040 float-to-int conversion
saves FPCR in a data register, sets round-to-zero, stores with
fmove.l fpN,<ea> and puts FPCR back. Sometimes it saves FPCR in the very
register <ea> indexes with (found 8 October 2026: tiny_jpeg wrote every
coefficient to one place, so its JPEGs were flat grey). This lists each such
store and exits 1 if there is one, so a build can't ship it."""
import re
import subprocess
import sys

objdump, files = sys.argv[1], sys.argv[2:]
bad = 0
for path in files:
    out = subprocess.run([objdump, "-m", "m68k:68040", "-d", path], capture_output=True, text=True).stdout
    saved, fn = None, "?"
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.*)>:", line)
        if m:
            fn, saved = m.group(1), None
            continue
        m = re.search(r"fmovel %?fpcr,%?(d[0-7])", line)
        if m:
            saved = m.group(1)
            continue
        if saved and re.search(r"fmovel %?fp[0-7],", line):
            ea = line.split("fmovel", 1)[1].split(",", 1)[1]
            if saved in ea:
                print(f"{path}: {fn}: {line.strip()} (FPCR saved in {saved})")
                bad += 1
            saved = None
print(f"fpcr_check: {bad} float-to-int store(s) through the FPCR register" if bad else "fpcr_check: none")
sys.exit(1 if bad else 0)
