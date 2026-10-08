# SDL 2 in OpenGPU: SDL2.module

SDL 2 for AmigaOS 3.2, as part of the one library. Programs open only
opengpu.library and link `-lSDL2` as on any other system. Their SDL calls
go to `LIBS:OpenGPU/SDL2.module`: SDL 2.32.10 with Amiga back ends that
draw through OpenGPU, play through AHI and read the Amiga's input. When
OpenGPU can't do something, SDL's own software code does it, so every
program runs.

## What is built

`library/modules/sdl2/build.sh [OUT_DIR]` (default `build/`) builds, with
the GCC 16 stove:

| File | What it is |
| --- | --- |
| `SDL2.module` | SDL 2 and its back ends. Goes in `LIBS:OpenGPU/`. |
| `libSDL2.a` | The link library programs use (`stubs/sdl2`). |
| `include/SDL2/` | SDL's headers, and the satellites', for programs. |
| `libSDL2_static.a` | All of SDL inside the program, with no module. |
| `libSDL2_image.a`, `libSDL2_mixer.a`, `libSDL2_ttf.a`, `libSDL2_net.a` | The satellite libraries. |
| `tests/sdl2/` | The tests (`tests/sdl2` in the repository). |

Build a program the usual way:

    m68k-amigaos-gcc -m68040 -m68881 -noixemul -Ibuild/include/SDL2 game.c -Lbuild -lSDL2 -lm -lamiga

## How a program reaches SDL

- `libSDL2.a` is SDL's own dynamic API, cut down to the caller's half. Every
  `SDL_` function jumps through a table.
- The first SDL call loads `SDL2.module` and calls its `SDL_DYNAPI_entry`,
  which fills the table with the module's functions. The table's layout is
  SDL's `SDL_dynapi_procs.h`, so a program built today runs on any later
  module.
- The module is shared (residency step 2, DESIGN.md section 5): it is built
  `-fbaserel32`, so opengpu.library loads its code once for every program
  and each program's open gets its own copy of SDL's globals. Every stub
  function sets A4 to the program's copy for the call and puts the
  program's A4 back (`OGPU_A4`, `include/opengpu/module.h`).
- SDL's calls into the program (the audio callback, timers, a thread's
  function, event filters and watchers) run with the program's A4, and the
  threads SDL starts begin with the A4 of the program that started them
  (`src/SDL_os3callout.h`, `patches/sdl2/0005`, `patches/amigaos3/0005`).
  Other callbacks (the log output function, hint callbacks, an RWops of the
  program's) arrive with the module's A4, which only matters to a program
  built `-fbaserel` itself; it marks them `__saveds`, as Amiga callbacks
  always need.
- `make SHARED=0` builds a copy for each program instead (step 1).
- The module's interface is `include/opengpu/module.h`'s (`stubs/sdl2/sdl2_module.h`
  adds SDL's table): the first code takes SysBase, DOSBase, OpenGPUBase and
  a version, and returns a table. On opengpu.library 0.5 and later the stub
  loads it with `OGPU_ModuleOpen`; on older ones it LoadSegs it itself.
- The module has no program startup. `library/modules/common`
  (`module_start.S`, `module_rt.c`) runs libnix's init list (memory,
  standard I/O on the calling program's Input and Output, the libraries
  libnix opens, the constructors) and its exit list when the program ends,
  and gives `getenv` through GetVar.
- `tools/baserel_check.py` checks every build reaches SDL's data only
  through A4; `baserel.allow` lists the reviewed exceptions (tables of the
  back ends' bootstraps and render drivers, two constant tables), which only
  ever read the data's first values.
- Kalms' c2p (`patches/amigaos3/0005`) takes its sizes in registers, so the
  code keeps no data of its own.

## The Amiga back ends

| SDL part | Back end |
| --- | --- |
| Video | Intuition windows and screens. RTG through the CyberGraphX API (OpenRTG's cybergraphics.library); AGA through c2p. A window on an RTG screen is shown by OpenGPU, which copies the frame into video RAM through each visible part of the window's layer; WritePixelArray does it when part of the window is covered. |
| Renderer | `opengpu` first, then `software`. See below. |
| Audio | AHI first (8 or 16-bit, mono or stereo, double buffered with `ahir_Link`, opened in SDL's audio thread), then Paula's audio.device. |
| Threads, timers | Exec processes and semaphores. `SDL_Delay`, timed semaphore and condition waits use timer.device's UNIT_MICROHZ, so `SDL_Delay(1)` is a millisecond, not dos.library's 20 ms tick. |
| Input | Keyboard and mouse from Intuition; relative mouse mode with IDCMP_DELTAMOVE; Ctrl-C quits. Joysticks and CD32 pads through lowlevel.library. |
| Clipboard | clipboard.device unit 0, IFF FTXT, converted between UTF-8 and Latin-1. On AmigaChrome, ACClip carries it to the PC. |
| Environment | `SDL_getenv` reads the Shell's local and global variables (SetEnv, ENV:) as well as the program's own. |

Settings, with `SetEnv` or `SDL_SetHint`:

- `SDL_RENDER_DRIVER=software` picks the software renderer.
- `SDL_OPENGPU=0` keeps SDL off OpenGPU altogether.
- `SDL_OPENGPU_STATS=1` logs, when a renderer closes, how many commands
  OpenGPU drew and how many the CPU drew.
- `SDL_AUDIODRIVER=paula` picks Paula.

### The opengpu renderer

Textures and the target are surfaces in memory, described to OpenGPU as
slots. Each `RunCommandQueue` becomes one OpenGPU batch:

| SDL | OpenGPU |
| --- | --- |
| Clear, opaque fills | FILL |
| Fills in any SDL blend mode | FILL_BLEND (stream v1.2) |
| Points and lines | POINTS_BLEND, LINES_BLEND (v1.2) |
| Copies, scaled or not, with alpha | COMPOSITE in SDL's blend mode; COPY for a plain 1:1 copy |
| Copies with colour modulation, rotation, flips or renderer scale | COMPOSITE_AFFINE (v1.2), one command each |
| `SDL_RenderGeometry` | TRIANGLES, when `include/opengpu/build3d.h` is there |
| YUV and NV12 textures | YUV (v1.2) into the texture's own XRGB surface |

The renderer asks `OGPU_Query` for each operation on the target's format.
A library without stream v1.2, or a back end without an operation, sends
that command to SDL's software code, which draws into the same surface
after the batch so far has finished. Custom blend modes always take that
path. SDL chooses the software renderer by itself when opengpu.library
isn't there or the window's format isn't one OpenGPU draws.

`tests/sdl2/rendercompare` draws the same scenes with both renderers and
compares them.

## How the source is put together

Nothing third-party is committed. `build.sh`:

1. checks SDL 2.32.10's tarball against its sha256 in `UPSTREAM.json`
   (from `TARBALLS`, default `~/AmigaChrome-dev/upstream-tarballs`, or
   downloaded once);
2. fetches libSDL2-amigaos3 (Zlib) at its pinned commit for the Amiga
   back ends it started (video, AGA c2p, Paula, threads, timer, filesystem);
3. copies SDL's software renderer to `src/render/opengpu/`, then applies
   `patches/sdl2` (the platform in SDL's lists, three fixes, the
   environment, and the opengpu renderer as a change to that copy) and
   `patches/amigaos3` (the Team's changes to libSDL2-amigaos3's back ends);
4. adds `src/` (the Team's own files: AHI, the joystick, the clipboard,
   OpenGPU's helpers) and `include/SDL_config_amigaos.h`;
5. builds the satellites from their pinned tarballs (`satellites/`).

| Library | Licence | Notes |
| --- | --- | --- |
| SDL 2.32.10 | Zlib | |
| libSDL2-amigaos3 0.7.0 | Zlib | |
| SDL2_image 2.8.12 | Zlib | stb_image (PNG, JPEG), nanosvg, QOI, miniz and tiny_jpeg are its own, built in |
| SDL2_mixer 2.8.2 | Zlib | stb_vorbis, minimp3, dr_flac and Timidity are its own, built in |
| SDL2_ttf 2.24.0 | Zlib | without HarfBuzz |
| SDL2_net 2.4.0 | Zlib | over bsdsocket.library (libnix's `-lsocket`; its `select()` is `satellites/net_shim.c`'s, as libnix's never sees a socket with data) |
| FreeType 2.14.3 | FreeType Licence | for SDL2_ttf |
| libxmp 4.7.3 | MIT | for SDL2_mixer's MOD, XM, S3M, IT, MED and the other tracker formats |

The Team's files are MIT, Copyright (c) 2026 Dalsin Limited. Files that
alter SDL's (the stub, the renderer patch) keep SDL's Zlib notice and say
they are altered.

## Measured

`measurements/20261008-sdl2-opengpu.txt`: SDL's testsprite2 and
testrendercopyex on the CPU and on OpenGPU, with `tests/sdl2/bench`.

Residency step 2 (8 October 2026, `measurements/20261008-residency-step2.txt`):
a first open of SDL2.module takes 1,057 KB and a further one 171 KB; a call
through libSDL2.a costs 0.021 µs against 0.015 before (SDL_GetCPUCount on
the AC090); testsprite2 runs at 504 to 539 fps, as before (525 to 553).

## Next

- `SDL_GL_*` on GL.module, when it is there.
- Remove the stub's own LoadSeg once every install has opengpu.library 0.5.
- The GCC 16 stove's FPCR clash (`fpcr_check.py`): three satellite files are
  built at -O0 until the stove is fixed, and the build checks everything.
