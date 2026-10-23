#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL3.module, libSDL3.a (the link stub), libSDL3_static.a, SDL's public
# headers (OUT/include/SDL3) and SDL's tests, with the GCC 16 stove. Built
# the same way as SDL2.module (library/modules/sdl2/build.sh).
#
# SDL 3.4.14 (Zlib) is the core. The Amiga back ends start from SDL3_68k
# (Zlib) at the commit UPSTREAM.json pins; patches/sdl3 adds the platform to
# SDL, patches/amigaos3 holds the Team's changes to those back ends, and
# src/ and include/ the Team's own files. Nothing third-party is committed:
# the tarball is checked against its sha256 and SDL3_68k is fetched by
# commit.
#
#   library/modules/sdl3/build.sh [OUT_DIR] [TARGETS]
#       OUT_DIR  default build/; TARGETS default: module stub static tests
#   CPU="-m68020 -m68881"   another CPU (default -m68040 -m68881)
#
# Needs the GCC 16 stove (STOVE, default ~/AmigaChrome/stoves/os32-gcc16/prefix)
# and the AHI developer headers (SDK_HEADERS, default the OS 3.2 stove's
# include directory). TARBALLS is where the SDL tarball is looked for before
# it is downloaded.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/build}
shift 2>/dev/null || true
TARGETS=${*:-module stub static tests}
TARBALLS=${TARBALLS:-$HOME/AmigaChrome-dev/upstream-tarballs}
SDK_HEADERS=${SDK_HEADERS:-$HOME/AmigaChrome/stoves/os32/prefix/m68k-amigaos/include}

pin() { python3 -c "import json,sys; print(json.load(open('$HERE/UPSTREAM.json'))[sys.argv[1]][sys.argv[2]])" "$1" "$2"; }

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
WORK=$OUT/sdl3-work
mkdir -p "$WORK"

# 1. SDL 3.4.14: from TARBALLS, or downloaded once into WORK/tarballs;
#    either way checked against its sha256.
f=$(pin sdl3 file)
TARBALL=$TARBALLS/$f
if [ ! -f "$TARBALL" ]; then
    TARBALL=$WORK/tarballs/$f
    mkdir -p "$WORK/tarballs"
    [ -f "$TARBALL" ] || curl -fsL -o "$TARBALL" "$(pin sdl3 url)"
fi
echo "$(pin sdl3 sha256)  $TARBALL" | sha256sum -c - >&2

# 2. SDL3_68k at its pinned commit, for the Amiga back ends.
A3=$WORK/SDL3_68k
COMMIT=$(pin amigaos3 commit)
SUB=$(pin amigaos3 subdir)
[ -d "$A3/.git" ] || git clone -q "$(pin amigaos3 url)" "$A3"
git -C "$A3" cat-file -e "$COMMIT^{commit}" 2>/dev/null || git -C "$A3" fetch -q origin "$COMMIT"

# 3. A fresh tree: SDL; the opengpu renderer starts as a copy of SDL's
#    software one (patches/sdl3/0004 turns it into the renderer); then the
#    back ends, the patches and the Team's files.
TREE=$WORK/$(pin sdl3 dir)
rm -rf "$TREE"
tar xzf "$TARBALL" -C "$WORK"
mkdir -p "$TREE/src/render/opengpu"
cp "$TREE/src/render/software/SDL_render_sw.c" "$TREE/src/render/opengpu/SDL_render_opengpu.c"
for d in video audio thread timer time joystick filesystem locale dialog; do
    git -C "$A3" archive "$COMMIT" "$SUB/src/$d/amigaos3" | tar x --strip-components=1 -C "$TREE"
done
git -C "$A3" show "$COMMIT:$SUB/src/events/scancodes_amiga.h" > "$TREE/src/events/scancodes_amiga.h"
git -C "$A3" show "$COMMIT:$SUB/LICENSE.txt" > "$TREE/LICENSE.SDL3_68k.txt"
for p in "$HERE"/patches/sdl3/*.patch "$HERE"/patches/amigaos3/*.patch; do
    patch -s -p1 -d "$TREE" < "$p"
done
cp -R "$HERE/src/." "$TREE/src/"
cp -p "$HERE/include/SDL_build_config_amigaos3.h" "$TREE/include/build_config/"   # its time: the Makefile rebuilds on a change

# 4. The AHI headers the GCC 16 stove lacks.
SDK=$WORK/sdk-include
for f in devices/ahi.h proto/ahi.h inline/ahi.h clib/ahi_protos.h; do
    mkdir -p "$SDK/$(dirname "$f")"
    cp "$SDK_HEADERS/$f" "$SDK/$f"
done

# 5. SDL's public headers for programs: OUT/include/SDL3.
mkdir -p "$OUT/include/SDL3"
cp "$TREE"/include/SDL3/*.h "$OUT/include/SDL3/"

make -f "$HERE/Makefile" -j"${JOBS:-$(nproc)}" SDL="$TREE" OUT="$OUT" WORK="$WORK" ${CPU:+CPU="$CPU"} $TARGETS

# 6. GCC 16's FPCR clash (library/modules/sdl2/fpcr_check.py) must not be in
#    anything built.
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
set --
for f in "$OUT/SDL3.module" "$OUT"/libSDL3*.a; do [ -f "$f" ] && set -- "$@" "$f"; done
[ $# -eq 0 ] || python3 "$HERE/../sdl2/fpcr_check.py" "$STOVE/bin/m68k-amigaos-objdump" "$@"
echo "SDL 3 built in $OUT"
