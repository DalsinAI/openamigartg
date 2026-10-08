#!/bin/sh
# OpenGfx premultiplied ARGB compositing reference, built for m68k and run
# under qemu-m68k against an independent pixel-at-a-time reference.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
gfx=$here/../../library/ogfx
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

for build in "-O0 -m68020" "-O2 -m68020" "-O0 -m68040" "-O2 -m68040"; do
    set -- $build
    opt=$1 cpu=$2
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -fno-lto -fno-builtin \
        -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
        -c "$gfx/ogfx_composite.c" -o "$tmp/composite.o"
    und=$("${prefix}nm" -u "$tmp/composite.o" | tr -d ' U' | tr -d '\n' || true)
    [ -z "$und" ] || { echo "FAIL $build: composite core calls $und"; fail=1; }
    if "$cc" -v 2>&1 | grep -q amigaos; then
        echo "ok   $build: object (no qemu for AmigaOS programs)"
        continue
    fi
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -static -Wall -Wextra -Werror \
        "$here/ogfx_composite_test.c" "$tmp/composite.o" -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        out=$("$qemu" -cpu m68040 "$tmp/t") || {
            echo "FAIL $build: $out" | tail -3
            fail=1
            continue
        }
        echo "ok   $build: $out"
    else
        echo "ok   $build: built (no $qemu to run it)"
    fi
done
exit $fail
