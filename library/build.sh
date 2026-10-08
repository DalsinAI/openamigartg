#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# openrtg.library, cybergraphics.library and opengpu.library (bare: no startup code or C
# library; the RomTag's stub comes first), C:OpenRTG and its monitor driver, with the os32 stove (bebbo's m68k-amigaos-gcc).
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
    -o "$OUT/openrtg.library" "$HERE/openrtg_lib.c" "$HERE/modes.c" "$HERE/displaydb.c" "$HERE/screens.c" "$HERE/pixels.c" "$HERE/opengpu/ogpu_build.c" "$HERE/string.c" -lgcc
echo "$OUT/openrtg.library ($(wc -c < "$OUT/openrtg.library") bytes)"
"$CC" -m68020 -O2 -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib -I"$ROOT/include" \
    -o "$OUT/cybergraphics.library" "$ROOT/compat/cybergraphics_lib.c" -lgcc
echo "$OUT/cybergraphics.library ($(wc -c < "$OUT/cybergraphics.library") bytes)"
"$CC" -m68020 -O2 -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib -I"$ROOT/include" \
    -o "$OUT/opengpu.library" "$HERE/opengpu/opengpu_lib.c" "$HERE/opengpu/ogpu_core.c" "$HERE/string.c" -lgcc
echo "$OUT/opengpu.library ($(wc -c < "$OUT/opengpu.library") bytes)"
"$CC" -m68020 -O2 -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/OpenRTG" "$ROOT/tools/openrtg_cmd.c"
echo "$OUT/OpenRTG ($(wc -c < "$OUT/OpenRTG") bytes)"
"$CC" -m68020 -O2 -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/OpenRTG-Monitor" "$ROOT/tools/openrtg_monitor.c"
echo "$OUT/OpenRTG-Monitor ($(wc -c < "$OUT/OpenRTG-Monitor") bytes)"
[ ! -x "${SDL2_STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] || STOVE="${SDL2_STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}" "$HERE/modules/sdl2/build.sh" "$OUT"   # SDL 2 (library/modules/sdl2), with the GCC 16 stove
