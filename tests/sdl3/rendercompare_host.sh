#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# tests/sdl3/rendercompare.c on a Linux PC: SDL 3.4.14 built for the PC (its
# dummy video driver), with SDL3.module's opengpu renderer patched in as
# build.sh patches it, and OpenGPU's own core as opengpu.library
# (tests/sdl3/render_host/host.c). The renderer, the core and the test are
# built with AddressSanitizer and UBSan. It checks the renderer's logic on
# a fast machine; the Amiga's build and its screen are checked by
# rendercompare.m in a lab.
#
#   SDL3_SRC=<SDL 3.4.14's tree> tests/sdl3/rendercompare_host.sh [OUT_DIR]
#
# Needs cmake and a C compiler. Exit 0 when every scene matches and no batch
# met an error. RENDERCOMPARE_DUMP=<prefix> keeps each scene's pictures.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
SDL3_SRC=${SDL3_SRC:?set SDL3_SRC to SDL 3.4.14 (the release tarball, or its git tree)}
OUT=${1:-$ROOT/build/tests/sdl3-host}
MOD=$ROOT/library/modules/sdl3
OGPU=$ROOT/library/opengpu
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)

# 1. SDL's tree with the renderer, as build.sh makes it (patches/sdl3/0004)
TREE=$OUT/SDL3
rm -rf "$TREE"
mkdir -p "$TREE"
(cd "$SDL3_SRC" && tar cf - --exclude=.git .) | (cd "$TREE" && tar xf -)
mkdir -p "$TREE/src/render/opengpu"
cp "$TREE/src/render/software/SDL_render_sw.c" "$TREE/src/render/opengpu/SDL_render_opengpu.c"
patch -s -p1 -d "$TREE" < "$MOD/patches/sdl3/0004-opengpu-renderer.patch"
mkdir -p "$TREE/src/video/amigaos3"
cp "$MOD/src/video/amigaos3/SDL_os3gpu.h" "$MOD/src/video/amigaos3/SDL_os3gpu.c" "$TREE/src/video/amigaos3/"

# 2. SDL for the PC: a static library, video only. SDL's CMake builds every
#    renderer in src/render, the opengpu one with it, so the harness's
#    headers (render_host/include) and OpenGPU's are on its path. All of SDL
#    is built with the sanitizers; UBSan carries on and reports.
SAN="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
INC="-I$HERE/render_host/include -I$HERE/../host-include -I$ROOT/include"
cmake -S "$TREE" -B "$OUT/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_C_FLAGS="-DSDL_VIDEO_RENDER_OPENGPU=1 -DSDL_OS3_GPU_NO_PRESENT $INC $SAN" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF \
    -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_GPU=OFF -DSDL_RENDER_GPU=OFF -DSDL_RENDER_VULKAN=OFF \
    -DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_CAMERA=OFF \
    -DSDL_DIALOG=OFF -DSDL_POWER=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_TRAY=OFF >"$OUT/cmake.log"
cmake --build "$OUT/build" >"$OUT/build.log"
LIB=$(find "$OUT/build" -maxdepth 2 -name 'libSDL3.a' | head -1)

# 3. SDL_os3gpu.c's library part and the harness, with the flags SDL's own
#    render code was built with; the core and the test
FLAGS=$(python3 - "$OUT/build/compile_commands.json" <<'EOF'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith("src/render/SDL_render.c"):
        args = shlex.split(e["command"])[1:]
        keep, skip = [], False
        for a in args:
            if skip: skip = False; continue
            if a in ("-o", "-c", "-MF", "-MT", "-MQ"): skip = True; continue
            if a.endswith(".c") or a in ("-MD", "-MMD"): continue
            if a.startswith("-W") or a.startswith("-O") or a == "-include" or a.endswith("cmake_pch.h"): continue
            keep.append(a)
        print(" ".join(shlex.quote(a) for a in keep))
        break
EOF
)
OBJ=$OUT/obj
mkdir -p "$OBJ"
eval "command cc $FLAGS -Wall -Werror -c \"$TREE/src/video/amigaos3/SDL_os3gpu.c\" -o \"$OBJ/os3gpu.o\""
eval "command cc $FLAGS -Wall -Werror -c \"$HERE/render_host/host.c\" -o \"$OBJ/host.o\""
for f in ogpu_core ogpu_3d ogpu_build ogpu_build3d; do
    command cc -std=c99 $SAN -DOGPU_WITH_3D -c "$OGPU/$f.c" -o "$OBJ/$f.o"
done
command cc -std=gnu11 $SAN -Wall -Werror -I"$TREE/include" -c "$HERE/rendercompare.c" -o "$OBJ/rendercompare.o"
command cc $SAN -o "$OUT/rendercompare" "$OBJ/rendercompare.o" "$OBJ/os3gpu.o" "$OBJ/host.o" \
    "$OBJ/ogpu_core.o" "$OBJ/ogpu_3d.o" "$OBJ/ogpu_build.o" "$OBJ/ogpu_build3d.o" "$LIB" -lm -lpthread

# 4. Run it: with a library of stream v1.7, then as opengpu.library 0.18
#    (v1.6), where what v1.7 brought must take the CPU's path or SDL's
#    conversion, and no command may carry v1.7
fail=0
for lib in 0.19 0.18; do
    log=$OUT/rendercompare-$lib.txt
    set +e
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 SDL_VIDEO_DRIVER=dummy SDL_OPENGPU_STATS=1 \
        OPENGPU_HOST_LIBRARY=$lib "$OUT/rendercompare" >"$log" 2>&1
    rc=$?
    set -e
    echo "== as opengpu.library $lib"
    cat "$log"
    if [ $rc -ne 0 ] || ! grep -q ", 0 with an error, 0 addresses outside" "$log" || grep -q "runtime error" "$log"; then
        fail=1
    fi
    if [ $lib = 0.18 ] && ! grep -q ": 0 commands a 0.18 library would have refused" "$log"; then
        fail=1
    fi
done
[ $fail -eq 0 ] && echo "rendercompare_host: PASS" || { echo "rendercompare_host: FAIL"; exit 1; }
