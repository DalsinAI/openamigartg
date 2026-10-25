#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The core against itself at an earlier git ref, byte for byte (tests/vector_same.c).
#   tests/vector_same.sh [REF [ROUNDS [SEED]]]     (REF: origin/main by default)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
REF=${1:-origin/main}
mkdir -p "$OUT/same"
git -C "$HERE/.." show "$REF:library/opengpu/ogpu_core.c" > "$OUT/same/ogpu_core.c"
cp "$HERE/../library/opengpu/ogpu_core.h" "$OUT/same/ogpu_core.h"
CF="-std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$HERE/../library/opengpu"
cc $CF -Dogpu_core_init=old_ogpu_core_init -Dogpu_core_run=old_ogpu_core_run -Dogpu_core_supports=old_ogpu_core_supports \
    -I"$HERE/../include" -include "$HERE/../include/opengpu/stream.h" -c -o "$OUT/same/old_core.o" "$OUT/same/ogpu_core.c"
cc $CF -o "$OUT/vector_same" "$HERE/vector_same.c" "$OUT/same/old_core.o" "$HERE/../library/opengpu/ogpu_core.c" \
    "$HERE/../library/opengpu/ogpu_build.c"
"$OUT/vector_same" "${2:-3000}" "${3:-1}"
