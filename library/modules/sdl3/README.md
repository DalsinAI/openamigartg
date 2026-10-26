# SDL 3 in OpenGPU: SDL3.module

SDL 3 for AmigaOS 3.2, built the same way as SDL2.module
(`library/modules/sdl2`): one copy of SDL's code in `LIBS:OpenGPU/SDL3.module`
for every program, each program with its own copy of SDL's data, and
programs that link `-lSDL3` as on any other system. Built hard float
(`-m68040 -m68881`): it needs a 68040 or 68060 with its FPU.

SDL 3.4.14 (Zlib) is the core, from its release tarball (checked against its
sha256). The Amiga back ends start from SDL3_68k (Zlib), fetched at the commit
`UPSTREAM.json` pins: video through intuition and graphics, AHI audio,
threads, timer, time, joystick, filesystem, locale and file dialogs. Nothing
third-party is committed; the Team's work is in:

| Path | What it is |
| --- | --- |
| `patches/sdl3/0001-amigaos3-platform.patch` | AmigaOS 3.x as an SDL platform; SDL's dynamic API on inside the module. |
| `patches/sdl3/0002-module-callouts.patch` | SDL's calls into the program run with the program's A4. |
| `patches/sdl3/0003-amigaos3-atomics.patch` | SDL's spinlock with Forbid(), and its atomic exchange through it (libnix has no libatomic). |
| `patches/sdl3/0004-opengpu-renderer.patch` | The opengpu renderer (below): build.sh copies SDL's software renderer to `src/render/opengpu`, and this turns the copy into it. It goes in SDL's list before the software one, and takes lines as lines. |
| `patches/amigaos3/0001-shared-module.patch` | SDL's threads start with the A4 of the program's copy of SDL. |
| `patches/amigaos3/0002-build.patch` | What the back ends need to build on SDL 3.4.14 as released. |
| `patches/amigaos3/0003-opengpu-present.patch` | The window framebuffer shown through OpenGPU where it can be, WritePixelArray otherwise. |
| `patches/amigaos3/0004-timer-signal.patch` | timer.device opened without a reply port, and closed when the program ends: before, each SDL program left a signal behind in its Shell, and after about a dozen runs that Shell could open no windows. |
| `patches/amigaos3/0005-screensaver.patch` | While SDL holds the screen saver off (`SDL_DisableScreenSaver`, SDL's default while a program runs), OpenBlanker is told every 5 seconds: a CTRL_E to the task of its `OpenBlanker` port, which OpenBlanker 1.2 counts as a key or the mouse. `SDL_VIDEO_ALLOW_SCREENSAVER=1` lets the show come. |
| `patches/amigaos3/0006-system-cursor-ownership.patch` | System cursor creation returns an unregistered backend cursor. Calling SDL's public cursor factory from the backend registered the same cursor twice, made its `next` point to itself, and hung SDL_SetCursor when selecting an older cursor (observed in OpenLoco). |
| `src/video/amigaos3/SDL_os3gpu.c`, `.h` | opengpu.library for SDL: opening it, the pixel formats, and a window's present (one copy into the screen's bitmap through the window's visible parts). |
| `src/SDL_os3callout.h` | The callouts (as SDL2.module's). |
| `include/SDL_build_config_amigaos3.h` | SDL's configuration for the Amiga. |
| `module/sdl3_module.c` | The module's table (`stubs/sdl3/sdl3_module.h`). |
| `../../../stubs/sdl3` | `libSDL3.a`, the link library. |
| `../../../tests/sdl3` | The runner for SDL's own tests; `nomodule.c` and the stub's host test, `stub_nomodule.c` (no SDL3.module); `rendercompare.c`, and `rendercompare_host.sh` with `render_host/`, which run it on a PC. |

## What is built

The cursor ownership regression runs on a host with
`python3 tests/sdl3/system_cursor_host.py`. The native `cursor_lifecycle.m`
test creates custom and system cursors, selects older cursors, and destroys
them, for 30 rounds before `SDL_Quit`. It must complete with return code 0;
the broken backend can loop indefinitely while selecting an older cursor.

`library/modules/sdl3/build.sh [OUT_DIR]` (default `build/`), with the GCC 16
stove:

| File | What it is |
| --- | --- |
| `SDL3.module` | SDL 3 and its back ends. Goes in `LIBS:OpenGPU/`. |
| `libSDL3.a` | The link library programs use. |
| `include/SDL3/` | SDL's headers, for programs. |
| `libSDL3_static.a`, `libSDL3_test.a` | All of SDL inside the program, with no module; SDL's test framework. |
| `tests/sdl3/` | SDL's `testsprite` and `testrendercopyex`, static and on the module (`.m`); `rendercompare` and `rendercompare.m` (below); `nomodule.m`, run with the module renamed away: one line saying it isn't installed, and return code 20. |

Build a program the usual way:

    m68k-amigaos-gcc -m68040 -m68881 -noixemul -Ibuild/include game.c -Lbuild -lSDL3 -lm -lamiga

## The opengpu renderer

SDL_CreateRenderer gives a program the `opengpu` renderer when
opengpu.library opens and OpenGPU draws on the window's surface; otherwise
SDL's software renderer. It is SDL's software renderer with its drawing sent
to OpenGPU, in one batch for each of SDL's command queues:

| SDL | OpenGPU |
| --- | --- |
| clears, opaque fills | FILL |
| fills, points and lines in any blend mode | FILL_BLEND, POINTS_BLEND, LINES_BLEND (SDL's own rule for line ends) |
| SDL_RenderTexture | COPY (a plain 1:1 copy), COMPOSITE in SDL's blend mode |
| colour modulation, rotation, flips, the render scale | COMPOSITE_AFFINE, one command each |
| SDL_RenderGeometry | TRIANGLES, clamped or repeated each way |
| palette (INDEX8) textures | read through their PALETTE: a palette change uploads nothing |
| YUV, NV12, NV21 and P010 textures | YUV into the texture's own ARGB32 surface, or YUV_SCALED straight into the target |
| SDL_RenderPresent | the copy to the screen, at the end of the frame's last batch: one submit a frame |

Stream v1.7 (opengpu.library 0.19) brings SDL 3's own: the premultiplied
blend modes, `SDL_SCALEMODE_PIXELART`, clamping one way, palette textures,
the RGBA32 and ABGR32 byte orders, P010, and BT.709's full range and
BT.2020. With an older library those draw on the CPU, and their texture
formats aren't offered, so SDL converts them. Anything OpenGPU doesn't
answer for on the target's format is drawn by SDL's software code into the
same surface, after OpenGPU has finished what it was given.

- `SDL_RENDER_DRIVER=software`: SDL's software renderer.
- `SDL_OPENGPU=0`: SDL off OpenGPU altogether (windows by WritePixelArray).
- `SDL_OPENGPU_STATS=1`: how many commands each path drew, logged at the end.

`tests/sdl3/rendercompare` draws 16 scenes with both renderers and compares
them. `tests/sdl3/rendercompare_host.sh` runs it on a Linux PC (SDL built
for the PC, OpenGPU's own core as the library, with the sanitizers), once as
0.19 and once as 0.18, where no command may carry v1.7.

## Still to come

The rest of SDL2.module's OpenGPU work, as patches on these back ends:
video RAM surfaces, the screen's own pixel format, AGA chunky to planar,
Paula audio, OpenInput joysticks, the clipboard and OpenGL through OpenGPU's
GL module.
