# Open RTG

Our own RTG system for AmigaOS 3.x: `openrtg.library`, OpenGPU, Warp3D,
several monitors, the window look and one prefs app. It is for real Amigas
and PiStorms as well as AmigaChrome, where its board (ACRTG) and OpenGPU's
host driver make the desktop fast without emulating drawing loops.

The design is `DESIGN.md`: the parts, several monitors, AGA as a pseudo card,
the drivers (named the OS 4 way), `openrtg.library`, OpenGPU, Warp3D, the OS 4
and MorphOS behaviours, the window look, the prefs app, the phases and what
phase 0 measured.

Status, 4 October 2026: designed; phase 0 (measuring what programs call)
done; phase 1 (several RTG monitors) built in AmigaChrome; `openrtg.library`
is next.

## Licence and credit

Open RTG is free software under the MIT licence (`LICENSE`): anyone may use
it, change it, fork it and ship it, commercially too (Dale, 4 October 2026:
"I want anyone to be able to run with it, fork it etc").

Open RTG was created by Dale Kirkwood at Dalsin Limited, for AmigaChrome.
The licence's one condition carries that credit: the copyright notice
("Copyright (c) 2026 Dalsin Limited") and the licence text must stay with
every copy and every fork. We also ask, as a courtesy rather than a
condition, that a fork or a port say in its documentation or About window
that it is based on Open RTG by Dalsin Limited.

## What is here

- `tools/libcount.c`: LibCount, phase 0's measuring tool. It counts the calls
  to every function of the libraries it is given, with OS-friendly
  SetFunction() stubs that pass every call on. It runs on any Amiga with
  OS 2.04 or later.

      LibCount graphics.library intuition.library layers.library SECONDS 30 TO RAM:counts

- `tools/libcount_report.py`: names the counted offsets from the NDK's FD
  files and prints a table sorted by calls.
- `tools/build.sh`: builds LibCount with bebbo's m68k-amigaos-gcc (amiga-gcc)
  and NDK 3.2: set `STOVE` to the toolchain's install folder (the one holding
  `prefix/bin`), or `CC` to the compiler itself. Output goes to `build/`.
  `libcount_report.py` reads the NDK's FD files; give their folder with `--fd`.
- `measurements/`: LibCount's raw counts from the OS 3.2.3 scratch copy with
  Picasso96, 4 October 2026: Workbench for 45 seconds, and MultiView showing
  a true-colour PNG for 30. The design's phase 0 section reads them.
