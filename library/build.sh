#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# openrtg.library (bare: no startup code or C library; its RomTag's stub
# comes first) and C:OpenRTG, with the os32 stove (bebbo's m68k-amigaos-gcc).
#   library/build.sh [OUT_DIR]     (default build/)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
"$CC" -m68020 -O2 -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib -I"$ROOT/include" \
    -o "$OUT/openrtg.library" "$HERE/openrtg_lib.c" "$HERE/modes.c" "$HERE/string.c" -lgcc
echo "$OUT/openrtg.library ($(wc -c < "$OUT/openrtg.library") bytes)"
"$CC" -m68020 -O2 -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/OpenRTG" "$ROOT/tools/openrtg_cmd.c"
echo "$OUT/OpenRTG ($(wc -c < "$OUT/OpenRTG") bytes)"
