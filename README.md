# OpenRTG

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

6 October 2026: OpenGPU G1 started: the stream (`include/opengpu/`), the
core every back end shares (`library/opengpu/ogpu_core.c`) with its tests
and golden scene, and `opengpu.library` 0.1 with the CPU back end.

## Licence and credit

OpenRTG is free software under the MIT licence (`LICENSE`): anyone may use
it, change it, fork it and ship it, commercially too (Team, 4 October 2026:
"I want anyone to be able to run with it, fork it etc").

OpenRTG was created by Dalsin Limited, for AmigaChrome.
The licence's one condition carries that credit: the copyright notice
("Copyright (c) 2026 Dalsin Limited") and the licence text must stay with
every copy and every fork. We also ask, as a courtesy rather than a
condition, that a fork or a port say in its documentation or About window
that it is based on OpenRTG by Dalsin Limited.

## What is here

- `openrtg.readme`, `tools/libcount.readme`: the Aminet readmes (We fill
  in the uploader at upload time).

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
- `library/modes.c`, `library/modes.h`: phase 2's first part, the display
  database's mode table. It builds each monitor's modes once (Standard: the
  popular PC sizes; All: with the Amiga-shaped ones), each in 8, 16 and
  32-bit, and gives their ModeIDs. The library answers NextDisplayInfo,
  FindDisplayInfo and BestModeIDA from it. Plain C, with no Amiga headers.
- `library/openrtg_lib.c`: `openrtg.library` 0.1. It finds the ACRTG
  boards, one per RTG monitor (the serial number is the monitor), and builds
  each monitor's mode table once. Its calls (`include/proto/openrtg.h`,
  `library/openrtg_lib.sfd`) give the monitor count, each listed mode, a mode
  by ModeID, the best mode for a size, the Standard or All choice, and a
  board's address. It doesn't claim the boards yet; Picasso96's acrtg.card
  drives them until OpenRTG opens screens of its own.
- `library/displaydb.c`: the display database (0.2). graphics.library's
  NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData and ModeNotAvailable
  answer for OpenRTG's ModeIDs from the mode table, chained OS-friendly and
  passing everything else through.
- `tools/openrtg_cmd.c`: `C:OpenRTG`.
  - `OpenRTG` lists the monitors.
  - `OpenRTG MODES [MONITOR n]` lists their modes.
  - `OpenRTG ALL|STANDARD [MONITOR n]` switches the list.
  - `OpenRTG ACTIVATE [FORCE] | OFF` puts OpenRTG's modes in the display
    database, or takes them out. It refuses while Picasso96 runs; FORCE is
    for tests only.
  - `OpenRTG LISTDB` lists every mode the display database has, as ScreenMode
    prefs sees them.
- `library/build.sh`: builds both with the os32 stove.
- `tests/run.sh`: the host tests (`tests/test_modes.c`), plus a check that
  the library's C builds for the 68k with the stove.
- `measurements/`: LibCount's raw counts from the OS 3.2.3 scratch copy with
  Picasso96, 4 October 2026: Workbench for 45 seconds, and MultiView showing
  a true-colour PNG for 30. The design's phase 0 section reads them.
