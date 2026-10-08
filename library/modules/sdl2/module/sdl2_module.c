/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module: SDL 2 (with the Amiga back ends) as a module that
 * opengpu.library, or the link stub, loads for a program. It is linked on
 * library/modules/common: module_start.S's first code, and module_rt.c,
 * which sets libnix up the way its startup code would for a program
 * (SysBase and DOSBase, then its init list: memory, standard I/O on the
 * calling program's Input and Output, auto-opened libraries, constructors)
 * and reads variables with GetVar.
 *
 * Step 2 of the layout's residency plan: the module is built -fbaserel32,
 * so opengpu.library loads its code once for every program and gives each
 * program its own copy of SDL's globals; the stub sets A4 on every call
 * (stubs/sdl2). SDL's calls back into the program (audio, timers, threads,
 * event filters) run with the program's A4, and its threads start with the
 * module's (src/SDL_os3callout.h). Built without -fbaserel32 (SHARED=0 in
 * the Makefile) it is a copy for each program, as in step 1. */
#include <exec/types.h>

#include "module_rt.h"
#include "sdl2_module.h"

/* SDL's dynamic API (src/dynapi/SDL_dynapi.c, built with
   SDL_OPEN_LIBRARY_BUILD). */
extern long SDL_DYNAPI_entry(unsigned long apiver, void *table, unsigned long tablesize);
extern void SDL_Quit(void);

static void SDL2Module_Close(void)
{
    SDL_Quit();
    ogpu_module_end();
}

static struct SDL2ModuleTable SDL2Module_table = {
    { SDL2_MODULE_VERSION, 0, 0 },
    (LONG (*)(ULONG, void *, ULONG))SDL_DYNAPI_entry,
    SDL2Module_Close,
};

struct OGPUModuleTable *ogpu_module_entry(SDL2ModuleArgs *args)
{
    if (!ogpu_module_begin(args, SDL2_MODULE_VERSION, &SDL2Module_table.head)) {
        return NULL;
    }
    return &SDL2Module_table.head;
}
