#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# OpenAL.module, libopenal.a (the link stub), libopenal_static.a, the
# headers for programs (OUT/include/AL) and the tests, with the GCC 16
# stove. Built the same way as SDL3.module (library/modules/sdl3/build.sh),
# with nothing third-party: the module is the Team's own, written from the
# OpenAL 1.1 specification and the extension texts (README.md).
#
#   library/modules/openal/build.sh [OUT_DIR] [TARGETS]
#       OUT_DIR  default build/; TARGETS default: module stub static tests
#   CPU="-m68020 -m68881"   another CPU (default -m68040 -m68881)
#
# Needs the GCC 16 stove (STOVE, default ~/AmigaChrome/stoves/os32-gcc16/prefix)
# and the AHI developer headers (SDK_HEADERS, default the OS 3.2 stove's
# include directory). The PC tests are tests/openal/run_host.sh.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/build}
shift 2>/dev/null || true
TARGETS=${*:-module stub static tests}
SDK_HEADERS=${SDK_HEADERS:-$HOME/AmigaChrome/stoves/os32/prefix/m68k-amigaos/include}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
WORK=$OUT/openal-work
mkdir -p "$WORK"

# 1. The AHI headers the GCC 16 stove lacks.
SDK=$WORK/sdk-include
for f in devices/ahi.h proto/ahi.h inline/ahi.h clib/ahi_protos.h; do
    mkdir -p "$SDK/$(dirname "$f")"
    cp "$SDK_HEADERS/$f" "$SDK/$f"
done

# 2. The headers for programs: OUT/include/AL.
mkdir -p "$OUT/include/AL"
cp "$HERE"/include/AL/*.h "$OUT/include/AL/"

make -f "$HERE/Makefile" -j"${JOBS:-$(nproc)}" OUT="$OUT" WORK="$WORK" STOVE="$STOVE" ${CPU:+CPU="$CPU"} $TARGETS

# 3. GCC 16's FPCR clash (tools/fpcr_check.py) must not be in anything built:
#    the module, and the libraries' objects.
set --
for f in "$OUT/OpenAL.module" "$WORK"/obj-static/*.o "$WORK"/obj-stub/*.o; do
    [ -f "$f" ] && set -- "$@" "$f"
done
[ $# -eq 0 ] || python3 "$ROOT/tools/fpcr_check.py" "$STOVE/bin/m68k-amigaos-objdump" "$@"
echo "OpenAL built in $OUT"
