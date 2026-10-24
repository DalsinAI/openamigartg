#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTG's host tests, and a check that the library's C builds for the 68k.
#   tests/run.sh      (STOVE or CC68K as in tools/build.sh; the 68k check is skipped without it)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
mkdir -p "$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/test_modes" "$HERE/test_modes.c" "$HERE/../library/modes.c"
"$OUT/test_modes"
# With no size asked for, the mode table is exactly 0.16's (0.17: the Native modes come only with one).
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/modes_golden" "$HERE/modes_golden.c" "$HERE/../library/modes.c"
"$OUT/modes_golden" "$HERE/golden/modes-table.txt"
OGPU="$HERE/../library/opengpu"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_opengpu" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_opengpu" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
# COMPOSITE_PD (v1.4, pixman.library's composites) against its per-pixel reference.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_composite_pd" "$HERE/test_composite_pd.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_composite_pd"
# Stream v1.6 (paths, strokes, gradients, patterns, blur, blend modes) against its references.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_vector" "$HERE/test_vector.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" -lm
"$OUT/test_vector"
# Stream v1.7 (SDL 3's blend modes, PIXELART, clamping one way, PALETTE, RGBA32 and ABGR32,
# P010 and the new YUV spaces; the library's variant check) against its references, with
# the 3D rasteriser for TRIANGLES' SDL form.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -DOGPU_WITH_3D \
    -o "$OUT/test_sdl3" "$HERE/test_sdl3.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lm
"$OUT/test_sdl3"
# The Vulkan back end against the core, when Vulkan's loader and headers are there
# (it skips itself when no device is found; lavapipe will do). Built with the 3D
# range (OGPU_WITH_3D), as the runtime is: TRIANGLES on the 3D shader, the rest
# of the range on the back end's core; and once without it, where the back end
# must answer no 3D.
if [ -e /usr/include/vulkan/vulkan.h ] && { ldconfig -p 2>/dev/null | grep -q libvulkan.so.1; }; then
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -DOGPU_WITH_3D \
        -o "$OUT/test_opengpu_vk" "$HERE/test_opengpu_vk.c" "$HERE/golden_scene_3d.c" "$HERE/scenes3d.c" "$HERE/../host/vulkan/ogpu_vk.c" \
        "$OGPU/ogpu_core.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lvulkan -lm
    ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 3000 "$HERE/golden/opengpu-v11.txt" \
        "$HERE/golden/opengpu-3d.txt"
    # Again in video RAM the test owns, as the runtime's board memory is.
    OGPU_VK_HOST=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 500 "$HERE/golden/opengpu-v11.txt" \
        "$HERE/golden/opengpu-3d.txt"
    # And mapped with a card part (the card's own memory for its second half),
    # where the device can (sparse binding); else it says so and carries on.
    OGPU_VK_HOST=1 OGPU_VK_HOSTMEM=map OGPU_VK_CARD_AT=0x80000 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 200 \
        "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-3d.txt"
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -o "$OUT/test_opengpu_vk-no3d" "$HERE/test_opengpu_vk.c" "$HERE/../host/vulkan/ogpu_vk.c" \
        "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" -lvulkan
    "$OUT/test_opengpu_vk-no3d" "$HERE/golden/opengpu-g1.txt" 200 "$HERE/golden/opengpu-v11.txt"
else
    echo "no Vulkan headers or loader; the GPU run is skipped"
fi
# ACVirgl's host side against virglrenderer, when ACVIRGL_LIB names a libvirglrenderer.so.1
# and VIRGL_SRC its source's src/ (for virgl_protocol.h): the release amigachrome's
# scripts/build_virglrenderer.sh builds (1.3.0).
if [ -n "${ACVIRGL_LIB:-}" ] && [ -f "${VIRGL_SRC:-/nonexistent}/virgl_protocol.h" ]; then
    cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$VIRGL_SRC" -o "$OUT/test_acvirgl" "$HERE/test_acvirgl.c" \
        "$HERE/../host/virgl/acvirgl.c" -ldl
    "$OUT/test_acvirgl"
    # again on GLES, as a Pi's V3D may give it (skipped without libGLESv2)
    AC_VIRGL_GLES=1 "$OUT/test_acvirgl"
else
    echo "no ACVIRGL_LIB and VIRGL_SRC; the virgl run is skipped"
fi
# libSDL3.a's stub with no SDL3.module (tests/sdl3/stub_nomodule.c), when SDL3_SRC
# names SDL 3.4.14's tree (the tarball, or library/modules/sdl3/build.sh's).
if [ -f "${SDL3_SRC:-/nonexistent}/src/dynapi/SDL_dynapi_procs.h" ]; then
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -I"$HERE/sdl3/host" -I"$HERE/host-include" -I"$HERE/../include" -I"$HERE/../stubs/sdl3" \
        -I"$SDL3_SRC/include" -I"$SDL3_SRC/src/dynapi" -I"$SDL3_SRC/src" \
        -o "$OUT/stub_nomodule" "$HERE/sdl3/stub_nomodule.c"
    "$OUT/stub_nomodule"
else
    echo "no SDL3_SRC; the SDL 3 stub test is skipped"
fi
# The same tests big-endian on a 68040, when a Linux m68k compiler and qemu are installed.
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror \
        -o "$OUT/test_opengpu-m68k" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
    qemu-m68k -cpu m68040 "$OUT/test_opengpu-m68k" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
else
    echo "no m68k-linux-gnu-gcc or qemu-m68k; the big-endian run is skipped"
fi
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    "$CC68K" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c -o "$OUT/modes-m68k.o" "$HERE/../library/modes.c"
    "$CC68K" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -fno-builtin -c -o "$OUT/ogpu_core-m68k.o" "$HERE/../library/opengpu/ogpu_core.c"
    echo "library/modes.c and library/opengpu/ogpu_core.c build for the 68k"
else
    echo "no m68k compiler; the 68k check is skipped"
fi
# OpenGfx (library/ogfx): its leaves, the compositing reference, the patch entry.
sh "$HERE/ogfx/run.sh"
# The module loader's test (Test.module and ModuleCheck, run on an Amiga), when both stoves are there.
if [ -x "${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] && [ -x "$CC68K" ]; then
    sh "$HERE/modules/build.sh" "$OUT/modules" >/dev/null && echo "tests/modules build (ModuleCheck runs on an Amiga)"
fi
