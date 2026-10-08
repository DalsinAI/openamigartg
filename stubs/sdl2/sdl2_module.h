/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl2) and the module (library/modules/sdl2/module) share
 * it. It is the module ABI of include/opengpu/module.h: an entry taking
 * SysBase, DOSBase, OpenGPUBase and a version, returning the module's table,
 * which starts with struct OGPUModuleTable (its version and the A4 for
 * calls into it).
 *
 * The table's first function is SDL's own SDL_DYNAPI_entry: it fills the
 * caller's SDL jump table, so every SDL_ call a program makes goes to the
 * module's code, with the table's A4 set by the stub.
 *
 * Version 2 (8 October 2026): the A4 calls (residency step 2). A version 1
 * stub is refused, as it would call the module without A4. */
#ifndef SDL2_MODULE_H
#define SDL2_MODULE_H

#include <exec/types.h>
#include <exec/execbase.h>
#include <opengpu/module.h>

#define SDL2_MODULE_NAME    "SDL2"
#define SDL2_MODULE_FILE    "LIBS:OpenGPU/SDL2.module"
#define SDL2_MODULE_VERSION 2

typedef struct OGPUModuleArgs SDL2ModuleArgs;

struct SDL2ModuleTable {
    struct OGPUModuleTable head;    /* SDL2_MODULE_VERSION, a4, caller_a4 */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before OGPU_ModuleClose: SDL_Quit, libnix's exit list */
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct OGPUModuleTable *(*SDL2ModuleEntry)(SDL2ModuleArgs *args);

#endif
