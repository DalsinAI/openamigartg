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
| `patches/amigaos3/0001-shared-module.patch` | SDL's threads start with the A4 of the program's copy of SDL. |
| `patches/amigaos3/0002-build.patch` | What the back ends need to build on SDL 3.4.14 as released. |
| `src/SDL_os3callout.h` | The callouts (as SDL2.module's). |
| `include/SDL_build_config_amigaos3.h` | SDL's configuration for the Amiga. |
| `module/sdl3_module.c` | The module's table (`stubs/sdl3/sdl3_module.h`). |
| `../../../stubs/sdl3` | `libSDL3.a`, the link library. |
| `../../../tests/sdl3` | The runner for SDL's own tests. |

## What is built

`library/modules/sdl3/build.sh [OUT_DIR]` (default `build/`), with the GCC 16
stove:

| File | What it is |
| --- | --- |
| `SDL3.module` | SDL 3 and its back ends. Goes in `LIBS:OpenGPU/`. |
| `libSDL3.a` | The link library programs use. |
| `include/SDL3/` | SDL's headers, for programs. |
| `libSDL3_static.a`, `libSDL3_test.a` | All of SDL inside the program, with no module; SDL's test framework. |
| `tests/sdl3/` | SDL's `testsprite` and `testrendercopyex`, static and on the module (`.m`). |

Build a program the usual way:

    m68k-amigaos-gcc -m68040 -m68881 -noixemul -Ibuild/include game.c -Lbuild -lSDL3 -lm -lamiga

## Still to come

SDL2.module's OpenGPU work comes over next, as patches on these back ends:
the opengpu renderer and one submit a frame, video RAM and Fast RAM
surfaces, the screen's own pixel format, AGA chunky to planar, Paula audio,
OpenInput joysticks, the clipboard and OpenGL through OpenGPU's GL module.
