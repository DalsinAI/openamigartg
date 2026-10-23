/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL3.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl3) and the module (library/modules/sdl3/module) share
 * it. It is the module ABI of include/opengpu/module.h, the same as
 * SDL2.module's (stubs/sdl2/sdl2_module.h): an entry taking SysBase, DOSBase,
 * OpenGPUBase and a version, returning the module's table, which starts with
 * struct OGPUModuleTable (its version and the A4 for calls into it).
 *
 * The table's first function is SDL's own SDL_DYNAPI_entry: it fills the
 * caller's SDL jump table (SDL_dynapi_procs.h, SDL's dynamic API version 2),
 * so every SDL_ call a program makes goes to the module's code, with the
 * table's A4 set by the stub.
 *
 * Version 2 (22 October 2026): the first; 2 because it starts with the A4
 * calls (OGPU_MODULE_A4_VERSION), as SDL2.module's version 2 does. */
#ifndef SDL3_MODULE_H
#define SDL3_MODULE_H

#include <exec/types.h>
#include <exec/execbase.h>
#include <opengpu/module.h>

#define SDL3_MODULE_NAME    "SDL3"
#define SDL3_MODULE_FILE    "LIBS:OpenGPU/SDL3.module"
#define SDL3_MODULE_VERSION 2
#define SDL3_MODULE_OPEN_VERSION 2      /* what a stub asks for */

typedef struct OGPUModuleArgs SDL3ModuleArgs;

struct SDL3ModuleTable {
    struct OGPUModuleTable head;    /* SDL3_MODULE_VERSION, a4, caller_a4 */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before OGPU_ModuleClose: SDL_Quit, libnix's exit list */
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct OGPUModuleTable *(*SDL3ModuleEntry)(SDL3ModuleArgs *args);

#endif
