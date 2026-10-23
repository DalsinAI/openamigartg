/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL3.module: SDL 3 (with the Amiga back ends) as a module that
 * opengpu.library, or the link stub, loads for a program, built the same way
 * as SDL2.module (library/modules/sdl2/module/sdl2_module.c). It is linked
 * on library/modules/common: module_start.S's first code, and module_rt.c,
 * which sets libnix up the way its startup code would for a program.
 *
 * The module is built -fbaserel32, so opengpu.library loads its code once
 * for every program and gives each program its own copy of SDL's globals;
 * the stub sets A4 on every call (stubs/sdl3). SDL's calls back into the
 * program run with the program's A4, and its threads start with the
 * module's (src/SDL_os3callout.h). Built without -fbaserel32 (SHARED=0 in
 * the Makefile) it is a copy for each program. */
#include <exec/types.h>

#include "module_rt.h"
#include "sdl3_module.h"

/* SDL's dynamic API (src/dynapi/SDL_dynapi.c, built with
   SDL_OPEN_LIBRARY_BUILD). */
extern long SDL_DYNAPI_entry(unsigned long apiver, void *table, unsigned long tablesize);
extern void SDL_Quit(void);

static void SDL3Module_Close(void)
{
    SDL_Quit();
    ogpu_module_end();
}

static struct SDL3ModuleTable SDL3Module_table = {
    { SDL3_MODULE_VERSION, 0, 0 },
    (LONG (*)(ULONG, void *, ULONG))SDL_DYNAPI_entry,
    SDL3Module_Close,
};

struct OGPUModuleTable *ogpu_module_entry(SDL3ModuleArgs *args)
{
    if (!ogpu_module_begin(args, SDL3_MODULE_VERSION, &SDL3Module_table.head)) {
        return NULL;
    }
    return &SDL3Module_table.head;
}
