/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * sdl12-compat's link to SDL 2 on AmigaOS 3 (library/modules/sdl12). On
 * other systems sdl12-compat opens SDL 2's shared library and looks its
 * calls up by name. Here SDL 2 is OpenGPU's SDL2.module, reached through
 * libSDL2.a's calls. Those calls are linked into libSDL.a under the names
 * SDL2X_ (build.sh renames them), because sdl12-compat itself defines the
 * SDL_ names with SDL 1.2's meanings. SDL12Amiga_LookupSDL20 answers
 * sdl12-compat's lookups from tables of them, which build.sh makes from
 * sdl12-compat's own list of the SDL 2 calls it uses (SDL20_syms.h):
 *
 *   SDL12Amiga_syms  every call but SDL_GL_ (sdl12_syms.c, in libSDL.a)
 *   SDL12Amiga_gl    the SDL_GL_ calls: sdl12_gl.c (libSDL_gl.a), which
 *                    links SDL 2's GL calls and so libGL.a; or sdl12_nogl.c
 *                    (in libSDL.a), an empty table, for programs without GL.
 *
 * sdl-config --libs --gl picks the first (-Wl,-u,_SDL12Amiga_gl -lSDL_gl);
 * without --gl a program links no GL, its SDL_GL_ calls answer 0, and
 * SDL_SetVideoMode(SDL_OPENGL) fails. */
#include <string.h>
#include <exec/types.h>
#include <proto/dos.h>

#include "sdl12_amiga.h"

/* What a call this program didn't link answers: 0. */
static long sdl12amiga_none(void)
{
    return 0;
}

static void *find(const struct sdl12amiga_sym *t, unsigned n, const char *name)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i].name, name))
            return t[i].fn;
    return NULL;
}

void *SDL12Amiga_LookupSDL20(const char *name)
{
    void *fn = find(SDL12Amiga_syms, SDL12Amiga_nsyms, name);
    if (fn)
        return fn;
    if (!strncmp(name, "SDL_GL_", 7)) {
        fn = find(SDL12Amiga_gl, SDL12Amiga_ngl, name);
        return fn ? fn : (void *)sdl12amiga_none;
    }
    return NULL;
}

/* The program's name, for sdl12-compat's per-program settings. */
void SDL12Amiga_GetExeName(char *buf, unsigned maxpath)
{
    char path[256];
    const char *file;
    buf[0] = '\0';
    if (!maxpath || !GetProgramName((STRPTR)path, sizeof(path)))
        return;
    file = (const char *)FilePart((CONST_STRPTR)path);
    strncpy(buf, file, maxpath - 1);
    buf[maxpath - 1] = '\0';
}
