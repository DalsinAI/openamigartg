# SDL 1.2 on OpenGPU's SDL 2

SDL 1.2 programs build for AmigaOS 3.2 and run on OpenGPU's SDL 2 (`SDL2.module`). This is sdl12-compat, SDL's own layer that turns every SDL 1.2 call into SDL 2 calls. SDL 1.2 programs therefore draw, play and read input through the same engine as SDL 2 programs. There is no second SDL here.

## What it makes

`build.sh OUT_DIR` writes three things into `OUT_DIR/sdl12/`. `library/build.sh` runs it after SDL 2. `tools/make_sdk.py` picks the result up for the developer kit.

| File | What it is |
| --- | --- |
| `include/SDL/` | sdl12-compat's SDL 1.2 headers. They are Zlib, unlike SDL 1.2's own LGPL headers. |
| `libSDL.a` | sdl12-compat, with SDL 2's link library (`libSDL2.a`) inside it. |
| `libSDL_gl.a` | SDL 1.2's OpenGL calls. Programs link it with `sdl-config --gl`. |

A program needs only this:

    m68k-amigaos-gcc -O2 game.c -o Game $(sdl-config --cflags --libs)          # -lSDL
    m68k-amigaos-gcc -O2 glgame.c -o GLGame $(sdl-config --cflags --libs --gl) # and -lSDL_gl -lGL

## How it works

- **Looking up SDL 2's calls.** On other systems sdl12-compat opens SDL 2's shared library and looks up each SDL 2 call by name. On the Amiga, `patches/0001-amigaos3.patch` adds an AmigaOS 3 branch to its loader. That branch answers the lookups from a table (`sdl12_amiga.c`).
- **The table.** `build.sh` makes it from sdl12-compat's own list of the SDL 2 calls it uses (`SDL20_syms.h`).
- **Renaming SDL 2's names.** sdl12-compat defines the `SDL_` names with SDL 1.2's meanings. So `build.sh` copies `libSDL2.a`'s members into `libSDL.a` with every `SDL_` name renamed `SDL2X_` (and `SDL2Stub_` renamed `SDL2XStub_`). The table points at those.
  - The calls still reach `SDL2.module` exactly as they do from `libSDL2.a`.
  - A program must not link `-lSDL2` as well.
- **The OpenGL calls** are in their own table:
  - `sdl12_gl.c` (`libSDL_gl.a`) links SDL 2's GL calls and so needs `libGL.a`.
  - `sdl12_nogl.c` (in `libSDL.a`) is an empty table.

  `sdl-config --gl` picks the first with `-Wl,-u,_SDL12Amiga_gl -lSDL_gl`. Without it, a program links no GL: its `SDL_GL_` calls answer 0, and `SDL_SetVideoMode(..., SDL_OPENGL)` fails.
- **GLU.** The kit has none. `sdl-config --cflags` gives `-DNO_SDL_GLU`, so `SDL_opengl.h` leaves `GL/glu.h` out.
- **The build checks** that every SDL 2 call sdl12-compat uses is in `libSDL2.a`, and runs `tools/fpcr_check.py` on the new objects.

## Source

- **sdl12-compat 1.2.78**, pinned by SHA-256 in `UPSTREAM.json`. It is taken from `TARBALLS` (default `~/AmigaChrome-dev/upstream-tarballs`), and nothing of it is committed.
- **The Team's change** to it is `patches/0001-amigaos3.patch`: the AmigaOS 3 branch of the loader, and the program's name for its per-program settings. The changed file is marked as altered, as its licence asks.
- **Built with the GCC 16 stove**, as SDL 2 is. Programs link it with either stove.

## Licences

- **sdl12-compat:** Zlib, with dr_mp3 inside it (public domain or MIT-0). The kit's `Licences/sdl12-compat.txt` carries its notice.
- **`sdl12_amiga.c`, `sdl12_amiga.h`, `sdl12_nogl.c` and `build.sh`:** MIT, Copyright (c) 2026 Dalsin Limited.
