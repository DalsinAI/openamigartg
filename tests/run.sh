#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTG's host tests, and a check that the library's C builds for the 68k.
#   tests/run.sh      (STOVE or CC68K as in tools/build.sh; the 68k check is skipped without it)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
mkdir -p "$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/test_modes" "$HERE/test_modes.c" "$HERE/../library/modes.c"
"$OUT/test_modes"
OGPU="$HERE/../library/opengpu"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_opengpu" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_opengpu" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
# The Vulkan back end against the core, when Vulkan's loader and headers are there
# (it skips itself when no device is found; lavapipe will do).
if [ -e /usr/include/vulkan/vulkan.h ] && { ldconfig -p 2>/dev/null | grep -q libvulkan.so.1; }; then
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -o "$OUT/test_opengpu_vk" "$HERE/test_opengpu_vk.c" "$HERE/../host/vulkan/ogpu_vk.c" \
        "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" -lvulkan
    ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 3000 "$HERE/golden/opengpu-v11.txt"
    # Again in video RAM the test owns, as the runtime's board memory is.
    OGPU_VK_HOST=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 500 "$HERE/golden/opengpu-v11.txt"
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
else
    echo "no ACVIRGL_LIB and VIRGL_SRC; the virgl run is skipped"
fi
# The same tests big-endian on a 68040, when a Linux m68k compiler and qemu are installed.
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then
    m68k-linux-gnu-gcc -std=c99 -O2 -m68040 -static -Wall -Wextra -Werror \
        -o "$OUT/test_opengpu-m68k" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
    qemu-m68k -cpu m68040 "$OUT/test_opengpu-m68k" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
else
    echo "no m68k-linux-gnu-gcc or qemu-m68k; the big-endian run is skipped"
fi
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    "$CC68K" -m68020 -O2 -Wall -Wextra -Werror -c -o "$OUT/modes-m68k.o" "$HERE/../library/modes.c"
    "$CC68K" -m68020 -O2 -Wall -Wextra -Werror -fno-builtin -c -o "$OUT/ogpu_core-m68k.o" "$HERE/../library/opengpu/ogpu_core.c"
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
