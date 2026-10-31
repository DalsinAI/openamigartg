#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tools): the
# tools as tools/build.sh builds them (OpenGPUCheck, OpenGPUBench, LibCount
# and the rest), with the stove's compiler.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
CC=${CC:-$PREFIX/bin/m68k-amigaos-gcc}
export CC
mkdir -p "$OUT"
"$CC" --version | head -1
sh "$HERE/build.sh" "$OUT"
sha256sum "$OUT"/*
