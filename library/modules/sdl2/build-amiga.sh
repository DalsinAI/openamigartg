#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target
# library/modules/sdl2): SDL2.module, libSDL2.a, libSDL2_static.a and the
# tests (build.sh's module, stub, static and tests), with the stove's
# compiler. build.sh fetches the sources UPSTREAM.json pins. The satellites
# are not built here.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
# build.sh and the Makefile take the stove's prefix directory
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
if [ -z "${VASM:-}" ] && [ -x "$PREFIX/bin/vasmm68k_mot" ]; then VASM=$PREFIX/bin/vasmm68k_mot; export VASM; fi
mkdir -p "$OUT"
# The whole log goes to OUT/build.log: the Cradle keeps only the end of
# what is printed, so the end says how it went and lists every warning.
LOG=$OUT/build.log
rc=0
STOVE=$PREFIX sh "$HERE/build.sh" "$OUT" module stub static tests > "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    tail -n 200 "$LOG"
    exit "$rc"
fi
tail -n 20 "$LOG"
echo "warnings, each once (file:line, from $(grep -c 'warning:' "$LOG" || true) in all):"
grep 'warning:' "$LOG" | sed -e "s|$OUT/sdl2-work/||" -e "s|$HOME/||" | sort | uniq -c || true

# The window surface in video RAM goes round the back end's framebuffer
# functions as SDL calls them (SDL_os3vram.h). Besides SDL_os3video.o, which
# hands them to SDL, only SDL_os3window.o's DestroyWindow may call one
# itself (its own framebuffer's Destroy; DestroyWindow is wrapped too).
bad=0
for o in "$OUT"/sdl2-work/obj-module/video/amigaos3/*.o; do
    n=$(basename "$o")
    for s in $("$PREFIX/bin/m68k-amigaos-nm" -u "$o" | awk '/WindowFramebuffer$/ { print $2 }'); do
        echo "$n calls $s"
        case "$n:$s" in
        SDL_os3video.o:*) ;;
        SDL_os3window.o:*DestroyWindowFramebuffer) ;;
        *) bad=1 ;;
        esac
    done
done
[ "$bad" -eq 0 ] || { echo "a framebuffer function is called past the video RAM wrappers" >&2; exit 1; }

ls -l "$OUT/SDL2.module" "$OUT/libSDL2.a" "$OUT/libSDL2_static.a" "$OUT/libSDL2_test.a"
ls "$OUT/tests/sdl2"
sha256sum "$OUT/SDL2.module" "$OUT/libSDL2.a" "$OUT/libSDL2_static.a"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1   # last, so the log's tail has it
