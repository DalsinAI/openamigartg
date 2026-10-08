#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Test.module, shared (-fbaserel32 -resident32: code loaded once, data per
# program), and TestEach.module, a copy for each program, both with the GCC
# 16 stove as GL.module and SDL2.module are; and ModuleCheck with the GCC
# 6.5 stove, as most programs are (the module ABI holds between them).
#   tests/modules/build.sh [OUT_DIR]     (default build/modules)
#   STOVE16: the os32-gcc16 install; STOVE: the os32 one.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
OUT=${1:-$ROOT/build/modules}
COMMON=$ROOT/library/modules/common
mkdir -p "$OUT/shared" "$OUT/each"
CC16=$STOVE16/bin/m68k-amigaos-gcc
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
F="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/include -I$COMMON"
# kind: shared or each; its extra flags.
build() {
    K=$1 NAME=$2; shift 2
    "$CC16" $F "$@" -c -o "$OUT/$K/module_start.o" "$COMMON/module_start.S"
    "$CC16" $F "$@" -c -o "$OUT/$K/module_rt.o" "$COMMON/module_rt.c"
    "$CC16" $F "$@" -c -o "$OUT/$K/test_module.o" "$HERE/test_module.c"
    # module_start.o first: its first code is the module's.
    "$CC16" -m68040 -m68881 -noixemul -nostartfiles "$@" -Wl,-Map,"$OUT/$K/$NAME.map" -o "$OUT/$K/$NAME.debug" \
        "$OUT/$K/module_start.o" "$OUT/$K/module_rt.o" "$OUT/$K/test_module.o" -lm -lamiga
    "$STOVE16/bin/m68k-amigaos-strip" -o "$OUT/$NAME" "$OUT/$K/$NAME.debug"
    echo "$OUT/$NAME ($(wc -c < "$OUT/$NAME") bytes)"
}
build shared Test.module -fbaserel32 -resident32
build each TestEach.module
# Nothing in the shared one may reach its data except through A4.
python3 "$ROOT/tools/baserel_check.py" "$STOVE16/bin/m68k-amigaos-" "$OUT/shared/Test.module.debug" "$OUT/shared/Test.module.map"
"$STOVE/prefix/bin/m68k-amigaos-gcc" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -ffixed-a4 -Wall -Werror -noixemul -I"$ROOT/include" -o "$OUT/ModuleCheck" "$HERE/module_check.c" -lm
echo "$OUT/ModuleCheck ($(wc -c < "$OUT/ModuleCheck") bytes)"
python3 "$ROOT/tools/fpcr_check.py" "$STOVE16/bin/m68k-amigaos-objdump" "$OUT/Test.module" "$OUT/TestEach.module" "$OUT/ModuleCheck"
