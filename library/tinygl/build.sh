#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# TinyGL on OpenGPU, with the os32 stove (bebbo's m68k-amigaos-gcc):
#   tinygl.library  the stand-in programs open (bare: no startup code or C library)
#   libtinygl.a     TinyGL's context calls and a few GLU calls, on GLA (libGL.a)
#   library/tinygl/build.sh [OUT_DIR]     (default build/)
# libtinygl.a is built for the 68040 with its FPU, as libGL.a and SDL 2 are,
# and links with either stove.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
AR=${AR:-$STOVE/prefix/bin/m68k-amigaos-ar}
OUT=${1:-$ROOT/build}
OBJ="$OUT/tinygl-obj"
mkdir -p "$OUT" "$OBJ"
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
WARN="-Wall -Wextra -Werror -Wno-unused-parameter"
"$CC" -m68020 -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin $WARN \
    -nostartfiles -nostdlib -I"$ROOT/include" -o "$OUT/tinygl.library" "$HERE/tinygl_lib.c" -lgcc
echo "$OUT/tinygl.library ($(wc -c < "$OUT/tinygl.library") bytes)"
# GLA's headers come from the GL module's source; GL's own (GL/gl.h) from
# Mesa's, given as MESA_INCLUDE, or the kit's or a stove's include/.
GLA="$ROOT/library/modules/gl/gla"
mkdir -p "$OBJ/inc"
ln -sfn "$GLA" "$OBJ/inc/gla"
if [ -z "${MESA_INCLUDE:-}" ]; then
    ENVF=${GL_WORK:-$ROOT/library/modules/gl/build}/mesa-build-m68k-amigaos/gla-link.env
    [ ! -f "$ENVF" ] || MESA_INCLUDE=$(sed -n "s/^SRC='\(.*\)'$/\1/p" "$ENVF")/include
fi
MI=
[ -z "${MESA_INCLUDE:-}" ] || MI="-I$MESA_INCLUDE"
"$CC" -m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -noixemul $WARN \
    -I"$ROOT/include" -I"$OBJ/inc" $MI -c -o "$OBJ/tinygl.o" "$HERE/tinygl.c"
# No 68881 instruction the 68040 leaves to software.
if "${CC%gcc}objdump" -d "$OBJ/tinygl.o" | grep -Eq '\sf(sin|cos|tan|atan|etox|logn|log10|log2|intrz|int|mod|rem)[.a-z]*\s'; then
    echo "libtinygl.a: has FPU instructions the 68040 lacks" >&2
    exit 1
fi
"$AR" rcs "$OUT/libtinygl.a" "$OBJ/tinygl.o"
echo "$OUT/libtinygl.a"
