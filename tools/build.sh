#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTG's tools, with the os32 stove (bebbo's m68k-amigaos-gcc, NDK 3.2).
#   tools/build.sh [OUT_DIR]     (default build/)
# STOVE: the amiga-gcc install (holding prefix/bin); or CC: the compiler.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
OUT=${1:-$HERE/../build}
mkdir -p "$OUT"
"$CC" -m68020 -O2 -Wall -Werror -noixemul -o "$OUT/LibCount" "$HERE/libcount.c"
echo "$OUT/LibCount ($(wc -c < "$OUT/LibCount") bytes)"
"$CC" -m68040 -m68881 -mnobitfield -O2 -Wall -Werror -noixemul -I"$HERE/../include" -o "$OUT/OpenGPUCheck" "$HERE/ogpu_check.c" \
    "$HERE/../tests/golden_scenes.c" "$HERE/../library/opengpu/ogpu_core.c" "$HERE/../library/opengpu/ogpu_build.c" -lm
echo "$OUT/OpenGPUCheck ($(wc -c < "$OUT/OpenGPUCheck") bytes)"
"$CC" -m68040 -m68881 -mnobitfield -O2 -Wall -Werror -noixemul -I"$HERE/../include" -o "$OUT/OpenGPUBench" "$HERE/ogpu_bench.c" \
    "$HERE/../library/opengpu/ogpu_core.c" "$HERE/../library/opengpu/ogpu_build.c" -lm
echo "$OUT/OpenGPUBench ($(wc -c < "$OUT/OpenGPUBench") bytes)"
"$CC" -m68040 -m68881 -O2 -Wall -Werror -noixemul -I"$HERE/../include" -o "$OUT/GfxBench" "$HERE/gfx_bench.c" -lm
echo "$OUT/GfxBench ($(wc -c < "$OUT/GfxBench") bytes)"
"$CC" -m68040 -m68881 -O2 -Wall -Werror -noixemul -I"$HERE/../include" -o "$OUT/OpenRTGExact" "$HERE/ortg_exact.c"
echo "$OUT/OpenRTGExact ($(wc -c < "$OUT/OpenRTGExact") bytes)"
