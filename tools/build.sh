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
