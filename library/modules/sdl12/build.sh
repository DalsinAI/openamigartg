#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# SDL 1.2 on OpenGPU's SDL 2: sdl12-compat (Zlib) built for AmigaOS 3.2 as
# libSDL.a, with the GCC 16 stove.
#   library/modules/sdl12/build.sh [OUT_DIR]     (default build/)
# Needs OUT_DIR's SDL 2 (library/modules/sdl2/build.sh: libSDL2.a and
# include/SDL2) and sdl12-compat's tarball in TARBALLS, checked against
# UPSTREAM.json's sha256. Makes OUT_DIR/sdl12/: include/SDL/ (sdl12-compat's
# SDL 1.2 headers), libSDL.a, libSDL_gl.a (SDL 1.2's OpenGL: sdl-config --gl)
# and LICENSE.txt, which tools/make_sdk.py puts in the developer kit.
#
# libSDL.a holds sdl12-compat (patches/: the AmigaOS 3 branch of its loader),
# sdl12_amiga.c (the Team's: its lookups answered from a table) and
# libSDL2.a's own members with every SDL_ name renamed SDL2X_, because
# sdl12-compat defines the SDL_ names with SDL 1.2's meanings. A program
# links -lSDL alone (sdl-config --libs); SDL 2's calls reach SDL2.module as
# they do from libSDL2.a.
set -eu
export LC_ALL=C          # sort and comm agree on order
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
TARBALLS=${TARBALLS:-$HOME/AmigaChrome-dev/upstream-tarballs}
OUT=${1:-$REPO/build}
OUT=$(CDPATH= cd -- "$OUT" && pwd)
CC=$STOVE16/bin/m68k-amigaos-gcc
AR=$STOVE16/bin/m68k-amigaos-ar
NM=$STOVE16/bin/m68k-amigaos-nm
OBJCOPY=$STOVE16/bin/m68k-amigaos-objcopy
eval "$(python3 - "$HERE/UPSTREAM.json" <<'PY'
import json, sys
u = json.load(open(sys.argv[1]))["sdl12-compat"]
print(f"FILE='{u['file']}' DIR='{u['dir']}' SHA='{u['sha256']}'")
PY
)"
[ -f "$OUT/libSDL2.a" ] && [ -d "$OUT/include/SDL2" ] || { echo "sdl12: no SDL 2 in $OUT (library/modules/sdl2/build.sh first)" >&2; exit 1; }
[ -f "$TARBALLS/$FILE" ] || { echo "sdl12: no $TARBALLS/$FILE" >&2; exit 1; }
echo "$SHA  $TARBALLS/$FILE" | sha256sum -c --quiet - || { echo "sdl12: $FILE's sha256 is not UPSTREAM.json's" >&2; exit 1; }

W=$OUT/sdl12-work
S=$W/$DIR
mkdir -p "$W"
if [ ! -f "$S/.patched" ]; then
    [ ! -e "$S" ] || mv "$S" "$W/$DIR.old-$(date +%Y%m%d%H%M%S)"
    tar xzf "$TARBALLS/$FILE" -C "$W"
    for p in "$HERE"/patches/*.patch; do patch -s -d "$S" -p1 < "$p"; done
    touch "$S/.patched"
fi

# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access. GCC 16 also gets
# -fno-tree-loop-distribute-patterns (its loop-shift miscompile).
CF="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -fomit-frame-pointer -noixemul -I$OUT/include/SDL2"
O=$W/obj
mkdir -p "$O"
"$CC" $CF -w -c -o "$O/SDL12_compat.o" "$S/src/SDL12_compat.c"

# The tables of SDL 2's calls (sdl12_amiga.c), from SDL20_syms.h as this
# compiler sees it: sdl12_syms.c (all but SDL_GL_) and sdl12_gl.c.
printf '#define SDL20_SYM(rc, fn, params, args, ret) SDL12SYM fn\n#include "SDL20_syms.h"\n' > "$W/syms.c"
"$CC" -E -P -I"$S/src" "$W/syms.c" | sed -n 's/^ *SDL12SYM \([A-Za-z0-9_]*\).*/\1/p' | sort -u > "$W/syms.list"
table() {   # NAME LIST: a C table of the calls in LIST
    echo "/* Made by library/modules/sdl12/build.sh from sdl12-compat's SDL20_syms.h. */"
    echo '#include "sdl12_amiga.h"'
    sed 's/.*/extern char SDL2X_&[];/' "$2"
    echo "const struct sdl12amiga_sym $1[] = {"
    sed 's/.*/    { "SDL_&", (void *)SDL2X_& },/' "$2"
    echo "};"
    echo "const unsigned $3 = sizeof($1) / sizeof($1[0]);"
}
grep -v '^GL_' "$W/syms.list" > "$W/syms-main.list"
grep '^GL_' "$W/syms.list" > "$W/syms-gl.list"
table SDL12Amiga_syms "$W/syms-main.list" SDL12Amiga_nsyms > "$W/sdl12_syms.c"
table SDL12Amiga_gl "$W/syms-gl.list" SDL12Amiga_ngl > "$W/sdl12_gl.c"
for f in "$HERE/sdl12_amiga.c" "$HERE/sdl12_nogl.c" "$W/sdl12_syms.c" "$W/sdl12_gl.c"; do
    "$CC" $CF -Wall -Wextra -Werror -Wno-unused-parameter -I"$HERE" -c -o "$O/$(basename "$f" .c).o" "$f"
done

# libSDL2.a's members, every SDL_ and SDL2Stub_ name renamed SDL2X_ and SDL2XStub_.
"$NM" "$OUT/libSDL2.a" | sed -n 's/^.* [TDBCU] _\(SDL_\|SDL2Stub_\)\(.*\)$/_\1\2/p' | sort -u \
    | sed -e 's/^_SDL_\(.*\)/_SDL_\1 _SDL2X_\1/' -e 's/^_SDL2Stub_\(.*\)/_SDL2Stub_\1 _SDL2XStub_\1/' > "$W/rename.map"
"$OBJCOPY" --redefine-syms="$W/rename.map" "$OUT/libSDL2.a" "$W/libSDL2X.a"
M=$W/members
mkdir -p "$M"
(cd "$M" && "$AR" x "$W/libSDL2X.a")
mkdir -p "$OUT/sdl12"
stamp=$(date +%Y%m%d%H%M%S)
for f in libSDL.a libSDL_gl.a; do [ ! -e "$OUT/sdl12/$f" ] || mv "$OUT/sdl12/$f" "$W/$f.old-$stamp"; done
LIB=$OUT/sdl12/libSDL.a
"$AR" rcs "$LIB" "$O/SDL12_compat.o" "$O/sdl12_amiga.o" "$O/sdl12_syms.o" "$O/sdl12_nogl.o" \
    $(cd "$M" && "$AR" t "$W/libSDL2X.a" | sed "s|^|$M/|")
"$AR" rcs "$OUT/sdl12/libSDL_gl.a" "$O/sdl12_gl.o"

# Every SDL 2 call sdl12-compat uses must be in libSDL2.a.
"$NM" "$W/libSDL2X.a" | sed -n 's/^.* [TDBC] _SDL2X_\(.*\)$/\1/p' | sort -u > "$W/have"
missing=$(comm -23 "$W/syms.list" "$W/have" || true)
[ -z "$missing" ] || { echo "sdl12: SDL 2 calls not in libSDL2.a: $missing" >&2; exit 1; }
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$O"/*.o

# The headers and the licence, for the kit.
mkdir -p "$OUT/sdl12/include/SDL"
cp "$S"/include/SDL/*.h "$OUT/sdl12/include/SDL/"
cp "$S/LICENSE.txt" "$OUT/sdl12/LICENSE.txt"
echo "$LIB ($(wc -c < "$LIB") bytes)"
