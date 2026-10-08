#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL2.module, libSDL2.a (the link stub), libSDL2_static.a, SDL's public
# headers (OUT/include/SDL2) and SDL's tests, with the GCC 16 stove.
#
# SDL 2.32.10 (Zlib) is the core. The Amiga back ends start from
# libSDL2-amigaos3 (Zlib) at the commit UPSTREAM.json pins; patches/sdl2
# adds the platform to SDL, patches/amigaos3 holds the Team's changes to
# those back ends, and src/ the Team's own files. Nothing third-party is
# committed: the tarball is checked against its sha256 and libSDL2-amigaos3
# is fetched by commit.
#
#   library/modules/sdl2/build.sh [OUT_DIR] [TARGETS]
#       OUT_DIR  default build/; TARGETS default: module stub static tests satellites
#       (satellites: SDL2_image.module and SDL2_mixer.module with their
#       link stubs, the same as static libraries, SDL2_ttf and SDL2_net, with
#       FreeType and libxmp, all pinned in UPSTREAM.json: satellites/Makefile;
#       patches/sdl2_image, patches/sdl2_mixer and patches/freetype hold the
#       Team's changes to them)
#   CPU="-m68020 -m68881"   another CPU (default -m68040 -m68881)
#
# Needs the GCC 16 stove (STOVE, default ~/AmigaChrome/stoves/os32-gcc16/prefix),
# vasmm68k_mot on PATH (or VASM=), and the CyberGraphX and AHI developer
# headers (SDK_HEADERS, default the OS 3.2 stove's include directory).
# TARBALLS is where the SDL tarball is looked for before it is downloaded.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/build}
shift 2>/dev/null || true
TARGETS=${*:-module stub static tests satellites}
TARBALLS=${TARBALLS:-$HOME/AmigaChrome-dev/upstream-tarballs}
SDK_HEADERS=${SDK_HEADERS:-$HOME/AmigaChrome/stoves/os32/prefix/m68k-amigaos/include}
if [ -z "${VASM:-}" ]; then
    VASM=$(command -v vasmm68k_mot || echo "$HOME/AmigaChrome/stoves/os32/prefix/bin/vasmm68k_mot")
fi

pin() { python3 -c "import json,sys; print(json.load(open('$HERE/UPSTREAM.json'))[sys.argv[1]][sys.argv[2]])" "$1" "$2"; }

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
WORK=$OUT/sdl2-work
mkdir -p "$WORK"

# A pinned tarball: from TARBALLS, or downloaded once into WORK/tarballs;
# either way checked against its sha256. Prints its path.
fetch() {
    f=$(pin "$1" file)
    p=$TARBALLS/$f
    if [ ! -f "$p" ]; then
        p=$WORK/tarballs/$f
        mkdir -p "$WORK/tarballs"
        [ -f "$p" ] || curl -fsL -o "$p" "$(pin "$1" url)"
    fi
    echo "$(pin "$1" sha256)  $p" | sha256sum -c - >&2
    echo "$p"
}

# 1. SDL 2.32.10.
TARBALL=$(fetch sdl2)

# 2. libSDL2-amigaos3 at its pinned commit, for the Amiga back ends.
A3=$WORK/libSDL2-amigaos3
COMMIT=$(pin amigaos3 commit)
[ -d "$A3/.git" ] || git clone -q "$(pin amigaos3 url)" "$A3"
git -C "$A3" cat-file -e "$COMMIT^{commit}" 2>/dev/null || git -C "$A3" fetch -q origin "$COMMIT"

# 3. A fresh tree: SDL; the opengpu renderer starts as a copy of SDL's
#    software one; then the back ends, the patches and the Team's files.
TREE=$WORK/$(pin sdl2 dir)
rm -rf "$TREE"
tar xzf "$TARBALL" -C "$WORK"
mkdir -p "$TREE/src/render/opengpu"
cp "$TREE/src/render/software/SDL_render_sw.c" "$TREE/src/render/opengpu/SDL_render_opengpu.c"
for d in video audio thread timer joystick filesystem; do
    git -C "$A3" archive "$COMMIT" "src/$d/amigaos3" | tar x -C "$TREE"
done
cp "$A3/LICENSE" "$TREE/LICENSE.libSDL2-amigaos3.txt"
for p in "$HERE"/patches/sdl2/*.patch "$HERE"/patches/amigaos3/*.patch; do
    patch -s -p1 -d "$TREE" < "$p"
done
cp -R "$HERE/src/." "$TREE/src/"
cp -p "$HERE/include/SDL_config_amigaos.h" "$TREE/include/"   # its time: the Makefile rebuilds on a change

# 4. The CyberGraphX and AHI headers the GCC 16 stove lacks.
SDK=$WORK/sdk-include
for f in cybergraphx/cybergraphics.h proto/cybergraphics.h inline/cybergraphics.h \
         clib/cybergraphics_protos.h devices/ahi.h proto/ahi.h inline/ahi.h clib/ahi_protos.h; do
    mkdir -p "$SDK/$(dirname "$f")"
    cp "$SDK_HEADERS/$f" "$SDK/$f"
done

# 5. SDL's public headers for programs: OUT/include/SDL2.
mkdir -p "$OUT/include/SDL2"
cp "$TREE"/include/*.h "$OUT/include/SDL2/"

SDL_TARGETS=$(echo " $TARGETS " | sed 's/ satellites / /')
if [ -n "$(echo $SDL_TARGETS)" ]; then
    make -f "$HERE/Makefile" -j"$(nproc)" SDL="$TREE" OUT="$OUT" WORK="$WORK" VASM="$VASM" ${CPU:+CPU="$CPU"} $SDL_TARGETS
fi

# 6. The satellite libraries, against the headers in OUT/include/SDL2.
case " $TARGETS " in
*" satellites "*)
    for lib in sdl2_image sdl2_mixer sdl2_ttf sdl2_net freetype libxmp; do
        t=$(fetch $lib)
        rm -rf "$WORK/$(pin $lib dir)"
        tar xf "$t" -C "$WORK"
        # the Team's changes to a satellite, when it has any (patches/freetype)
        for p in "$HERE"/patches/$lib/*.patch; do
            if [ -f "$p" ]; then patch -s -p1 -d "$WORK/$(pin $lib dir)" < "$p"; fi
        done
    done
    rm -rf "$WORK/obj-satellites"
    make -f "$HERE/satellites/Makefile" -j"$(nproc)" OUT="$OUT" WORK="$WORK" ${CPU:+CPU="$CPU"} satellites
    ;;
esac
# 7. GCC 16's FPCR clash (fpcr_check.py) must not be in anything built.
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
set --
for f in "$OUT/SDL2.module" "$OUT"/SDL2_*.module "$OUT"/libSDL2*.a; do [ -f "$f" ] && set -- "$@" "$f"; done
[ $# -eq 0 ] || python3 "$HERE/fpcr_check.py" "$STOVE/bin/m68k-amigaos-objdump" "$@"
echo "SDL 2 built in $OUT"
