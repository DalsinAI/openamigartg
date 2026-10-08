#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library (bare: no startup code or C library; the RomTag's stub
# comes first), with the OpenGPU core and 3D core linked in for drawing on
# the 68k, its link libraries and its test program, with the os32 stove
# (bebbo's m68k-amigaos-gcc).
#   library/minigl/build.sh [OUT_DIR]     (default build/)
#   library/minigl/build.sh api           remake the interface files from minigl_api.txt
# The library needs an FPU (-m68040 -mhard-float) and keeps a4 for the
# programs whose functions it calls (-ffixed-a4: small-data programs).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
if [ "${1:-}" = api ]; then
    python3 "$HERE/gen_api.py"
    exit 0
fi
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
AR=${AR:-$STOVE/prefix/bin/m68k-amigaos-ar}
OBJDUMP=${OBJDUMP:-$STOVE/prefix/bin/m68k-amigaos-objdump}
OUT=${1:-$ROOT/build}
OBJ="$OUT/mgl-obj"
mkdir -p "$OUT" "$OBJ"
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
WARN="-Wall -Wextra -Werror -Wno-unused-parameter"
FLAGS="-m68040 -mhard-float -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -ffixed-a4 -fno-builtin $WARN -I$ROOT/include ${MGL_CFLAGS:-}"
OGPU="$ROOT/library/opengpu"
# The cores as opengpu.library builds them (-m68020; see library/warp3d/build.sh).
for f in ogpu_core ogpu_3d ogpu_build ogpu_build3d; do
    "$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -ffixed-a4 -fno-builtin $WARN -I"$ROOT/include" \
        -c -o "$OBJ/$f.o" "$OGPU/$f.c"
done
"$CC" $FLAGS -fno-toplevel-reorder -nostartfiles -nostdlib -o "$OUT/minigl.library" \
    "$HERE/mgl_lib.c" "$HERE/mgl_math.c" "$HERE/mgl_state.c" "$HERE/mgl_matrix.c" "$HERE/mgl_vertex.c" \
    "$HERE/mgl_pipe.c" "$HERE/mgl_texture.c" "$HERE/mgl_out.c" "$HERE/mgl_list.c" "$HERE/mgl_glu.c" \
    "$HERE/mgl_glut.c" "$HERE/mgl_display.c" "$HERE/mgl_table.c" \
    "$OBJ/ogpu_core.o" "$OBJ/ogpu_3d.o" "$OBJ/ogpu_build.o" "$OBJ/ogpu_build3d.o" -lgcc
echo "$OUT/minigl.library ($(wc -c < "$OUT/minigl.library") bytes)"
# Instructions the 68040's FPU leaves to software must not be in it.
if "$OBJDUMP" -d "$OUT/minigl.library" 2>/dev/null | grep -Eq '\sf(sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|etox|etoxm1|twotox|tentox|logn|lognp1|log10|log2|intrz|int|getexp|getman|mod|rem|scale|movecr)[.a-z]*\s'; then
    echo "minigl.library: has FPU instructions the 68040 lacks" >&2
    "$OBJDUMP" -d "$OUT/minigl.library" | grep -E '\sf(sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|etox|etoxm1|twotox|tentox|logn|lognp1|log10|log2|intrz|int|getexp|getman|mod|rem|scale|movecr)[.a-z]*\s' | head >&2
    exit 1
fi
# No conditional branch first after a label: GCC 6.5's flags bug (tests/scan_68k_branches.py).
for f in "$HERE"/mgl_*.c; do
    "$CC" $FLAGS -S -o "$OBJ/$(basename "$f" .c).s" "$f"
done
python3 "$ROOT/tests/scan_68k_branches.py" "$OBJ"/mgl_*.s
# libminigl.a: MiniGLOpen and the table, for programs that include <proto/minigl.h>.
"$CC" -m68000 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/minigl_base.o" "$HERE/client/minigl_base.c"
"$CC" -m68000 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/minigl_open.o" "$HERE/client/minigl_open.c"
"$CC" -m68000 -c -o "$OBJ/minigl_getdispatch.o" "$HERE/client/minigl_getdispatch.s"

"$AR" rcs "$OUT/libminigl.a" "$OBJ/minigl_base.o" "$OBJ/minigl_open.o" "$OBJ/minigl_getdispatch.o"
echo "$OUT/libminigl.a"
# libmgl.a: every call as a function, for programs written for the linked-in MiniGL.
"$CC" -m68000 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/mgl_static.o" "$HERE/mgl_static.c"
"$AR" rcs "$OUT/libmgl.a" "$OBJ/mgl_static.o" "$OBJ/minigl_base.o" "$OBJ/minigl_open.o" "$OBJ/minigl_getdispatch.o"
echo "$OUT/libmgl.a"
# MGLTest, the test scene.
"$CC" -m68040 -mhard-float -O2 -fno-delete-null-pointer-checks $WARN -noixemul -I"$ROOT/include" -o "$OUT/MGLTest" "$ROOT/tools/mgltest.c" -L"$OUT" -lminigl
echo "$OUT/MGLTest ($(wc -c < "$OUT/MGLTest") bytes)"
"$CC" -m68040 -mhard-float -O2 -fno-delete-null-pointer-checks $WARN -noixemul -DMGLTEST_STATIC -I"$ROOT/include" -o "$OUT/MGLTestStatic" "$ROOT/tools/mgltest.c" -L"$OUT" -lmgl
echo "$OUT/MGLTestStatic ($(wc -c < "$OUT/MGLTestStatic") bytes)"
"$CC" -m68040 -mhard-float -O2 -fno-delete-null-pointer-checks $WARN -noixemul -I"$ROOT/include" -o "$OUT/MGLGlutTest" "$ROOT/tools/mglgluttest.c" -L"$OUT" -lminigl
echo "$OUT/MGLGlutTest ($(wc -c < "$OUT/MGLGlutTest") bytes)"
