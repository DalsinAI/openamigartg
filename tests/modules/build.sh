#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Test.module (with the GCC 16 stove, as GL.module and SDL2.module are) and
# ModuleCheck (with the GCC 6.5 stove, as most programs are: the module ABI
# holds between them).
#   tests/modules/build.sh [OUT_DIR]     (default build/modules)
#   STOVE16: the os32-gcc16 install; STOVE: the os32 one.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
OUT=${1:-$ROOT/build/modules}
COMMON=$ROOT/library/modules/common
mkdir -p "$OUT"
CC16=$STOVE16/bin/m68k-amigaos-gcc
F="-m68040 -m68881 -mnobitfield -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/include -I$COMMON"
"$CC16" $F -c -o "$OUT/module_start.o" "$COMMON/module_start.s"
"$CC16" $F -c -o "$OUT/module_rt.o" "$COMMON/module_rt.c"
"$CC16" $F -c -o "$OUT/test_module.o" "$HERE/test_module.c"
# module_start.o first: its jump is the module's entry.
"$CC16" -m68040 -m68881 -noixemul -nostartfiles -o "$OUT/Test.module" "$OUT/module_start.o" "$OUT/module_rt.o" "$OUT/test_module.o" -lm -lamiga
echo "$OUT/Test.module ($(wc -c < "$OUT/Test.module") bytes)"
"$STOVE/prefix/bin/m68k-amigaos-gcc" -m68040 -m68881 -O2 -Wall -Werror -noixemul -I"$ROOT/include" -o "$OUT/ModuleCheck" "$HERE/module_check.c" -lm
echo "$OUT/ModuleCheck ($(wc -c < "$OUT/ModuleCheck") bytes)"
python3 "$ROOT/tools/fpcr_check.py" "$STOVE16/bin/m68k-amigaos-objdump" "$OUT/Test.module" "$OUT/ModuleCheck"
