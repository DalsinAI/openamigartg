#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module and libGL.a for AmigaOS 3.2, on the Mesa build that mesa/build.sh
# makes (run first, for the AmigaOS cross file), with the GCC 16 stove.
# GL.module is shared: Mesa is built -fbaserel32 (the cross file), the module
# linked -resident32, so opengpu.library loads its code once and gives each
# program its own copy of the data (include/opengpu/module.h). libGL.a sets
# A4 on every call.
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
# Each call's argument size from Mesa's glapi XML and the GLA headers, for the stub's A4 entries.
python3 "$STUBS/gen_gl.py" --stub "$W/gl_stub_calls.s" --exports "$W/gl_exports.c" --xml "$SRC/src/mesa/glapi/glapi/gen" \
    --header "$GL/gla/gla_core.h" --header "$GL/gla/os3/gla_present_os3.h" --header "$GL/gla/os3/gla_virgl_os3.h" \
    "$W/gl.names" "$STUBS/gla.names"

MF="-m68040 -m68881 -mnobitfield -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -Wno-unused-parameter -I$REPO/include -I$COMMON -I$STUBS"
# The module's own objects reach their globals through A4 too.
MODF="$MF -fbaserel32"
cd "$B"
eval "$CC $FLAGS -I$REPO/include -I$GL -I$CGX -O2 -c $GL/gla/os3/gla_present_os3.c -o gla_present_os3.o"
$CC $MODF -c -o "$W/module_start.o" "$COMMON/module_start.S"
$CC $MODF -c -o "$W/module_rt.o" "$COMMON/module_rt.c"
$CC $MODF -c -o "$W/gl_module.o" "$HERE/gl_module.c"
$CC $MODF -c -o "$W/gl_exports.o" "$W/gl_exports.c"
# module_start.o first: its first code is the module's. -nostartfiles: no
# libnix startup (module_rt.c stands in). -resident32: the data-to-data
# relocations, for each program's copy.
$CC $MESA_LFLAGS -nostartfiles $WEAK -Wl,-Map,"$W/GL.module.map" -o "$W/GL.module.debug" "$W/module_start.o" "$W/module_rt.o" "$W/gl_module.o" "$W/gl_exports.o" \
    $GLAOBJS gla_present_os3.o $SHIM ${FIXOBJS:-} -Wl,--start-group $LIBS $OPT -Wl,--end-group $SYSLIBS -latomic -lamiga
# No float-to-int store through the FPCR register may ship (tools/fpcr_check.py),
# and the data is reached only through A4 (tools/baserel_check.py).
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$W/GL.module.debug"
python3 "$REPO/tools/baserel_check.py" "${CC%gcc}" "$W/GL.module.debug" "$W/GL.module.map" --allow "$HERE/baserel.allow"
# (strip refuses some -fbaserel32 links with a big data hunk; tools/hunk_strip.py doesn't.)
"${CC%gcc}strip" -o "$OUT/GL.module" "$W/GL.module.debug" 2>/dev/null || python3 "$REPO/tools/hunk_strip.py" "$W/GL.module.debug" "$OUT/GL.module"
echo "built $OUT/GL.module ($(wc -c < "$OUT/GL.module") bytes)"

# libGL.a: the generated entries and the loader (-ffixed-a4: it sets A4 for
# its calls into the module).
$CC $MF -c -o "$W/gl_stub_calls.o" "$W/gl_stub_calls.s"
$CC $MF -ffixed-a4 -c -o "$W/gl_stub.o" "$STUBS/gl_stub.c"
rm -f "$OUT/libGL.a"
"${CC%gcc}ar" rcs "$OUT/libGL.a" "$W/gl_stub_calls.o" "$W/gl_stub.o"
echo "built $OUT/libGL.a ($(wc -l < "$W/gl.names") GL calls, $(wc -l < "$STUBS/gla.names") GLA calls)"

# The GLA headers programs include beside GL's.
mkdir -p "$OUT/include/gla/os3"
cp "$GL/gla/gla_core.h" "$OUT/include/gla/"
cp "$GL/gla/os3/gla_present_os3.h" "$GL/gla/os3/gla_virgl_os3.h" "$OUT/include/gla/os3/"
