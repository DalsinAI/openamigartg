#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTG's host tests, and a check that the library's C builds for the 68k.
#   tests/run.sh      (STOVE or CC68K as in tools/build.sh; the 68k check is skipped without it)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
mkdir -p "$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/test_modes" "$HERE/test_modes.c" "$HERE/../library/modes.c"
"$OUT/test_modes"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    "$CC68K" -m68020 -O2 -Wall -Wextra -Werror -c -o "$OUT/modes-m68k.o" "$HERE/../library/modes.c"
    echo "library/modes.c builds for the 68k"
else
    echo "no m68k compiler; the 68k check is skipped"
fi
