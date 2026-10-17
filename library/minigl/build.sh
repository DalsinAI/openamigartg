#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library (bare: no startup code or C library; the RomTag's stub
# comes first), with the OpenGPU core and 3D core linked in for drawing on
# the 68k, its link libraries and its test program, with the os32 stove
# (bebbo's m68k-amigaos-gcc).
#   library/minigl/build.sh [OUT_DIR]     (default build/)
#   library/minigl/build.sh api           remake the interface files from minigl_api.txt
# The library runs on any 68040 and uses the FPU when there is one: its front
# end (every mgl_*.c but mgl_lib.c) is built twice, for the FPU (-m68881)
# and soft (tools/m68k-soft-m68040.specs: libnix's soft libm020, through the
# mathieee libraries). Each half is linked on its own (ld -r) with its own
# libgcc (and soft, libm), and its symbols renamed (mglF_, mglS_; the tables
# mgl_table_fpu and mgl_table_soft), so the halves never call each other
# across the two float ABIs. mgl_lib.c chooses one table at the first open
# (AttnFlags; ENV:MINIGL_FORCE_SOFT). It keeps a4 for the programs whose
# functions it calls (-ffixed-a4: small-data programs).
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
NM=${NM:-$STOVE/prefix/bin/m68k-amigaos-nm}
OBJCOPY=${OBJCOPY:-$STOVE/prefix/bin/m68k-amigaos-objcopy}
OUT=${1:-$ROOT/build}
OBJ="$OUT/mgl-obj"
mkdir -p "$OUT" "$OBJ"
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
WARN="-Wall -Wextra -Werror -Wno-unused-parameter"
SOFT="-m68040 -specs=$ROOT/tools/m68k-soft-m68040.specs"
FPU="-m68040 -m68881"
FLAGS="-mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -ffixed-a4 -fno-builtin $WARN -I$ROOT/include ${MGL_CFLAGS:-}"
HOT="mgl_math mgl_state mgl_matrix mgl_vertex mgl_pipe mgl_texture mgl_out mgl_list mgl_glu mgl_glut mgl_display mgl_table"
# $1 the half (fpu or soft), $2 its symbol prefix, $3 its CPU flags, the rest
# the archives it links (its libgcc, and soft, libnix's libm): OBJ/<half>/ren,
# its objects and those archives with every global they define renamed, so
# each half calls only its own helpers. (The stoves' ld can't link -r to hunk
# objects, so the names are changed object by object, with one list.)
build_half() {
    half=$1 pre=$2 cpu=$3; shift 3
    mkdir -p "$OBJ/$half/ren"
    for f in $HOT; do
        "$CC" $cpu $FLAGS -fno-common -c -o "$OBJ/$half/$f.o" "$HERE/$f.c"
    done
    "$NM" -g --defined-only "$OBJ/$half"/mgl_*.o "$@" 2>/dev/null | awk -v p="$pre" -v h="$half" \
        'NF == 3 && !seen[$3]++ { s = $3; if (s == "_mgl_table") print s, "_mgl_table_" h; else print s, "_" p substr(s, 2) }' > "$OBJ/$half/syms"
    for f in $HOT; do
        "$OBJCOPY" --redefine-syms="$OBJ/$half/syms" "$OBJ/$half/$f.o" "$OBJ/$half/ren/$f.o"
    done
    n=0
    for a in "$@"; do
        n=$((n + 1))
        "$OBJCOPY" --redefine-syms="$OBJ/$half/syms" "$a" "$OBJ/$half/ren/lib$n.a"
    done
}
# The archives each half takes: as the specs file and -m68881 pick them.
GCCDIR=$(dirname "$("$CC" -print-libgcc-file-name)")
NIXDIR=$(dirname "$("$CC" -noixemul -print-file-name=libm.a)")
for a in "$GCCDIR/libm020/libm881/libgcc.a" "$GCCDIR/libm020/libgcc.a" "$NIXDIR/libm020/libm.a"; do
    [ -f "$a" ] || { echo "no $a" >&2; exit 1; }
done
OGPU="$ROOT/library/opengpu"
# The cores as opengpu.library builds them (-m68040, no FPU code): one copy,
# which both halves call (no float crosses).
for f in ogpu_core ogpu_3d ogpu_build ogpu_build3d; do
    "$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -ffixed-a4 -fno-builtin $WARN -I"$ROOT/include" \
        -c -o "$OBJ/$f.o" "$OGPU/$f.c"
done
build_half fpu mglF_ "$FPU" "$GCCDIR/libm020/libm881/libgcc.a"
build_half soft mglS_ "$SOFT" "$NIXDIR/libm020/libm.a" "$GCCDIR/libm020/libgcc.a"
"$CC" $SOFT $FLAGS -fno-toplevel-reorder -nostartfiles -nostdlib -o "$OUT/minigl.library" \
    "$HERE/mgl_lib.c" $(for f in $HOT; do echo "$OBJ/fpu/ren/$f.o"; done) $(for f in $HOT; do echo "$OBJ/soft/ren/$f.o"; done) \
    "$OBJ/ogpu_core.o" "$OBJ/ogpu_3d.o" "$OBJ/ogpu_build.o" "$OBJ/ogpu_build3d.o" \
    "$OBJ"/fpu/ren/lib*.a "$OBJ"/soft/ren/lib*.a -lgcc
und=$("$NM" -u "$OUT/minigl.library" || true)
[ -z "$und" ] || { echo "minigl.library: undefined $und" >&2; exit 1; }
echo "$OUT/minigl.library ($(wc -c < "$OUT/minigl.library") bytes)"
# Instructions the 68040's FPU leaves to software must not be in it.
if "$OBJDUMP" -d "$OUT/minigl.library" 2>/dev/null | grep -Eq '\sf(sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|etox|etoxm1|twotox|tentox|logn|lognp1|log10|log2|intrz|int|getexp|getman|mod|rem|scale|movecr)[.a-z]*\s'; then
    echo "minigl.library: has FPU instructions the 68040 lacks" >&2
    "$OBJDUMP" -d "$OUT/minigl.library" | grep -E '\sf(sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|etox|etoxm1|twotox|tentox|logn|lognp1|log10|log2|intrz|int|getexp|getman|mod|rem|scale|movecr)[.a-z]*\s' | head >&2
    exit 1
fi
# No conditional branch first after a label: GCC 6.5's flags bug (tests/scan_68k_branches.py).
for f in "$HERE"/mgl_*.c; do
    "$CC" $FPU $FLAGS -S -o "$OBJ/fpu/$(basename "$f" .c).s" "$f"
    "$CC" $SOFT $FLAGS -S -o "$OBJ/soft/$(basename "$f" .c).s" "$f"
done
python3 "$ROOT/tests/scan_68k_branches.py" "$OBJ"/fpu/mgl_*.s "$OBJ"/soft/mgl_*.s
# libminigl.a: MiniGLOpen and the table, for programs that include <proto/minigl.h>.
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/minigl_base.o" "$HERE/client/minigl_base.c"
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/minigl_open.o" "$HERE/client/minigl_open.c"
"$CC" -m68040 -c -o "$OBJ/minigl_getdispatch.o" "$HERE/client/minigl_getdispatch.s"

"$AR" rcs "$OUT/libminigl.a" "$OBJ/minigl_base.o" "$OBJ/minigl_open.o" "$OBJ/minigl_getdispatch.o"
echo "$OUT/libminigl.a"
# libmgl.a: every call as a function, for programs written for the linked-in MiniGL.
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks $WARN -I"$ROOT/include" -c -o "$OBJ/mgl_static.o" "$HERE/mgl_static.c"
"$AR" rcs "$OUT/libmgl.a" "$OBJ/mgl_static.o" "$OBJ/minigl_base.o" "$OBJ/minigl_open.o" "$OBJ/minigl_getdispatch.o"
echo "$OUT/libmgl.a"
# MGLTest, the test scene: soft float, so it runs on any 68040 (the library
# chooses its own table).
"$CC" $SOFT -O2 -fno-delete-null-pointer-checks $WARN -noixemul -I"$ROOT/include" -o "$OUT/MGLTest" "$ROOT/tools/mgltest.c" -L"$OUT" -lminigl -lm
echo "$OUT/MGLTest ($(wc -c < "$OUT/MGLTest") bytes)"
"$CC" $SOFT -O2 -fno-delete-null-pointer-checks $WARN -noixemul -DMGLTEST_STATIC -I"$ROOT/include" -o "$OUT/MGLTestStatic" "$ROOT/tools/mgltest.c" -L"$OUT" -lmgl -lm
echo "$OUT/MGLTestStatic ($(wc -c < "$OUT/MGLTestStatic") bytes)"
"$CC" $SOFT -O2 -fno-delete-null-pointer-checks $WARN -noixemul -I"$ROOT/include" -o "$OUT/MGLGlutTest" "$ROOT/tools/mglgluttest.c" -L"$OUT" -lminigl -lm
echo "$OUT/MGLGlutTest ($(wc -c < "$OUT/MGLGlutTest") bytes)"
