#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""gen_sat_procs.py HEADER PROCS [--write]

The ABI of a satellite module (SDL2_image.module, SDL2_mixer.module): one
SAT_PROC(rc, name, (params), (args), ret) line for each function its public
header declares, in the order of the committed PROCS file. The module fills
its stub's jump table in that order, so the order never changes: functions
a newer header adds go at the end.

Without --write it checks that PROCS has every function HEADER declares,
each with the same declaration, and exits 1 if not (the build runs it).
With --write it appends the new ones to PROCS (or makes PROCS).
"""
import re
import sys
from pathlib import Path

DECL = re.compile(r"extern\s+DECLSPEC\s+(.*?)\s*SDLCALL\s+(\w+)\s*\((.*?)\)\s*;", re.S)
LINE = re.compile(r"^SAT_PROC\((.*?), (\w+), \((.*)\), \((.*)\), (return|)\)$")


def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        cur += ch
    out.append(cur)
    return [p.strip() for p in out]


def arg_name(p):
    m = re.search(r"\(\s*(?:SDLCALL\s*)?\*\s*(\w+)\s*\)", p)
    if m:
        return m.group(1)
    m = re.search(r"(\w+)\s*(\[\s*\w*\s*\])?$", p)
    if not m:
        sys.exit("gen_sat_procs: no name in parameter %r" % p)
    return m.group(1)


def procs_of(header):
    text = Path(header).read_text()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    out = []
    for rc, name, params in DECL.findall(text):
        rc = " ".join(rc.replace("SDL_DEPRECATED", "").split())
        params = " ".join(params.split())
        ps = [] if params in ("", "void") else split_top(params)
        if any(p == "..." for p in ps):
            sys.exit("gen_sat_procs: %s takes varargs; give it a stub of its own" % name)
        args = ", ".join(arg_name(p) for p in ps)
        ret = "" if rc == "void" else "return"
        out.append("SAT_PROC(%s, %s, (%s), (%s), %s)" % (rc, name, params or "void", args, ret))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    header, procs = sys.argv[1], Path(sys.argv[2])
    write = "--write" in sys.argv[3:]
    want = procs_of(header)
    have = [l for l in procs.read_text().splitlines() if l.startswith("SAT_PROC(")] if procs.exists() else []
    have_names = {LINE.match(l).group(2): l for l in have}
    bad = [l for l in want if LINE.match(l).group(2) in have_names and have_names[LINE.match(l).group(2)] != l]
    new = [l for l in want if LINE.match(l).group(2) not in have_names]
    if bad:
        sys.exit("gen_sat_procs: declarations changed (an ABI break):\n  " + "\n  ".join(bad))
    if not new:
        print("gen_sat_procs: %s: %d functions, all in %s" % (header, len(want), procs.name))
        return
    if not write:
        sys.exit("gen_sat_procs: %s has %d functions %s lacks (run with --write):\n  %s"
                 % (header, len(new), procs.name, "\n  ".join(new)))
    if not have:
        head = ("/* %s: the ABI of the satellite module, made by gen_sat_procs.py from\n"
                " * %s. The order is the module's jump table: never reorder or remove a\n"
                " * line; a newer header's functions go at the end. Zlib-licensed API\n"
                " * (the declarations are the library's own); this list is the Team's. */\n"
                % (procs.name, Path(header).name))
        procs.write_text(head)
    with procs.open("a") as f:
        for l in new:
            f.write(l + "\n")
    print("gen_sat_procs: %d added to %s" % (len(new), procs))


if __name__ == "__main__":
    main()
