#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# Reads GCC's 68k assembler output and reports every conditional branch that
# is the first instruction after a label. Such a branch tests flags set on
# every path into the label, and GCC 6.5 has put a register spill (a move,
# which sets flags of its own) on one of those paths: the 3D core's loop in
# o3_log2_88 then never ended. Exit status 1 when any is found.
#   tests/scan_68k_branches.py file.s [...]
import re
import sys

LABEL = re.compile(r'^\.L\d+:$')
BRANCH = re.compile(r'^\t(j|b|fj|fb)(lt|ge|gt|le|eq|ne|mi|pl|cs|cc|hi|ls|vs|vc|oge|olt|ogt|ole|un|or)\b')
found = 0
for name in sys.argv[1:]:
    lines = open(name).read().split('\n')
    for i in range(1, len(lines)):
        if LABEL.match(lines[i - 1]) and BRANCH.match(lines[i]):
            found += 1
            print('%s:%d: %s right after %s (falls in from: %s)' % (name, i + 1, lines[i].strip(), lines[i - 1],
                                                                    lines[i - 2].strip() if i >= 2 else '?'))
sys.exit(1 if found else 0)
