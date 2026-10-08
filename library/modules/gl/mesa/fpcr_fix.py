#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""fpcr_fix.py BUILD_DIR OBJDUMP OUT_DIR LIB...: Mesa's objects without GCC 16's
FPCR clash (tools/fpcr_check.py).

Every object of the static libraries LIB (relative to BUILD_DIR, as meson
builds them: lib.a's objects are in lib.a.p/) is scanned. Each that has a
float-to-int store through the FPCR register is compiled again, by its own
command from compile_commands.json, with -m68020 in place of -m68040 (with
-m68881, GCC converts with fintrz and doesn't save FPCR), into OUT_DIR, and
scanned again. The new objects' paths are printed, one a line: linked before
the libraries, they stand in for the members (every symbol of the object is
in its replacement, so the linker never takes the old member). Exits 1 if a
clash is left."""
import json
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "tools"))
from fpcr_check import scan  # noqa: E402


def main():
    build, objdump, out, libs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    os.makedirs(out, exist_ok=True)
    objs = []
    for lib in libs:
        d = os.path.join(build, lib + ".p")
        if os.path.isdir(d):
            objs += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".o")]
    found = []
    for i in range(0, len(objs), 200):
        found += scan(objdump, *objs[i:i + 200])
    bad = sorted({os.path.relpath(p, build) for p, _, _ in found})
    db = {e["output"]: e for e in json.load(open(os.path.join(build, "compile_commands.json")))}
    left = 0
    for rel in bad:
        fns = sorted({fn for p, fn, _ in found if os.path.relpath(p, build) == rel})
        e = db.get(rel)
        if not e:
            print("fpcr_fix: no compile command for %s" % rel, file=sys.stderr)
            left += 1
            continue
        args = shlex.split(e["command"])
        new = os.path.join(os.path.abspath(out), os.path.basename(rel))
        args = ["-m68020" if a == "-m68040" else a for a in args]
        args[args.index("-o") + 1] = new
        # meson's dependency file goes beside the new object
        if "-MF" in args:
            args[args.index("-MF") + 1] = new + ".d"
        r = subprocess.run(args, cwd=e["directory"], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            sys.exit("fpcr_fix: %s didn't build again" % rel)
        if scan(objdump, new):
            print("fpcr_fix: %s still has a clash at -m68020" % rel, file=sys.stderr)
            left += 1
            continue
        print("fpcr_fix: %s (%s) built again at -m68020" % (rel, ", ".join(fns)), file=sys.stderr)
        print(new)
    print("fpcr_fix: %d of %d objects had the clash" % (len(bad), len(objs)), file=sys.stderr)
    sys.exit(1 if left else 0)


if __name__ == "__main__":
    main()
