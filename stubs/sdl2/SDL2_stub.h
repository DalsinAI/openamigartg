/*
  SDL 2's link library for AmigaOS 3.2 (Open): what SDL2_stub.c and
  SDL2_gl.c share inside libSDL2.a.

  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
  Altered for AmigaOS 3.x by Dalsin Limited, 2026: SDL's dynamic API
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

  The SDL_GL_ functions are in SDL2_gl.o, a member of their own, so a
  program that calls none of them links no GL (SDL2_gl.c). Both members
  include SDL_dynapi_procs.h through a copy the Makefile filters:
  SDL2_stub_procs.h without the window GL calls, SDL2_gl_procs.h with only
  them.
*/
#ifndef SDL2_STUB_H
#define SDL2_STUB_H

#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_vulkan.h"

#include <exec/types.h>
#include <opengpu/module.h>

#include "sdl2_module.h"

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

extern SDL_DYNAPI_jump_table SDL2Stub_jump_table;
extern int SDL2Stub_ready;
extern APTR SDL2Stub_a4;            /* the module's A4 for this program */
extern void SDL2Stub_Init(void);
/* The program's GL for the module (SDL2_gl.c sets it before main runs). */
extern const struct SDL2GLBridge *SDL2Stub_gl;

/* Called before SDL2.module closes, newest first: the satellite stubs
   (sat_stub.h) close their modules here while SDL is still open. */
struct SDL2StubCloseHook {
    struct SDL2StubCloseHook *next;
    void (*close)(void);
};
extern void SDL2Stub_AtClose(struct SDL2StubCloseHook *hook);

/* Every call into the module: its A4 for the call, the program's after. */
#define SDL2STUB_A4() OGPU_A4(SDL2Stub_a4)
#define SDL2STUB_INIT() do { if (!SDL2Stub_ready) SDL2Stub_Init(); } while (0)

/* An SDL function that jumps through the table. */
#define SDL2STUB_PROC(rc, fn, params, args, ret) \
    rc SDLCALL fn params                         \
    {                                            \
        SDL2STUB_INIT();                         \
        {                                        \
            SDL2STUB_A4();                       \
            ret SDL2Stub_jump_table.fn args;     \
        }                                        \
    }

#endif
