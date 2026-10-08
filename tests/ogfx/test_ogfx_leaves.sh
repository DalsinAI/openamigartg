#!/bin/sh
# OpenGfx's leaves (library/ogfx/ogfx_leaves.c), built by an m68k cross
# GCC as a library of ours would build them (-fno-lto, -fno-builtin,
# -fno-tree-loop-distribute-patterns) at -O0 and -O2 for the 68020 and the
# 68040 and -O2 for the 68000 (run on a 68040, as the C library needs): each tagged entry the 12-byte tag with its id
# and then JMP to its C body, no call out of the object; then
# ogfx_leaves_test.c under qemu-m68k against the pixel-at-a-time reference.
# With bebbo's m68k-amigaos-gcc as $M68K_CC (m68k-amigaos- as $M68K_PREFIX)
# only the objects and tags are checked: qemu-m68k runs Linux programs.
#   sh tests/ogfx/test_ogfx_leaves.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
gfx=$here/../../library/ogfx
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
ulp=$(echo __USER_LABEL_PREFIX__ | "$cc" -E -P -x c - | tr -d ' \n')
fail=0
for build in "-O0 -m68020" "-O2 -m68020" "-O0 -m68040" "-O2 -m68040" "-O2 -m68000"; do
    set -- $build
    opt=$1 cpu=$2
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -fno-delete-null-pointer-checks -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
        -c "$gfx/ogfx_leaves.c" -o "$tmp/leaves.o"
    und=$("${prefix}nm" -u "$tmp/leaves.o" | tr -d ' U' | grep -v '^__mulsi3$' | tr -d '\n' || true)   # a 68000 has no 32-bit multiply: libgcc's, which -lgcc links
    [ -z "$und" ] || { echo "FAIL $build: the leaves call $und"; fail=1; }
    for f in "ogfx_planar_rect 00010100" "ogfx_chunky_rect 00010101" "ogfx_chunky_copy 00010102"; do
        set -- $f
        # the tag: BRA.S *+12, 'ACMF', version 1, the id, then JMP
        addr=$("${prefix}nm" "$tmp/leaves.o" | awk -v s="${ulp}$1" '$3 == s { print $1 }')
        [ -n "$addr" ] || { echo "FAIL $build: no $1"; fail=1; continue; }
        bytes=$("${prefix}objdump" -s -j .text --start-address=0x$addr --stop-address=$(printf '0x%x' $((0x$addr + 14))) "$tmp/leaves.o" | awk '/^ [0-9a-f]+ /{ printf "%s%s%s%s", $2, $3, $4, $5 }' | cut -c1-28)
        [ "$bytes" = "600a41434d460001${2}4ef9" ] || { echo "FAIL $build: $1's tag is $bytes"; fail=1; }
    done
    if "$cc" -v 2>&1 | grep -q amigaos; then echo "ok   $build: objects and tags (no qemu for AmigaOS programs)"; continue; fi
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -fno-delete-null-pointer-checks -static -Wall -Wextra "$here/ogfx_leaves_test.c" "$tmp/leaves.o" -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        out=$("$qemu" -cpu m68040 "$tmp/t") || { echo "FAIL $build: $out" | tail -3; fail=1; continue; }   # the C library is 68020 code: every build runs on a 68040
        echo "ok   $build: $out"
    else
        echo "ok   $build: built (no $qemu to run it)"
    fi
done
exit $fail
