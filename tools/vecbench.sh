#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OGPUVecBench (tools/ogpu_vecbench.c), with the os32-gcc16 stove: the core at
# each git ref given, built as the library builds it and linked side by side,
# the first the one the others' pixels are compared with.
#   tools/vecbench.sh OUT_DIR NAME=REF [NAME=REF ...]      (REF . : the working tree)
# e.g. tools/vecbench.sh build/vecbench main=origin/main new=.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
OUT=$1
shift
mkdir -p "$OUT"
# library/build.sh's flags for ogpu_core.c; GCC 16 also keeps late-combine off
CF="-m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter"
case $("$CC" --version | head -1) in *" 16."*) CF="$CF -fno-late-combine-instructions" ;; esac
: > "$OUT/vb_cores.h"
objs=
for v in "$@"; do
    name=${v%%=*} ref=${v#*=}
    rm -rf "$OUT/$name"
    mkdir -p "$OUT/$name"
    if [ "$ref" = . ]; then
        (cd "$ROOT" && tar -cf - library/opengpu include) | tar -xf - -C "$OUT/$name"
    else
        git -C "$ROOT" archive "$ref" library/opengpu include | tar -xf - -C "$OUT/$name"
    fi
    # shellcheck disable=SC2086
    "$CC" $CF -Dogpu_core_init=vb_${name}_init -Dogpu_core_run=vb_${name}_run -Dogpu_core_supports=vb_${name}_supports \
        -c -o "$OUT/$name/ogpu_core.o" "$OUT/$name/library/opengpu/ogpu_core.c"
    echo "VB_CORE(\"$name\", vb_${name}_init, vb_${name}_run)" >> "$OUT/vb_cores.h"
    objs="$objs $OUT/$name/ogpu_core.o"
    if [ "$ref" = . ]; then echo "$name: the working tree"; else echo "$name: $(git -C "$ROOT" rev-parse --short "$ref")"; fi
done
# the bench itself uses the FPU for its scenes' sizes (FPCR reset at start)
# shellcheck disable=SC2086
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul -I"$OUT" -I"$ROOT/include" \
    -o "$OUT/OGPUVecBench" "$HERE/ogpu_vecbench.c" "$ROOT/library/opengpu/ogpu_build.c" $objs -lm
ls -l "$OUT/OGPUVecBench"
