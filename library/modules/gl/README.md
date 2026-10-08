# OpenGPU's GL module

Mesa's OpenGL and GLES inside OpenGPU (DESIGN.md section 5, "One library"):
the GLA core, which runs Mesa's GL state tracker on one of two Gallium
drivers:

- **virgl, on the PC's graphics chip.** On AmigaChrome, opengpu.library
  carries OGPU_OP_VIRGL to the ACRTG board's ring, and the runtime hands it
  to virglrenderer (`host/virgl/acvirgl.c`). The GLA core's winsys
  (`gla/gla_virgl.c`) turns virgl's calls into ACVirgl request blocks
  (`include/opengpu/virgl.h`). It gives GL 4.3 and GLES 3.1.
- **softpipe, on the 68k.** Used on every other Amiga, and wherever
  `OGPU_Query(OGPU_OP_VIRGL)` doesn't answer "full". It gives GL 3.3 and
  GLES 3.1.

Both draw the same scenes: `tests/test_gla.c` checks softpipe against stored
pictures. It then checks virgl against softpipe's pictures, allowing a
little rounding: 99% of pixels within 8 on every channel.

## Big-endian

The 68k is big-endian and virglrenderer reads little-endian, so the module
deals with byte order in these places:

- **The command stream** goes little-endian, word by word, while byte
  strings such as shader text go as they are (patch 0008).
- **Constant buffers** are written as words (patch 0008).
- **Textures and buffers** move between Amiga memory and the host with
  their units turned round: a texture's by its format's channel or pixel
  size, a buffer's by 32 bits.
- **8- and 16-bit indices** are widened to 32 bits on the 68k (patch 0010).
- **The caps** arrive word by word in the Amiga's order, and the word of
  one-bit fields is reversed to match a big-endian compiler.

## Mesa

- **Version:** Mesa 26.2.4, pinned by SHA-256 in `mesa/UPSTREAM.json` and
  fetched at build time. It is MIT, and its notices are kept.
- **Our changes:** the patches in `mesa/patches/`.
  - 0001-0006 are the AmigaOS port. It was openamigamesa's until
    8 October 2026; it moved here with the one-library design, and
    openamigamesa keeps OpenDemos.
  - 0007-0010 are for virgl.

## Build

```
sh mesa/build.sh                                        # this machine: test_gla
CROSS=mesa/cross/m68k-amigaos.ini sh mesa/build.sh      # AmigaOS 3.2 (the os32-gcc16 stove's bin/ first on PATH)
ACVIRGL_LIB=/path/libvirglrenderer.so.1 sh tests/run.sh
```

`GL_WORK` chooses the work directory, which defaults to `build/` here (about
1 GB). Programs link the GLA core from `gla-link.env` in the build
directory (OpenDemos does).

## Measured, 8 October 2026

On a scratch copy of the AmigaChrome Showcase instance (AC090 68040, ACRTG,
virglrenderer 1.3.0 on the PC's graphics chip):

- **test_gla:** softpipe gives its stored pictures. virgl gives GL 4.3, the
  same pictures as virgl on the x86 host (c3c62778, 34e0c2fd, 1a30012c),
  and 100% of softpipe's within the allowance.
- **OpenDemos at 320x240** (openamigamesa), softpipe → virgl:
  - Boing: 4.2 → 342 fps
  - Chrome Gears: 1.5 → 421 fps
  - Copper Tunnel: 0.3 → 512 fps

Next: GL.module (loaded by OGPU_ModuleOpen) and the libGL.a link stub, so
programs no longer carry Mesa's 20 MB each.
