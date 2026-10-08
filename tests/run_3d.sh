#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The 3D core's host tests (every test with the fast spans and with the
# general ones, under AddressSanitizer and UBSan), its golden scene, and the
# 68k checks: the core builds for the library, with no branch shape GCC 6.5
# gets wrong (scan_68k_branches.py), and Warp3D.h's structures
# have every offset 68k programs were compiled with.
#   tests/run_3d.sh      (STOVE or CC68K as in tests/run.sh; the 68k checks are skipped without one)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
OGPU="$HERE/../library/opengpu"
mkdir -p "$OUT"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_3d" "$HERE/test_3d.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lm
"$OUT/test_3d" "$HERE/golden/opengpu-3d.txt"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    "$CC68K" -m68020 -mnobitfield -O2 -Wall -Wextra -Werror -fno-builtin -c -o "$OUT/ogpu_3d-m68k.o" "$OGPU/ogpu_3d.c"
    # no conditional branch first after a label (GCC 6.5's flags bug: tests/scan_68k_branches.py)
    "$CC68K" -m68020 -mnobitfield -O2 -fomit-frame-pointer -fno-builtin -S -o "$OUT/ogpu_3d-m68k.s" "$OGPU/ogpu_3d.c"
    python3 "$HERE/scan_68k_branches.py" "$OUT/ogpu_3d-m68k.s"
    "$CC68K" -m68020 -std=gnu11 -I"$HERE/../include" -c -o "$OUT/w3d_layout.o" "$HERE/w3d_layout.c"
    echo "library/opengpu/ogpu_3d.c builds for the 68k; include/Warp3D/Warp3D.h has every offset 68k programs use"
else
    echo "no m68k compiler; the 68k checks are skipped"
fi
