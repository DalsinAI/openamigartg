#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""LibCount's report with names: each library's function offsets named from
the NDK's FD files, sorted by calls, as a Markdown table.

  libcount_report.py COUNTS [--fd DIR] [--top N]

COUNTS is LibCount's output (library, offset, count a line). FD files are
read from --fd (default: the os32 stove's NDK 3.2 FD folder); a library
with no FD file there keeps its offsets."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

DEFAULT_FD = Path.home() / "AmigaChrome" / "stoves" / "os32" / "ndk" / "FD"


def fd_names(path: Path) -> dict[int, str]:
    """{offset (negative): name} from an FD file: ##bias N, then one function
    a line from -N on, 6 bytes apart, private ones included."""
    names, bias = {}, 30
    for line in path.read_text(encoding="latin-1").splitlines():
        line = line.strip()
        if line.startswith("##bias"):
            bias = int(line.split()[1])
        elif line.startswith("##") or line.startswith("*") or not line:
            continue
        else:
            m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
            if m:
                names[-bias] = m.group(1)
                bias += 6
    return names


def fd_for(library: str, folder: Path) -> Path | None:
    stem = library.split(".")[0].lower()
    for p in folder.glob("*.fd"):
        if p.stem.lower() in (f"{stem}_lib", stem):
            return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("counts")
    ap.add_argument("--fd", default=str(DEFAULT_FD))
    ap.add_argument("--top", type=int, default=0)
    a = ap.parse_args()
    rows, named = [], {}
    for line in Path(a.counts).read_text(encoding="latin-1").splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        lib, off, n = parts[0], int(parts[1]), int(parts[2])
        if lib not in named:
            fd = fd_for(lib, Path(a.fd))
            named[lib] = fd_names(fd) if fd else {}
        rows.append((n, lib, off, named[lib].get(off, "")))
    rows.sort(key=lambda r: -r[0])
    if a.top:
        rows = rows[:a.top]
    total = sum(r[0] for r in rows)
    print("| Calls | Library | Offset | Function |")
    print("| ---: | --- | ---: | --- |")
    for n, lib, off, name in rows:
        print(f"| {n:,} | {lib} | {off} | {name or '(no FD)'} |")
    print(f"\n{len(rows)} functions, {total:,} calls.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
