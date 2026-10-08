#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Mesa and the GLA core on this machine: softpipe must draw the stored
# pictures, and with ACVIRGL_LIB naming a libvirglrenderer.so.1 (virglrenderer
# 1.3.0; scripts/build_virglrenderer.sh in amigachrome builds one) virgl on this
# machine's graphics chip must draw the same scenes, within a little rounding.
# The Amiga's own run is test_gla.stripped from CROSS=mesa/cross/m68k-amigaos.ini.
set -eu
TOP=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
(cd "$TOP" && sh mesa/build.sh)
"${GL_WORK:-$TOP/build}/mesa-build-host/test_gla" "$TOP/tests/golden/gla-m1.txt"
