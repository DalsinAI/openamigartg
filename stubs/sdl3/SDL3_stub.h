/*
  SDL 3's link library for AmigaOS 3.2 (Open): what SDL3_stub.c shares
  inside libSDL3.a.

  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
  Altered for AmigaOS 3.x by the AmigaChrome Team, 2026: SDL's dynamic API
  (src/dynapi/SDL_dynapi.c) cut down to its caller's half. It is not the
  original software.

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#ifndef SDL3_STUB_H
#define SDL3_STUB_H

/* As in SDL_dynapi.c: SDL_memcpy, SDL_memmove and SDL_memset are SDL's
   functions here, not the C library's under their names. */
#define SDL_SLOW_MEMCPY
#define SDL_SLOW_MEMMOVE
#define SDL_SLOW_MEMSET
#include <SDL3/SDL.h>
#define SDL_MAIN_NOIMPL
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
/* The table's entries for other systems' functions (iOS, Windows, Android,
   GDK) and their types, as SDL_dynapi.c has them. */
#include "SDL_dynapi_unsupported.h"
#include "core/SDL_core_unsupported.h"
#include "video/SDL_video_unsupported.h"

#include <exec/types.h>
#include <opengpu/module.h>

#include "sdl3_module.h"

/* The jump table: one pointer per function, in SDL_dynapi_procs.h order. */
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) typedef rc (SDLCALL *SDL_DYNAPIFN_##fn) params;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC

typedef struct
{
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) SDL_DYNAPIFN_##fn fn;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
} SDL_DYNAPI_jump_table;

extern SDL_DYNAPI_jump_table SDL3Stub_jump_table;
extern int SDL3Stub_ready;
extern APTR SDL3Stub_a4;            /* the module's A4 for this program */
extern void SDL3Stub_Init(void);

/* Every call into the module: its A4 for the call, the program's after. */
#define SDL3STUB_A4() OGPU_A4(SDL3Stub_a4)
#define SDL3STUB_INIT() do { if (!SDL3Stub_ready) SDL3Stub_Init(); } while (0)

/* An SDL function that jumps through the table. */
#define SDL3STUB_PROC(rc, fn, params, args, ret) \
    rc SDLCALL fn params                         \
    {                                            \
        SDL3STUB_INIT();                         \
        {                                        \
            SDL3STUB_A4();                       \
            ret SDL3Stub_jump_table.fn args;     \
        }                                        \
    }

#endif
