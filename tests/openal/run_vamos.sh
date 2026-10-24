#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# OpenAL.module's 68k builds on a PC, under amitools' vamos (an AmigaOS
# emulator that runs a program without an Amiga or its ROM), big-endian:
#   - test_al_api, test_al_3d and test_al_stream, the PC tests built by the
#     stove (library/modules/openal/Makefile, tests);
#   - alinfo.m through libopenal.a and OpenAL.module (in LIBS:OpenGPU/;
#     with no opengpu.library the stub LoadSegs it itself): the module's
#     first code, its A4 data, libnix's start in it, the jump table, its
#     calls and its close. vamos has no ahi.device, so no device opens;
#   - nomodule.m and alinfo.m with no module.
#
# Build with CPU="-m68020 -m68881": the 68040 build takes square roots with
# FDSQRT, which vamos (machine68k 0.3.0) doesn't emulate (sqrt(16) comes back
# 16); a real 68040 has it. vamos 0.8.1 needs machine68k 0.3.0.
#
#   CPU="-m68020 -m68881" library/modules/openal/build.sh OUT_DIR
#   tests/openal/run_vamos.sh OUT_DIR       (VAMOS: the vamos to run)
# Exit 0 when every test passes.
set -eu
OUT=${1:?the OUT_DIR of a build with CPU="-m68020 -m68881"}
VAMOS=${VAMOS:-vamos}
OUT=$(CDPATH= cd -- "$OUT" && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/libs/OpenGPU" "$W/none/OpenGPU"
cp "$OUT/OpenAL.module" "$W/libs/OpenGPU/"
cp "$OUT"/tests/openal/* "$W/"
cd "$W"
run() {
    $VAMOS -C 68040 -V "Work:$W" "$@"
}
for t in test_al_api test_al_3d test_al_stream; do
    run "$t"
done
run -a LIBS:Work:libs alinfo.m
run -a LIBS:Work:none nomodule.m > out.txt
grep -q "^OpenAL: this program's sound needs LIBS:OpenGPU/OpenAL.module" out.txt
grep -q "^nomodule: no sound device" out.txt
set +e
run -a LIBS:Work:none alinfo.m > /dev/null
rc=$?
set -e
[ "$rc" -eq 10 ]
echo "OpenAL on vamos: all passed"
