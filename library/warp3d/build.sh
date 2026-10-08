#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D.library (bare: no startup code or C library; the RomTag's stub comes
# first), with the OpenGPU core and 3D core linked in for the W3D_CPU driver,
# and the W3D test program, with the os32 stove (bebbo's m68k-amigaos-gcc).
#   library/warp3d/build.sh [OUT_DIR]          (default build/)
#   library/warp3d/build.sh headers            remake include/inline, proto, clib from warp3d_lib.sfd
# Warp3D needs an FPU (as Warp3D always has): -m68040 -mhard-float, and
# -mnobitfield so the 3D core's pixel loops keep to fast instructions.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
SFDC=${SFDC:-$STOVE/prefix/bin/sfdc}
if [ "${1:-}" = headers ]; then
    for m in macros:inline/Warp3D.h proto:proto/Warp3D.h clib:clib/Warp3D_protos.h; do
        mode=${m%%:*}; out=$ROOT/include/${m#*:}
        { echo "/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE)."
          echo " * SPDX-License-Identifier: MIT"
          echo " * Made by sfdc from library/warp3d/warp3d_lib.sfd (library/warp3d/build.sh headers). */"
          "$SFDC" --quiet --mode="$mode" --target=m68k-amigaos -o /dev/stdout "$HERE/warp3d_lib.sfd"; } > "$out"
        echo "$out"
    done
    exit 0
fi
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
FLAGS="-m68040 -mhard-float -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/include"
OGPU="$ROOT/library/opengpu"
# The cores as opengpu.library builds them (-m68020): on the AC090 their
# 68040-tuned code ran a quarter slower (8 October 2026, W3DTest).
mkdir -p "$OUT/w3d-obj"
for f in ogpu_core ogpu_3d ogpu_build ogpu_build3d; do
    "$CC" -m68020 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -I"$ROOT/include" -c -o "$OUT/w3d-obj/$f.o" "$OGPU/$f.c"
done
"$CC" $FLAGS -fno-toplevel-reorder -fno-builtin -nostartfiles -nostdlib -o "$OUT/Warp3D.library" \
    "$HERE/w3d_lib.c" "$HERE/w3d_ctx.c" "$HERE/w3d_batch.c" "$HERE/w3d_draw.c" "$HERE/w3d_texture.c" "$HERE/w3d_query.c" \
    "$OUT/w3d-obj/ogpu_core.o" "$OUT/w3d-obj/ogpu_3d.o" "$OUT/w3d-obj/ogpu_build.o" "$OUT/w3d-obj/ogpu_build3d.o" -lgcc
echo "$OUT/Warp3D.library ($(wc -c < "$OUT/Warp3D.library") bytes)"
"$CC" $FLAGS -noixemul -o "$OUT/W3DTest" "$ROOT/tools/w3dtest.c"
echo "$OUT/W3DTest ($(wc -c < "$OUT/W3DTest") bytes)"
"$CC" $FLAGS -noixemul -o "$OUT/W3DCheck" "$ROOT/tools/w3dcheck.c"
echo "$OUT/W3DCheck ($(wc -c < "$OUT/W3DCheck") bytes)"
"$CC" -m68020 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul -I"$ROOT/include" -o "$OUT/Warp3DPrefs" "$ROOT/tools/w3dprefs.c"
echo "$OUT/Warp3DPrefs ($(wc -c < "$OUT/Warp3DPrefs") bytes)"
# The headers' structures, as 68k programs were compiled (compile-time checks only).
"$CC" -m68020 -std=gnu11 -fno-delete-null-pointer-checks -I"$ROOT/include" -c -o "$OUT/w3d_layout.o" "$ROOT/tests/w3d_layout.c"
echo "include/Warp3D/Warp3D.h: every structure offset as 68k programs have it"
