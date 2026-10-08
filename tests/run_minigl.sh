#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library's PC tests: its files built 32-bit (MGL_HOST) under
# AddressSanitizer and UBSan with a drawing area in memory, the calls going
# through the table as a program's do; and the golden scene.
#   tests/run_minigl.sh [-update]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
OUT=${OUT:-$ROOT/build/tests}
M="$ROOT/library/minigl"
O="$ROOT/library/opengpu"
mkdir -p "$OUT"
cc -m32 -std=gnu99 -DMGL_HOST -I"$HERE/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_minigl" "$HERE/test_minigl.c" \
    "$M/mgl_math.c" "$M/mgl_state.c" "$M/mgl_matrix.c" "$M/mgl_vertex.c" "$M/mgl_pipe.c" "$M/mgl_texture.c" \
    "$M/mgl_out.c" "$M/mgl_list.c" "$M/mgl_glu.c" "$M/mgl_glut.c" "$M/mgl_table.c" "$M/mgl_static.c" \
    "$O/ogpu_core.c" "$O/ogpu_3d.c" "$O/ogpu_build.c" "$O/ogpu_build3d.c" -lm
"$OUT/test_minigl" "$HERE/golden/minigl-scene.txt" ${1:-} -o "$OUT/minigl-scene.ppm"
