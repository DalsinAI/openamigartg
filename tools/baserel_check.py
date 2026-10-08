#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""baserel_check.py PREFIX MODULE.debug MODULE.map [--allow FILE]: a shared module (built
-fbaserel32 -resident32, include/opengpu/module.h) must reach its data only
through A4. opengpu.library loads its code once and gives each program a copy
of the data; the data LoadSeg loaded is never anyone's. Two things would
reach it instead, and this lists both and exits 1 when there is either:

  - an absolute pointer from the code into the data: a RELOC32 in a code
    hunk to the data hunk (assembly that names a variable, a const table
    holding a variable's address, a __saveds function that loads A4 from
    ___a4_init). Only the module's header and module_self (module_start.S)
    may have one. Each is looked at in the code and said to be a read, a
    write, an address taken (LEA, PEA) or a pointer kept in a table in the
    code. They reach the data as loaded, which is no program's: right only
    for something no program ever changes (GCC reaches a function's static
    const tables and a local array's first values this way). Each fails
    unless FILE allows it: a line of FILE is a regular expression for the
    report's line, written after a review; a write needs a line that starts
    "write:" (for a constant table every program's start fills in with the
    same values);
  - an A4-relative reference (DREL32/DREL16) to a symbol the linker put in
    the code: an "extern" array that is really in the code (libnix's set
    lists), which the compiler reaches through A4 because it isn't const.

It also says how big the data is, which is what each program's open
copies. PREFIX is the stove's tool prefix (.../bin/m68k-amigaos-); the map
comes from linking with -Wl,-Map; the .debug file is the unstripped link.
"""
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile

CODE, DATA, BSS = 0x3E9, 0x3EA, 0x3EB


def hunks(path):
    d = open(path, "rb").read()
    p = 0

    def word():
        nonlocal p
        v = struct.unpack_from(">I", d, p)[0]
        p += 4
        return v

    if word() != 0x3F3:
        sys.exit("baserel_check: %s isn't a LoadSeg file" % path)
    while word():
        pass
    word()
    first, last = word(), word()
    sizes = [word() & 0x3FFFFFFF for _ in range(last - first + 1)]
    out, cur = [], None
    while p < len(d):
        t = word() & 0x3FFFFFFF
        if t in (CODE, DATA):
            n = word()
            cur = {"type": t, "size": n * 4, "relocs": {}, "bytes": d[p:p + n * 4] if t == CODE else b""}
            p += n * 4
            out.append(cur)
        elif t == BSS:
            cur = {"type": BSS, "size": word() * 4, "relocs": {}}
            out.append(cur)
        elif t == 0x3EC:                        # HUNK_RELOC32
            while True:
                n = word()
                if not n:
                    break
                h = word()
                cur["relocs"].setdefault(h, []).extend(struct.unpack_from(">%dI" % n, d, p))
                p += n * 4
        elif t == 0x3F0:                        # HUNK_SYMBOL
            while True:
                n = word()
                if not n:
                    break
                p += n * 4 + 4
        elif t in (0x3F1,):                     # HUNK_DEBUG
            p += word() * 4
        elif t == 0x3F2:                        # HUNK_END
            pass
        else:
            sys.exit("baserel_check: %s: hunk type %x not known" % (path, t))
    for h, s in zip(out, sizes):
        h["alloc"] = s * 4
    return out


def symbols(prefix, path):
    """{name: (address, kind)}, and sorted [(address, name)] of the code's and
    of the data's (addresses are offsets in their hunk)."""
    out = subprocess.run([prefix + "nm", path], capture_output=True, text=True, errors="replace").stdout
    syms, code, data = {}, [], []
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]+) (\w) (\S+)$", line)
        if m:
            a, k, n = int(m.group(1), 16), m.group(2), m.group(3)
            syms[n] = (a, k)
            if k in "Tt":
                code.append((a, n))
            elif k in "DdBb" and not n.startswith("___a4_init"):
                data.append((a, n))
    code.sort()
    data.sort()
    return syms, code, data


def data_inputs(mapfile):
    """[(start, end, file)] of the input sections in the data and BSS."""
    out, pending, in_data, base = [], None, False, None
    for line in open(mapfile, errors="replace"):
        m = re.match(r"^\.(data|bss)\s+0x([0-9a-f]+)", line)
        if m:
            in_data = True
            if base is None:
                base = int(m.group(2), 16)       # the data hunk's start (BSS follows in it)
        elif re.match(r"^\.\w", line):
            in_data = False
        if not in_data:
            continue
        m = re.match(r"^ (\.\S+)\s*$", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^ (\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            a, n = int(m.group(2), 16) - (base or 0), int(m.group(3), 16)
            if n:
                out.append((a, a + n, (m.group(1) or pending) + "+0x%x of " + m.group(4).strip().rsplit("/", 1)[-1]))
        pending = None
    out.sort()
    return out


def instruction(prefix, debug, at, fstart):
    """objdump's (start, text) of the instruction holding byte `at`, reading
    from fstart (the function or object it is in)."""
    r = subprocess.run([prefix + "objdump", "-d", "--start-address=0x%x" % fstart, "--stop-address=0x%x" % (at + 8), debug],
                       capture_output=True, text=True, errors="replace").stdout
    best = None
    for line in r.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} ?)+)\s+(.*)$", line)
        if m:
            a, n = int(m.group(1), 16), len(m.group(2).split()) * 2
            if a <= at < a + n:
                best = (a, m.group(3).strip())
    return best


def kind_of(ins, at):
    """What the code does with the address: read, write, address (LEA, PEA,
    an immediate) or table (data kept in the code: a pointer in a table, not
    an instruction)."""
    if not ins or ins[0] == at:
        return "table"
    text = ins[1]
    if not text.split():
        return "table"
    op = text.split()[0]
    args = text[len(op):].strip()
    parts, depth, cur = [], 0, ""
    for ch in args:
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
            continue
        depth += ch in "(<"
        depth -= ch in ")>"
        cur += ch
    parts.append(cur.strip())
    # the operand objdump writes with the symbol, "addr <sym>" or "#addr <sym>"
    symbolic = [i for i, x in enumerate(parts) if "<" in x]
    if op.startswith(("lea", "pea")) or (symbolic and parts[symbolic[0]].startswith("#")):
        return "address"
    if not symbolic:
        return "read"
    last = symbolic[-1]
    if op.startswith(("cmp", "tst", "btst", "chk", "jsr", "jmp")):
        return "read"
    if len(parts) == 1:
        return "write" if op.startswith(("clr", "neg", "not", "tas", "s", "nbcd")) else "read"
    return "write" if last == len(parts) - 1 else "read"


def code_inputs(mapfile):
    """[(start, end, section, file)] of the input sections in the code, from the map."""
    out, pending, in_text = [], None, False
    for line in open(mapfile, errors="replace"):
        if re.match(r"^\.text\s", line) or line.startswith(".text\n"):
            in_text = True
        elif re.match(r"^\.\w", line):
            in_text = False
        if not in_text:
            continue
        m = re.match(r"^ (\.\S+)\s*$", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^ (\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            sec = m.group(1) or pending
            a, n = int(m.group(2), 16), int(m.group(3), 16)
            if n:
                out.append((a, a + n, sec, m.group(4).strip()))
        pending = None
    out.sort()
    return out


def inputs(mapfile):
    """The objects the link used: [(file, member or None)]."""
    used, seen = [], set()
    for line in open(mapfile, errors="replace"):
        m = re.match(r"^(\S+\.a)\(([^)]+)\)", line)
        if m and (m.group(1), m.group(2)) not in seen:
            seen.add((m.group(1), m.group(2)))
            used.append((m.group(1), m.group(2)))
        m = re.match(r"^LOAD (\S+\.o)$", line.strip())
        if m and (m.group(1), None) not in seen:
            seen.add((m.group(1), None))
            used.append((m.group(1), None))
    return used


def drel_lines(prefix, f, members):
    """objdump -r's lines for the members of f used (all of f when it is an
    object), as (member, line). objdump crashes on some of the stove's
    archives as a whole (libnix20.a), so those are taken apart first and
    read a member at a time. None for a file it can't read at all."""
    r = subprocess.run([prefix + "objdump", "-r", f], capture_output=True, text=True, errors="replace")
    if r.returncode == 0:
        cur, out = None, []
        for line in r.stdout.splitlines():
            m = re.match(r"^(\S+):\s+file format ", line)
            if m:
                cur = m.group(1)
            elif None in members or cur in members:
                out.append((cur, line))
        return out
    if None in members:
        return None
    out = []
    with tempfile.TemporaryDirectory() as t:
        for mem in sorted(members):
            if subprocess.run([prefix + "ar", "x", os.path.abspath(f), mem], cwd=t, capture_output=True).returncode:
                return None
            r = subprocess.run([prefix + "objdump", "-r", os.path.join(t, mem)], capture_output=True, text=True, errors="replace")
            if r.returncode:
                return None
            out += [(mem, line) for line in r.stdout.splitlines()]
    return out


def drel_targets(prefix, used):
    """{symbol: [where]} for every A4-relative relocation in the objects used."""
    by_file = {}
    for f, member in used:
        by_file.setdefault(f, set()).add(member)
    found, unread = {}, []
    for f, members in by_file.items():
        lines = drel_lines(prefix, f, members)
        if lines is None:
            unread.append(f)
            continue
        for cur, line in lines:
            m = re.match(r"^[0-9a-f]+ (DREL\w+)\s+(\S+)", line)
            if m:
                sym = re.sub(r"[+-]0x[0-9a-f]+$", "", m.group(2))
                found.setdefault(sym, []).append("%s(%s)" % (f.rsplit("/", 1)[-1], cur))
    return found, unread


def main():
    args = sys.argv[1:]
    allow_file = None
    if len(args) == 5 and args[3] == "--allow":
        allow_file = args.pop(4)
        args.pop(3)
    if len(args) != 3:
        sys.exit(__doc__)
    prefix, debug, mapfile = args
    allow, allow_writes = [], []
    if allow_file:
        for line in open(allow_file):
            line = line.split("#", 1)[0].strip()
            if line.startswith("write:"):
                allow_writes.append(re.compile(line[6:].strip()))
            elif line:
                allow.append(re.compile(line))
    hs = hunks(debug)
    syms, code, dsyms = symbols(prefix, debug)
    data = [i for i, h in enumerate(hs) if h["type"] in (DATA, BSS)]
    bad, allowed = [], []
    start = syms.get("_ogpu_module_start", (0, ""))[0]
    callout = syms.get("_ogpu_module_callout_a4", (start + 64, ""))[0]
    addrs = [a for a, _ in code]
    daddrs = [a for a, _ in dsyms]
    secs = code_inputs(mapfile)
    starts = [x[0] for x in secs]
    dsecs = data_inputs(mapfile)
    dstarts = [x[0] for x in dsecs]
    for i, h in enumerate(hs):
        if h["type"] != CODE:
            continue
        for target, offs in h["relocs"].items():
            if target not in data:
                continue
            for o in offs:
                if i == 0 and start <= o < callout:
                    continue                    # the header and module_self
                k = bisect.bisect_right(addrs, o) - 1
                fstart = code[k][0] if k >= 0 and i == 0 else 0
                where = "%s+0x%x" % (code[k][1], o - code[k][0]) if k >= 0 and i == 0 else "hunk %d +0x%x" % (i, o)
                k = bisect.bisect_right(starts, o) - 1
                if i == 0 and k >= 0 and o < secs[k][1]:
                    where += " (%s of %s)" % (secs[k][2], secs[k][3].rsplit("/", 1)[-1])
                    fstart = max(fstart, secs[k][0])    # a static function has no symbol here
                value = struct.unpack_from(">I", h["bytes"], o)[0]
                k = bisect.bisect_right(dstarts, value) - 1
                what = dsecs[k][2] % (value - dsecs[k][0]) if k >= 0 and value < dsecs[k][1] else "data+0x%x" % value
                k = bisect.bisect_right(daddrs, value) - 1
                if k >= 0 and dsyms[k][0] == value:
                    what = "%s (%s)" % (dsyms[k][1], what)
                kind = kind_of(instruction(prefix, debug, o, fstart) if i == 0 else None, o)
                line = "%s of %s, from the code at %s" % (kind, what, where)
                if any(r.search(line) for r in (allow_writes if kind == "write" else allow)):
                    allowed.append(line)
                else:
                    bad.append("absolute " + line)
    found, unread = drel_targets(prefix, inputs(mapfile))
    for sym, where in sorted(found.items()):
        if sym in syms and syms[sym][1] in "TtRr":
            bad.append("A4-relative reference to %s, which is in the code, from %s" % (sym, ", ".join(sorted(set(where))[:4])))
    size = sum(h["alloc"] for i, h in enumerate(hs) if i in data)
    ncode = sum(h["alloc"] for h in hs if h["type"] == CODE)
    print("baserel_check: %s: code %d bytes once; data and BSS %d bytes for each program (%d hunks)" %
          (debug.rsplit("/", 1)[-1], ncode, size, len(hs)))
    for f in unread:
        bad.append("couldn't read %s's relocations" % f)
    for a in allowed:
        print("baserel_check: allowed (%s): %s" % (allow_file.rsplit("/", 1)[-1], a))
    for b in bad:
        print("baserel_check: " + b)
    if bad:
        print("baserel_check: %d problems" % len(bad))
        sys.exit(1)
    print("baserel_check: the data is written only through A4")

if __name__ == "__main__":
    main()
