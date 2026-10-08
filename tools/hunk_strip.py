#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""hunk_strip.py IN OUT: an AmigaOS LoadSeg file without its symbols and
debug hunks (HUNK_SYMBOL, HUNK_DEBUG), everything else as it is. The
stove's strip refuses some -fbaserel32 programs whose data hunk is large
("is too large", a BFD sanity check); this doesn't need BFD."""
import struct
import sys


def strip(data):
    out, p = bytearray(), 0

    def word():
        nonlocal p
        v = struct.unpack_from(">I", data, p)[0]
        p += 4
        return v

    def copy(n):
        nonlocal p
        out.extend(data[p:p + n])
        p += n

    if word() != 0x3F3:
        sys.exit("hunk_strip: not a LoadSeg file")
    out += struct.pack(">I", 0x3F3)
    while True:                                 # resident library names (none in practice)
        n = word()
        out += struct.pack(">I", n)
        if not n:
            break
        copy(n * 4)
    table = word()
    first, last = word(), word()
    out += struct.pack(">III", table, first, last)
    copy((last - first + 1) * 4)
    while p < len(data):
        t = word()
        kind = t & 0x3FFFFFFF
        if kind in (0x3E9, 0x3EA):              # CODE, DATA
            n = word()
            out += struct.pack(">II", t, n)
            copy(n * 4)
        elif kind == 0x3EB:                     # BSS
            out += struct.pack(">II", t, word())
        elif kind in (0x3EC, 0x3F7):            # RELOC32, DREL32 (short form isn't used here)
            out += struct.pack(">I", t)
            while True:
                n = word()
                out += struct.pack(">I", n)
                if not n:
                    break
                copy(4 + n * 4)
        elif kind == 0x3F0:                     # SYMBOL: dropped
            while True:
                n = word()
                if not n:
                    break
                p += (n & 0xFFFFFF) * 4 + 4
        elif kind == 0x3F1:                     # DEBUG: dropped
            p += word() * 4
        elif kind == 0x3F2:                     # END
            out += struct.pack(">I", t)
        else:
            sys.exit("hunk_strip: hunk type %x not known" % kind)
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    open(sys.argv[2], "wb").write(strip(open(sys.argv[1], "rb").read()))
