#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""hunk_rw_check.py FILE...: fails when an AmigaOS LoadSeg file has a data or
BSS hunk with anything in it. opengpu.library may run from ROM, where code
can't write to itself, so it keeps nothing writable outside its base
(DESIGN.md section 5, "Residency")."""
import struct
import sys

CODE, DATA, BSS, END = 0x3E9, 0x3EA, 0x3EB, 0x3F2
RELOCS = {0x3EC, 0x3ED, 0x3EE, 0x3F7, 0x3F8, 0x3F9}     # RELOC32/16/8, DREL32/16/8: lists of offsets
RELOC32SHORT = {0x3FC, 0x3FD}


def writable(path):
    data = open(path, "rb").read()
    p = 0

    def word():
        nonlocal p
        v = struct.unpack_from(">I", data, p)[0]
        p += 4
        return v

    if word() != 0x3F3:
        sys.exit("hunk_rw_check: %s is not a LoadSeg file" % path)
    while True:
        n = word()
        if not n:
            break
        p += n * 4
    word()
    first, last = word(), word()
    p += (last - first + 1) * 4
    found, index = [], first
    while p < len(data):
        t = word() & 0x3FFFFFFF
        if t in (CODE, DATA):
            n = word()
            if t == DATA and n:
                found.append("hunk %d: DATA, %d bytes" % (index, n * 4))
            p += n * 4
        elif t == BSS:
            n = word()
            if n:
                found.append("hunk %d: BSS, %d bytes" % (index, n * 4))
        elif t in RELOCS:
            while True:
                n = word()
                if not n:
                    break
                p += 4 + n * 4
        elif t in RELOC32SHORT:
            start = p
            while True:
                n = struct.unpack_from(">H", data, p)[0]
                p += 2
                if not n:
                    break
                p += 2 + n * 2
            if (p - start) % 4:
                p += 2
        elif t == 0x3F0:                        # SYMBOL
            while True:
                n = word()
                if not n:
                    break
                p += n * 4 + 4
        elif t in (0x3F1, 0x3E8):               # DEBUG, NAME
            p += word() * 4
        elif t == END:
            index += 1
        else:
            sys.exit("hunk_rw_check: %s: hunk type %#x not understood" % (path, t))
    return found


fail = 0
for f in sys.argv[1:]:
    rw = writable(f)
    if rw:
        print("hunk_rw_check: FAIL %s has writable data: %s" % (f, "; ".join(rw)))
        fail = 1
    else:
        print("hunk_rw_check: %s has no writable data" % f)
sys.exit(fail)
