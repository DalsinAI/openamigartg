#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# openrtg.library, cybergraphics.library and opengpu.library (bare: no startup code or C
# library; the RomTag's stub comes first), C:OpenRTG and its monitor driver, with the os32 stove (bebbo's m68k-amigaos-gcc).
#   library/build.sh [OUT_DIR]     (default build/)
# or, with `library/build.sh module [OUT_DIR]` (OUT_DIR: $OUT, else build/), only
# the ROM modules, in OUT_DIR/modules, built for size (OPT=, -Os by default):
#   openrtg.module          openrtg.library
#   cybergraphics.module    cybergraphics.library
#   openrtg.monitor.module  the monitor driver's job, before DOS (openrtg_monitor_rom.c)
# Every build of these three fails if one keeps data or BSS: they run from a ROM.
set -eu
TARGET=all
[ "${1:-}" != module ] || { TARGET=module; shift; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
NM=${NM:-${CC%gcc}nm}
if [ "$TARGET" = module ]; then OUT=${1:-${OUT:-$ROOT/build}}; else OUT=${1:-$ROOT/build}; fi
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
mkdir -p "$OUT"
# openrtg.library and cybergraphics.library keep what they write in memory they
# allocate, its address in A4 (library/ortg_state.h): -ffixed-a4 on all their code.
# $1 the file, $2 the optimisation, the rest its sources
build_ortg() {
    out=$1 opt=$2; shift 2
    "$CC" -m68020 -mnobitfield $opt -ffixed-a4 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -nostartfiles -nostdlib -I"$ROOT/include" -o "$out" "$@" -lgcc
    echo "$out ($(wc -c < "$out") bytes)"
}
ORTG_SRC="$HERE/openrtg_lib.c $HERE/modes.c $HERE/displaydb.c $HERE/screens.c $HERE/pixels.c $HERE/opengpu/ogpu_build.c $HERE/string.c $HERE/ortg_stub.c"
CGX_SRC="$ROOT/compat/cybergraphics_lib.c $HERE/ortg_stub.c"
# nothing writable in the file, and nothing undefined
rom_clean() {
    python3 "$ROOT/tools/hunk_rw_check.py" "$1"
    data=$("$NM" "$1" | awk 'NF == 3 && $2 ~ /^[dDbBcC]$/ {print $3}' || true)
    [ -z "$data" ] || { echo "$1 keeps data: $data" >&2; exit 1; }
    und=$("$NM" -u "$1" || true)
    [ -z "$und" ] || { echo "undefined in $1: $und" >&2; exit 1; }
}
if [ "$TARGET" = module ]; then
    OPT=${OPT:--Os}
    mkdir -p "$OUT/modules"
    # shellcheck disable=SC2086
    build_ortg "$OUT/modules/openrtg.module" "$OPT" $ORTG_SRC
    # shellcheck disable=SC2086
    build_ortg "$OUT/modules/cybergraphics.module" "$OPT" $CGX_SRC
    "$CC" -m68020 $OPT -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror \
        -nostartfiles -nostdlib -I"$ROOT/include" -o "$OUT/modules/openrtg.monitor.module" "$HERE/openrtg_monitor_rom.c" -lgcc
    echo "$OUT/modules/openrtg.monitor.module ($(wc -c < "$OUT/modules/openrtg.monitor.module") bytes)"
    for m in openrtg cybergraphics openrtg.monitor; do rom_clean "$OUT/modules/$m.module"; done
    python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/modules/openrtg.module" "$OUT/modules/cybergraphics.module" "$OUT/modules/openrtg.monitor.module"
    sha256sum "$OUT"/modules/openrtg.module "$OUT"/modules/cybergraphics.module "$OUT"/modules/openrtg.monitor.module
    exit 0
fi
# shellcheck disable=SC2086
build_ortg "$OUT/openrtg.library" -O2 $ORTG_SRC
# shellcheck disable=SC2086
build_ortg "$OUT/cybergraphics.library" -O2 $CGX_SRC
rom_clean "$OUT/openrtg.library"
rom_clean "$OUT/cybergraphics.library"
# The 3D rasteriser (Warp3D's and SDL's TRIANGLES), when its files are there.
D3= D3SRC=
if [ -f "$HERE/opengpu/ogpu_3d.c" ]; then D3=-DOGPU_WITH_3D; D3SRC="$HERE/opengpu/ogpu_3d.c"; fi
# OpenGfx (library/ogfx): the patches' glue and its leaves. The leaves are AC090
# magic functions: stack arguments, and no loop turned into a call (ogfx_leaves.c).
# OpenGfx's drawing core comes from openamigagfx at a pinned commit (one copy).
sh "$ROOT/tools/fetch_ogfx_core.sh" "$OUT"
for f in gfx_core gfx_rp; do
    "$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_$f.o" "$OUT/ogfx-core/$f.c"
done
"$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -I"$ROOT/include" -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_lib.o" "$HERE/ogfx/ogfx_lib.c"
"$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
    -c -o "$OUT/ogfx_leaves.o" "$HERE/ogfx/ogfx_leaves.c"
# -mnobitfield: GCC's bitfield instructions are slow on the AC090's JIT (opengpu_core.c's header).
"$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib -I"$ROOT/include" \
    $D3 -o "$OUT/opengpu.library" "$HERE/opengpu/opengpu_lib.c" "$HERE/opengpu/ogpu_core.c" $D3SRC "$HERE/opengpu/ogpu_module.c" "$HERE/string.c" \
    "$OUT/ogfx_lib.o" "$OUT/ogfx_leaves.o" "$OUT/ogfx_gfx_core.o" "$OUT/ogfx_gfx_rp.o" -lgcc
echo "$OUT/opengpu.library ($(wc -c < "$OUT/opengpu.library") bytes)"
# No writable data: it may run from ROM, so everything it keeps is in its base.
python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/opengpu.library"
"$CC" -m68020 -O2 -fno-delete-null-pointer-checks -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/OpenRTG" "$ROOT/tools/openrtg_cmd.c"
echo "$OUT/OpenRTG ($(wc -c < "$OUT/OpenRTG") bytes)"
"$CC" -m68020 -O2 -fno-delete-null-pointer-checks -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/OpenRTG-Monitor" "$ROOT/tools/openrtg_monitor.c"
echo "$OUT/OpenRTG-Monitor ($(wc -c < "$OUT/OpenRTG-Monitor") bytes)"
"$HERE/warp3d/build.sh" "$OUT"        # Warp3D.library, its tools and checks
"$HERE/minigl/build.sh" "$OUT"        # minigl.library, libminigl.a, libmgl.a and MGLTest
"$HERE/tinygl/build.sh" "$OUT"        # tinygl.library (the stand-in) and libtinygl.a
[ ! -x "${SDL2_STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] || STOVE="${SDL2_STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}" "$HERE/modules/sdl2/build.sh" "$OUT"   # SDL 2 (library/modules/sdl2), with the GCC 16 stove
# SDL 1.2 (library/modules/sdl12: sdl12-compat on that SDL 2), when SDL 2 was built.
[ ! -f "$OUT/libSDL2.a" ] || STOVE16="${SDL2_STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}" "$HERE/modules/sdl12/build.sh" "$OUT"
# No float-to-int store through the FPCR register (tools/fpcr_check.py: GCC 16
# at -m68040 can make one; the check runs on every build, whichever stove).
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/openrtg.library" "$OUT/cybergraphics.library" "$OUT/opengpu.library" "$OUT/OpenRTG" "$OUT/OpenRTG-Monitor" "$OUT/minigl.library" "$OUT/tinygl.library" "$OUT/libtinygl.a"
