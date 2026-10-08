# AutoDocs-OpenGPU: building programs on OpenGPU

The reference for the OpenGPU developer kit (`OpenGPU-SDK-0.6`): how a
program reaches OpenGPU, how it links, what each part gives, and the
settings it reads. The kit's `README` says how to install it. The design is
openamigartg's `DESIGN.md`, section 5.

## 1. The parts

OpenGPU is one library and the modules it loads:

| Part | File | What it is |
| --- | --- | --- |
| opengpu.library 0.6 | `LIBS:opengpu.library` | the command stream (`include/opengpu/`), its back ends (the CPU, the GPU through AC090), OpenGfx's drawing, and the module loader |
| SDL2.module 3 | `LIBS:OpenGPU/SDL2.module` | SDL 2.32.10 with the Amiga back ends |
| GL.module | `LIBS:OpenGPU/GL.module` | Mesa 26.2.4's GL and GLES, and GLA |
| minigl.library 29 | `LIBS:minigl.library` | MiniGL (OpenGL 1.1) on OpenGPU's 3D |
| Warp3D.library | `LIBS:Warp3D.library` | Warp3D on OpenGPU's 3D |

A program links a small library from the kit and opens the rest at run
time:

| The program uses | It links | It needs at run time |
| --- | --- | --- |
| SDL 2 | `-lSDL2` (and `-lGL` when it calls `SDL_GL_`) | opengpu.library, SDL2.module (and GL.module) |
| SDL_image, SDL_mixer, SDL_ttf, SDL_net | `-lSDL2_image` and the others, before `-lSDL2` | as SDL 2 (SDL_net: bsdsocket.library) |
| OpenGL, GLES, GLA | `-lGL` | opengpu.library, GL.module |
| MiniGL | `-lminigl` (stubs) or `-lmgl` (GLUT-style helpers) | minigl.library |
| Warp3D | the `proto/Warp3D.h` calls | Warp3D.library |
| opengpu.library | the `proto/opengpu.h` calls | opengpu.library |

## 2. Modules and stubs

`libSDL2.a` and `libGL.a` are stubs: they hold no SDL and no Mesa.

- **Loading.** The program's first SDL (or GL) call opens opengpu.library
  and calls `OGPU_ModuleOpen("SDL2", 2, &table)` (or `"GL"`). The library
  loads `LIBS:OpenGPU/SDL2.module` once and returns its table. On an
  opengpu.library older than 0.5 the stub `LoadSeg`s the module itself.
- **The table.** SDL2.module's table is SDL's own dynamic API
  (`SDL_dynapi_procs.h`): the stub's every `SDL_` function jumps through it.
  The order is SDL's, so a program built today runs on every later module.
  GL.module's table names each call; `libGL.a` binds them all by name at the
  first call, and a call the module lacks returns 0.
- **Versions.** A stub asks for the oldest module version it can use (SDL:
  2). SDL2.module 3 adds `set_gl` (section 5); a program on SDL2.module 2
  runs without OpenGL and `SDL_GL_LoadLibrary` says why.
- **Missing parts.** With no module, the stub prints which file is missing
  and ends the program with 20.
- **Closing.** A destructor in the stub closes the module when the program
  ends (`SDL_Quit` first).

## 3. Residency: code once, data per program

Modules are built `-fbaserel32`. opengpu.library loads a module's code once
for every program and gives each program's open its own copy of the
module's data, reached through A4.

- Every stub call sets A4 to the program's copy and gives the program its A4
  back. The program itself needs no special build.
- SDL's calls into the program (the audio callback, timers, thread
  functions, event filters and watchers) run with the program's A4.
- Other callbacks (the log output function, hint callbacks, an RWops of the
  program's) arrive with the module's A4. That only matters to a program
  built `-fbaserel` itself; it marks them `__saveds`, as Amiga callbacks
  always need.
- Measured on the AC090, 8 October 2026: the first open of SDL2.module takes
  about 1 MB and each further program 171 KB; GL.module 19 MB, then 345 KB a
  program. A call through `libSDL2.a` costs 0.021 µs.

## 4. Linking

```
m68k-amigaos-gcc -O2 game.c -o Game $(sdl2-config --cflags --libs)
m68k-amigaos-gcc -O2 gl.c -o GLProgram $(pkg-config --cflags --libs gl)
```

- **`sdl2-config`**: `--cflags` is `-I<kit>/include/SDL2 -noixemul -m68040
  -m68881`, and with GCC 16 also `-fno-tree-loop-distribute-patterns`.
  `--libs` is `-noixemul -m68040 -m68881 -L<kit>/lib -lSDL2 -lm`, and
  `--libs --gl` adds `-lGL` after `-lSDL2`, for programs that call `SDL_GL_`.
  `SDL2_CPU` replaces the CPU flags and `SDL2_RUNTIME` the `-noixemul`.
- **libnix.** The libraries are built for libnix (`-noixemul`). GCC 6.5's
  default is ixemul, so the flag matters there. `-mcrt=nix20` is the same
  libnix, but GCC must not get both, or it links libnix's start code twice:
  a build that uses `-mcrt=nix20` sets `SDL2_RUNTIME=-mcrt=nix20` for
  `sdl2-config` (ACKitchen does the same for its stoves).
- **The FPU.** SDL2.module is built `-m68040 -m68881`, and SDL functions
  that return `float` or `double` return them in FP0. A program must be
  built for an FPU (`-m68881`, `-m68040` or `-m68060`).
- **GL is linked only when used.** `libSDL2.a`'s `SDL_GL_` functions are a
  member of their own: a program that calls one of them needs `-lGL` (which
  `sdl2-config --libs --gl` and `pkg-config --libs sdl2 gl` give), and one
  that calls none links no GL.
- **Both compilers.** One set of libraries serves GCC 6.5 (the os32 stove)
  and GCC 16 (os32-gcc16): the same libnix, objects and calling convention.
  The libraries were built with GCC 16 and checked with `fpcr_check.py`.
- **GCC 16.** It can put a float-to-int store in the wrong place at
  `-m68040`; run `tools/fpcr_check.py` (openamigartg) on what it builds.
  Programs using `_Thread_local` link `-lpthread` (libgcc's emutls).
- **No SDL main.** SDL needs no main of its own on the Amiga: `main` is the
  program's. `libSDL2main.a` is empty, for build systems that link it.
- **pkg-config**: `sdl2`, `SDL2_image`, `SDL2_mixer`, `SDL2_ttf`,
  `SDL2_net`, `gl`, `glesv2`, with `PKG_CONFIG_LIBDIR=<kit>/lib/pkgconfig`.
  The files find the kit from where they are.
- **CMake**: `find_package(SDL2)` with the kit in `CMAKE_PREFIX_PATH` gives
  `SDL2::SDL2`, `SDL2::SDL2main` (empty), `SDL2::SDL2test`,
  `SDL2_INCLUDE_DIRS` and `SDL2_LIBRARIES`; `find_package(SDL2_mixer)` and
  the others give `SDL2_mixer::SDL2_mixer` and so on.
- **The stack.** GL runs on the program's stack. A GL program asks libnix
  for a big one: `unsigned long __stack = 1024 * 1024;`.

## 5. SDL 2

SDL 2.32.10, with the Amiga back ends (libSDL2-amigaos3 0.7.0 and the
Team's):

| Part | On OpenGPU |
| --- | --- |
| Video | Intuition windows and screens. RTG through the CyberGraphX API (OpenRTG); AGA through c2p. |
| Renderer | `opengpu` first (fills, lines, copies with blending, scaling, rotation, geometry, YUV, drawn by OpenGPU), then `software`. |
| OpenGL | `SDL_WINDOW_OPENGL`, `SDL_GL_CreateContext`, `SDL_GL_SwapWindow`, `SDL_GL_GetProcAddress`, on GL.module (below). |
| Audio | AHI first (8 or 16-bit, mono or stereo), then Paula's audio.device. |
| Input | Keyboard, mouse (relative mode too), Ctrl-C quits; joysticks and CD32 pads through lowlevel.library. |
| Haptic | SDL's dummy driver: `SDL_INIT_HAPTIC` succeeds with no devices. |
| Clipboard | clipboard.device unit 0, as text. |
| Threads, timers | Exec processes and semaphores; timer.device for `SDL_Delay`. |
| Not there | Vulkan, `SDL_LoadObject`, sensors, power, locale, HIDAPI. |

The satellites: SDL_image 2.8.12 (BMP, GIF, JPEG, LBM, PCX, PNG, PNM, QOI,
SVG, TGA, XCF, XPM, XV), SDL_mixer 2.8.2 (WAV, AIFF, VOC, Ogg Vorbis, MP3,
FLAC, MIDI with Timidity, and MOD, XM, S3M, IT, MED with libxmp), SDL_ttf
2.24.0 (FreeType built in, no HarfBuzz) and SDL_net 2.4.0 (over
bsdsocket.library; link `-lsocket`, which its `.pc` file gives).

### SDL_GL on GL.module

A program has one copy of GL: the one its `libGL.a` opens. SDL's GL calls
use that copy, so SDL's context and the program's `gl` calls are the same
GL:

- `libSDL2.a`'s `SDL2_gl.o` hands the module the program's GL (struct
  `SDL2GLBridge`) when SDL starts, and the module calls it with the
  program's A4.
- `SDL_GL_SetAttribute` chooses the context: `SDL_GL_CONTEXT_PROFILE_MASK`
  (compatibility, core, or ES for GLES), the version, `SDL_GL_DOUBLEBUFFER`,
  `SDL_GL_DEPTH_SIZE` (16 or 24), `SDL_GL_STENCIL_SIZE` (8) and
  `SDL_GL_ALPHA_SIZE`. Compatibility 2.1, SDL's default, gives the highest
  compatibility version there is.
- Each window gets a GL buffer the size of its inside. `SDL_GL_SwapWindow`
  shows it in the window (OpenGPU, or WritePixelArray where the window is
  covered) and follows the window's size.
- Swap intervals are kept but frames are not held for the display.
- `SDL_GL_GetProcAddress` returns `libGL.a`'s own entries, so calls through
  what it gives set A4 too.

## 6. OpenGL, GLES and GLA

GL.module is Mesa's GL state tracker on one of two drivers:

| Driver | Where | Gives |
| --- | --- | --- |
| virgl | AmigaChrome: the GPU, through opengpu.library and the ACRTG board | GL 4.3, GLES 3.1 |
| softpipe | every other Amiga, or when asked | GL 3.3, GLES 3.1 |

The headers are Mesa's and Khronos's: `GL/gl.h`, `GL/glext.h`,
`GL/glcorearb.h`, `GLES2/`, `GLES3/`, `KHR/`. GLES 1 isn't there.

GLA (`include/gla/`) is OpenGPU's own interface to GL: what a program uses
without SDL.

| Call | What it does |
| --- | --- |
| `gla_os3_virgl_transport(&t)` | fills `t` when opengpu.library carries virgl; 0 when it doesn't |
| `gla_display_create_virgl(&t)` | a display on the GPU, or NULL |
| `gla_display_create()` | a softpipe display |
| `gla_display_driver(d)` | `"virgl"` or `"softpipe"` |
| `gla_display_version(d, profile)` | the version a profile has there: major × 10 + minor |
| `gla_display_destroy(d)` | |
| `gla_context_create(d, &cfg, share)` | a context: `cfg` is `struct gla_config` (profile `GLA_COMPAT`, `GLA_CORE` or `GLA_ES2`; major and minor, 0 for the highest; double buffer; depth 0, 16 or 24; stencil 0 or 8; alpha) |
| `gla_context_destroy(c)` | |
| `gla_os3_present_init(&p, &target)` | a present hook drawing into `target`: a RastPort, and the corner of the GL area in it |
| `gla_buffer_create(d, &cfg, w, h, &p)` | a buffer of `w` × `h`, shown through `p` |
| `gla_buffer_resize(b, w, h)`, `gla_buffer_destroy(b)` | |
| `gla_make_current(c, b)` | nonzero when it worked; `c` NULL makes none current |
| `gla_swap(c, b)` | finishes the frame and shows it |
| `gla_get_proc_address(name)` | a GL or GLES call by name |

`examples/gla/glatriangle.c` uses all of these in a Workbench window.

## 7. Warp3D, MiniGL and opengpu.library

- **Warp3D** (`include/Warp3D/`, `proto/Warp3D.h`): Warp3D's API, drawn by
  OpenGPU's 3D (openamigartg `DESIGN.md`, section 6).
- **MiniGL** (`include/mgl/`, `libraries/minigl.h`, `proto/minigl.h`):
  OpenGL 1.1 on the same 3D. `libminigl.a` holds its stubs, `libmgl.a` the
  GLUT-style helpers.
- **opengpu.library** (`include/opengpu/`, `proto/opengpu.h`,
  `sfd/opengpu_lib.sfd`): `OGPU_Query` (what a back end does, per operation
  and pixel format), `OGPU_Submit` and `OGPU_Wait` (a command stream and its
  fence), `OGPU_BackEndName`, `OGPU_ModuleOpen` and `OGPU_ModuleClose`, and
  OpenGfx's calls. From opengpu.library 0.7, `OGFX_RegisterLook` and
  `OGFX_UnregisterLook` (`gfx.h`) let a program draw the system's look on
  RectFill and Text (OpenLook's window frames) without patching them: OpenGfx
  asks the look first. The stream's commands are in `include/opengpu/stream.h`
  and `stream3d.h`; `build.h` and `build3d.h` write them.
- **SFD files** (`sfd/`) for opengpu.library, openrtg.library and
  Warp3D.library, for other compilers' stubs and pragmas.

## 8. Settings

Set with `SetEnv` (or `SDL_SetHint` from the program):

| Variable | Effect |
| --- | --- |
| `SDL_RENDER_DRIVER=software` | SDL's software renderer instead of `opengpu` |
| `SDL_OPENGPU=0` | keeps SDL off OpenGPU: software renderer, windows by WritePixelArray |
| `SDL_OPENGPU_STATS=1` | logs how many commands OpenGPU and the CPU drew, when a renderer closes |
| `SDL_OPENGPU_GL=cpu` | SDL's GL on softpipe even where virgl is there |
| `SDL_AUDIODRIVER=paula` | Paula instead of AHI (`dummy` for none) |

## 9. Versions

| Kit | opengpu.library | SDL2.module | GL.module | SDL | Mesa |
| --- | --- | --- | --- | --- | --- |
| 0.6 (8 October 2026) | 0.6 | 3 | 2 | 2.32.10 | 26.2.4 |

Programs built with this kit run on SDL2.module 2 as well, without OpenGL.

Copyright (c) 2026 Dalsin Limited. MIT licence.
