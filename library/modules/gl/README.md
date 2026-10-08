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
- **The Team's changes:** the patches in `mesa/patches/`.
  - 0001-0006 are the AmigaOS port. It was openamigamesa's until
    8 October 2026; it moved here with the one-library design, and
    openamigamesa keeps OpenDemos.
  - 0007-0010 are for virgl.

## GL.module and libGL.a

On AmigaOS 3.2 programs don't carry Mesa. They link `libGL.a` (`stubs/gl`),
and their first GL call loads `LIBS:OpenGPU/GL.module` through
opengpu.library's `OGPU_ModuleOpen` (`include/opengpu/module.h`):

- **GL.module** (`module/`) is Mesa, the GLA core and its OS 3 parts, linked
  as a module on `library/modules/common`. Its table hands out every call by
  name: the 1,300 GL and GLES entry points Mesa's glapi generated for this
  build, and the 15 GLA calls (`stubs/gl/gla.names`). It is about 20 MB and
  opens in about a second on the AC090.
- **libGL.a** has one entry per call, generated with the module's table from
  the same list (`stubs/gl/gen_gl.py`). The first call loads the module and
  binds the whole table by name in one pass. A call the installed module
  doesn't have returns 0.
- **Shared** (residency step 2, DESIGN.md section 5): Mesa is built
  `-fbaserel32` (the cross file) and GL.module linked `-resident32`, so
  opengpu.library loads its code once for every program and each program's
  open gets its own copy of Mesa's data (about 200 KB with the BSS). An
  entry of libGL.a sets A4 to the program's copy: it copies its arguments
  (their size comes from Mesa's glapi XML, the GLA calls' from their
  headers), calls, and gives the program its A4 back.
  `gla_get_proc_address` answers with libGL.a's own entries, so calls
  through what it gives set A4 too.
- Each program has a virgl context of its own: the context id and the
  resource handles are the addresses of the program's own winsys and
  resources (`gla/gla_virgl.c`). They were 1 and a count from 1 for every
  program, so two GL programs on virgl at once shared one context, and
  their resources clashed.
- `tools/baserel_check.py` checks GL.module reaches its data only through
  A4; `module/baserel.allow` lists the reviewed exceptions. Mesa is built
  without C++ exceptions (nothing in it catches one), and it starts no
  threads here; a shared build refuses `pthread_create` (posix_shim.c), as
  the stove's libpthread would start a thread without the program's A4.
- GL runs on the program's stack: give `main` a big one (OpenDemos asks
  libnix for 1 MB).

## GCC 16's FPCR clash

At `-m68040`, GCC 16 sometimes saves FPCR around a float-to-int store in
the register the store indexes with, so the value lands in the wrong place
(`tools/fpcr_check.py`). `mesa/fpcr_fix.py` scans Mesa's objects after
each build and builds any that have one again at `-m68020 -m68881` (which
converts with `fintrz`), linked ahead of the libraries. On 8 October 2026
that was one of 833: `feedback.c`'s `update_hit_record`. `module/build.sh`
checks GL.module itself before it ships.

## Build

```
sh mesa/build.sh                                        # this machine: test_gla
CROSS=mesa/cross/m68k-amigaos.ini sh mesa/build.sh      # AmigaOS 3.2 (the os32-gcc16 stove's bin/ first on PATH)
sh module/build.sh [OUT_DIR]                            # AmigaOS 3.2: GL.module, libGL.a, include/ (default build/gl)
ACVIRGL_LIB=/path/libvirglrenderer.so.1 sh tests/run.sh
```

`GL_WORK` chooses the work directory, which defaults to `build/` here (about
1 GB). A program for OS 3.2 compiles with Mesa's `include/` and
`OUT_DIR/include` and links `-L OUT_DIR -lGL`; OpenDemos (openamigamesa)
does.

## Measured, 8 October 2026

On a scratch copy of the AmigaChrome Showcase instance (AC090 68040, ACRTG,
virglrenderer 1.3.0 on the PC's graphics chip):

- **test_gla:** softpipe gives its stored pictures. virgl gives GL 4.3, the
  same pictures as virgl on the x86 host (c3c62778, 34e0c2fd, 1a30012c),
  and 100% of softpipe's within the allowance.
- **OpenDemos at 320x240** (openamigamesa), linked with libGL.a (100 KB
  rather than 20 MB), GL from GL.module, softpipe → virgl:
  - Boing: 4.0 → 357 fps
  - Chrome Gears: 1.5 → 427 fps
  - Copper Tunnel: 0.3 → 738 fps

  A second run gave 442, 556 and 1,205 fps on virgl (the PC's load
  varies) and the same on softpipe. Mesa linked into the program gave
  4.2 → 342, 1.5 → 421, 0.3 → 512: the module costs nothing per call.
- **ModuleCheck** (`tests/modules`) opens GL.module in 1,020 ms and finds
  its 1,315 calls.

## Speed on the 68k

Softpipe's rates above (4.0, 1.5 and 0.3 fps) were below the 5.8, 2.6 and
0.9 fps of 6 October. The Team measured why on a scratch copy of the
Showcase instance, at 320x240. Each figure is the steady rate after the
first 10 seconds (start-up, display lists and shaders are left out) over
30 to 70 seconds. GL.module was built with each set of flags, and the
6 October build of Mesa was relinked into a static OpenDemos.

| Build | Runtime of 8 Oct | JIT of amigachrome #293 |
| --- | --- | --- |
| 6 October (`-m68040`, bit fields) | 6.5, 2.4, 0.24 | 16.1, 7.1, 0.41 |
| `-m68040 -mnobitfield` (until now) | 4.1, 1.6, 0.33 | 16.3 to 17.8, 7.2 to 7.6, 0.44 |
| `-m68040`, bit fields (now) | 6.3, 2.7, 0.24 | 17.9, 7.5, 0.45 |
| `-m68020 -m68881 -mnobitfield` | | 6.9, 3.5, 0.41 |
| `-m68020 -m68881`, bit fields | | 7.2, 3.6, 0.41 |
| `-m68020 -m68881 -mnobitfield`, `-O2` | | 8.6, 3.5, 0.36 |

Boing, Chrome Gears and Copper Tunnel, in fps.

- **`-mnobitfield`** cost Boing and Chrome Gears a third of their rate on
  the runtime of 8 October, so it is dropped. On the #293 JIT, which
  translates bit-field instructions, it makes no difference. The pictures
  are unchanged (test_gla's b6a1c876, a670987e and 209db179).
- **`-m68020 -m68881`** halves the rate, so `-m68040` stays.
- **`-O2`** is no faster than meson's release `-O3` overall.
- **The FPCR fix** rebuilds one object, `feedback.c`, which the demos don't
  use, so it plays no part.
- **So the drop came from `-mnobitfield`.** It arrived with the GL module.
  The steady rates of that build on that runtime (4.1 and 1.6) match the
  figures above. Copper Tunnel's 0.9 of 6 October was read from a
  one-second window title, and no build here reaches it: the steady rate is
  0.24 to 0.45, with the 6 October build too.

## Measured, residency step 2 (8 October 2026)

On a scratch copy of the Showcase instance (AC090 68040, ACRTG, virgl):

- **ModuleCheck:** the first open of GL.module takes 19,236 KB and about
  860 ms; a second and third open, with the first still open, take 345 KB
  and 1 ms each. Closing them all gives every byte back.
- **Three OpenDemos Gears at once**, memory in use over idle: on softpipe
  29.5, 39.6 and 49.8 MB for one, two and three (a copy each, before: 30.1,
  50.8 and 71.4 MB); on virgl 25.2, 30.9 and 36.6 MB. All of it comes back
  when they end.
- **Frame rates** at 320x240, softpipe → virgl, as before within the PC's
  variation: Boing 14.8 → 294 fps, Gears 5.9 → 340, Tunnel 0.4 → 581 (a
  copy each: 14.2 → 329, 5.9 → 330, 0.4 → 579). Two at once on virgl, Gears
  and Tunnel: 192 and 331 fps.
- **A call through libGL.a** (CallCost, `tests/modules/call_cost.c`,
  glGetError with no context): 1.3 to 1.6 µs, as before (1.5); Mesa's own
  work is most of it.

