#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module and libGL.a for AmigaOS 3.2, on the Mesa build that mesa/build.sh
# makes (run first, for the AmigaOS cross file), with the GCC 16 stove.
#   library/modules/gl/module/build.sh [OUT_DIR]      (default build/gl)
# OUT_DIR gets GL.module (for LIBS:OpenGPU/), libGL.a and include/ (the
# GLA headers; programs take GL's own from Mesa, $SRC/include in
# gla-link.env). GL_WORK as for mesa/build.sh. cybergraphics.library's
# headers come from the GCC 6.5 stove (STOVE), as the GCC 16 one has none.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GL=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$GL/../../.." && pwd)
COMMON=$REPO/library/modules/common
STUBS=$REPO/stubs/gl
STOVE16=${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
OUT=${1:-$REPO/build/gl}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
export PATH="$STOVE16/bin:$PATH"

(cd "$GL" && CROSS=mesa/cross/m68k-amigaos.ini sh mesa/build.sh)
B=${GL_WORK:-$GL/build}/mesa-build-m68k-amigaos
. "$B/gla-link.env"

# Only cybergraphics.library's headers, so nothing else of the older stove's is seen.
CGX=$OUT/cgx-include
mkdir -p "$CGX/proto" "$CGX/inline" "$CGX/clib"
I=$STOVE/prefix/m68k-amigaos/include
cp -r "$I/cybergraphx" "$CGX/"
cp "$I/proto/cybergraphics.h" "$CGX/proto/"
cp "$I/inline/cybergraphics.h" "$CGX/inline/"
cp "$I/clib/cybergraphics_protos.h" "$CGX/clib/"

W=$OUT/work
mkdir -p "$W"
# The calls: every GL entry point Mesa's glapi generated for this build, and GLA's.
"${CC%gcc}nm" -g --defined-only "$B/src/mesa/glapi/glapi/libglapi_bridge.a" | sed -n 's/^[0-9a-f]* T _\(gl[A-Z][A-Za-z0-9_]*\)$/\1/p' | sort -u > "$W/gl.names"
python3 "$STUBS/gen_gl.py" --stub "$W/gl_stub_calls.s" --exports "$W/gl_exports.c" "$W/gl.names" "$STUBS/gla.names"

MF="-m68040 -m68881 -mnobitfield -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -Wno-unused-parameter -I$REPO/include -I$COMMON -I$STUBS"
cd "$B"
eval "$CC $FLAGS -I$REPO/include -I$GL -I$CGX -O2 -c $GL/gla/os3/gla_present_os3.c -o gla_present_os3.o"
$CC $MF -c -o "$W/module_start.o" "$COMMON/module_start.s"
$CC $MF -c -o "$W/module_rt.o" "$COMMON/module_rt.c"
$CC $MF -c -o "$W/gl_module.o" "$HERE/gl_module.c"
$CC $MF -c -o "$W/gl_exports.o" "$W/gl_exports.c"
# module_start.o first: its jump is the module's entry. -nostartfiles: no
# libnix startup (module_rt.c stands in).
$CC $LFLAGS -nostartfiles $WEAK -o "$W/GL.module.debug" "$W/module_start.o" "$W/module_rt.o" "$W/gl_module.o" "$W/gl_exports.o" \
    $GLAOBJS gla_present_os3.o $SHIM ${FIXOBJS:-} -Wl,--start-group $LIBS $OPT -Wl,--end-group $SYSLIBS -latomic -lamiga
# No float-to-int store through the FPCR register may ship (tools/fpcr_check.py).
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$W/GL.module.debug"
"${CC%gcc}strip" -o "$OUT/GL.module" "$W/GL.module.debug"
echo "built $OUT/GL.module ($(wc -c < "$OUT/GL.module") bytes)"

# libGL.a: the generated entries and the loader.
$CC $MF -c -o "$W/gl_stub_calls.o" "$W/gl_stub_calls.s"
$CC $MF -c -o "$W/gl_stub.o" "$STUBS/gl_stub.c"
rm -f "$OUT/libGL.a"
"${CC%gcc}ar" rcs "$OUT/libGL.a" "$W/gl_stub_calls.o" "$W/gl_stub.o"
echo "built $OUT/libGL.a ($(wc -l < "$W/gl.names") GL calls, $(wc -l < "$STUBS/gla.names") GLA calls)"

# The GLA headers programs include beside GL's.
mkdir -p "$OUT/include/gla/os3"
cp "$GL/gla/gla_core.h" "$OUT/include/gla/"
cp "$GL/gla/os3/gla_present_os3.h" "$GL/gla/os3/gla_virgl_os3.h" "$OUT/include/gla/os3/"
