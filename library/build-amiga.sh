#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target library):
# openrtg.library, cybergraphics.library and the monitor driver as ROM
# modules (library/build.sh module), and openrtg.library and
# cybergraphics.library as library/build.sh builds them for LIBS:, with the
# stove's compiler. The rest of library/build.sh (opengpu.library, Warp3D,
# MiniGL, SDL) is not built here.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
NM=${CC%gcc}nm
export STOVE CC
mkdir -p "$OUT/libs"
"$CC" --version | head -1
sh "$HERE/build.sh" module "$OUT"
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
for lib in openrtg cybergraphics; do
    if [ "$lib" = openrtg ]; then
        src="$HERE/openrtg_lib.c $HERE/modes.c $HERE/displaydb.c $HERE/screens.c $HERE/pixels.c $HERE/opengpu/ogpu_build.c $HERE/string.c $HERE/ortg_stub.c"
    else
        src="$ROOT/compat/cybergraphics_lib.c $HERE/ortg_stub.c"
    fi
    # as library/build.sh's build_ortg, at -O2 for the 68040
    # shellcheck disable=SC2086
    "$CC" -m68040 $SOFT -mnobitfield -O2 -ffixed-a4 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -nostartfiles -nostdlib -I"$ROOT/include" -o "$OUT/libs/$lib.library" $src -lgcc
    und=$("$NM" -u "$OUT/libs/$lib.library" || true)
    [ -z "$und" ] || { echo "undefined in $lib.library: $und" >&2; exit 1; }
    python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/libs/$lib.library"
    echo "$OUT/libs/$lib.library ($(wc -c < "$OUT/libs/$lib.library") bytes)"
done
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/libs/openrtg.library" "$OUT/libs/cybergraphics.library"
sha256sum "$OUT/libs/openrtg.library" "$OUT/libs/cybergraphics.library"
