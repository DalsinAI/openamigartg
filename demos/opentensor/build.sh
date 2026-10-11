#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT=${1:-"$ROOT/build/opentensor"}
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P="$STOVE/prefix"
SYS="$P/m68k-amigaos"
OLD_STOVE=${OLD_STOVE:-"$HOME/AmigaChrome/stoves/os32"}
OLD_SYS="$OLD_STOVE/prefix/m68k-amigaos"
CC="$P/bin/m68k-amigaos-gcc"

[ -x "$CC" ] || { echo "OpenTensor: missing compiler $CC" >&2; exit 2; }
[ -f "$SYS/include/GL/gl.h" ] || { echo "OpenTensor: OpenGL headers are missing" >&2; exit 4; }

rm -rf "$OUT"
mkdir -p "$OUT/package/OpenTensor"

# The GCC16 stove has the OpenGPU SDK. Older snapshots may still carry the
# CyberGraphX and AHI developer headers, so copy only those headers into a
# private include root rather than mixing the two complete SDKs.
EXTRA_INC="$OUT/sdk-compat"
mkdir -p "$EXTRA_INC"
if [ ! -f "$SYS/include/cybergraphx/cybergraphics.h" ]; then
    [ -f "$OLD_SYS/include/cybergraphx/cybergraphics.h" ] || { echo "OpenTensor: CyberGraphX headers are missing" >&2; exit 3; }
    mkdir -p "$EXTRA_INC/cybergraphx" "$EXTRA_INC/proto" "$EXTRA_INC/inline" "$EXTRA_INC/clib"
    cp "$OLD_SYS/include/cybergraphx/cybergraphics.h" "$EXTRA_INC/cybergraphx/"
    cp "$OLD_SYS/include/proto/cybergraphics.h" "$EXTRA_INC/proto/"
    cp "$OLD_SYS/include/inline/cybergraphics.h" "$EXTRA_INC/inline/"
    cp "$OLD_SYS/include/clib/cybergraphics_protos.h" "$EXTRA_INC/clib/"
fi
if [ ! -f "$SYS/include/devices/ahi.h" ]; then
    [ -f "$OLD_SYS/include/devices/ahi.h" ] || { echo "OpenTensor: AHI development headers are missing" >&2; exit 3; }
    mkdir -p "$EXTRA_INC/devices"
    cp "$OLD_SYS/include/devices/ahi.h" "$EXTRA_INC/devices/"
fi

find_lib() {
    name=$1
    for base in "$SYS/lib" "$P/lib" "$SYS" "$P"; do
        [ -d "$base" ] || continue
        hit=$(find "$base" -type f -name "$name" -print -quit 2>/dev/null || true)
        [ -z "$hit" ] || { printf '%s\n' "$hit"; return 0; }
    done
    return 1
}

GL_LIB=$(find_lib libGL.a || true)
[ -n "$GL_LIB" ] || { echo "OpenTensor: libGL.a is missing from the GCC16 stove" >&2; exit 5; }
GL_DIR=$(dirname "$GL_LIB")

"$CC" \
    -O2 -g -m68040 -m68881 -fomit-frame-pointer \
    -fno-delete-null-pointer-checks -noixemul \
    -Wall -Wextra -Wno-unused-parameter \
    -I"$SYS/include" \
    -I"$EXTRA_INC" \
    -I"$ROOT/library/modules/gl" \
    "$HERE/src/opentensor.c" \
    -L"$GL_DIR" -lGL -lm -lamiga \
    -o "$OUT/package/OpenTensor/OpenTensor"

file "$OUT/package/OpenTensor/OpenTensor"
sha256sum "$OUT/package/OpenTensor/OpenTensor" > "$OUT/package/OpenTensor/SHA256SUMS"

cat > "$OUT/package/OpenTensor/README.txt" <<'TXT'
OpenTensor: First Light
=======================

An AmigaOS 3.x demo using Mesa/GLA/OpenGPU, OpenRTG and ahi.device.

Recommended AmigaChrome launch:

    Stack 1000000
    OpenTensor

Controls:

    Esc or either mouse button   Exit
    Space                       Pause / resume
    F1                          Diagnostic overlay

Options:

    CPU                         Force Mesa softpipe
    WINDOW                      Run in a Workbench window
    W n H n                     Resolution (default 960 x 540)
    DEPTH 16|32                 RTG depth (default 32)
    SECONDS n                   Timed run
    NOAUDIO                     No AHI soundtrack
    NOVSYNC                     Do not WaitTOF after each frame

The default private OpenRTG screen uses GLA's screen-owned OpenGPU present
path. WINDOW deliberately exercises the layered-window compatibility path.
TXT

cat > "$OUT/package/OpenTensor/BUILD-INFO.txt" <<EOF2
OpenTensor First Light 0.1.0
Target: AmigaOS 3.x / AC090 / 68040 + FPU
Compiler: $("$CC" --version | head -1)
Graphics: Mesa / GLA / OpenGPU / OpenRTG
Audio: ahi.device, generated 44.1 kHz signed 16-bit stereo
libGL: $GL_LIB
EOF2

(
    cd "$OUT/package"
    zip -qr "$OUT/OpenTensor-First-Light-0.1.0.zip" OpenTensor
)
sha256sum "$OUT/OpenTensor-First-Light-0.1.0.zip" > "$OUT/SHA256SUMS"
printf 'OPENTENSOR_PACKAGE=%s\n' "$OUT/OpenTensor-First-Light-0.1.0.zip"
cat "$OUT/SHA256SUMS"
