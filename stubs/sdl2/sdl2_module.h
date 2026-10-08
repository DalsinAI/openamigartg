/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl2) and the module (library/modules/sdl2/module) share
 * it. It follows the layout's module ABI (an entry taking SysBase, DOSBase,
 * OpenGPUBase and a version, returning the module's table); when
 * include/opengpu/module.h lands, these become its types.
 *
 * The table's first function is SDL's own SDL_DYNAPI_entry: it fills the
 * caller's SDL jump table, so every SDL_ call a program makes goes straight
 * to the module's code. */
#ifndef SDL2_MODULE_H
#define SDL2_MODULE_H

#include <exec/types.h>
#include <exec/execbase.h>

#define SDL2_MODULE_NAME    "SDL2"
#define SDL2_MODULE_FILE    "LIBS:OpenGPU/SDL2.module"
#define SDL2_MODULE_VERSION 1

struct SDL2ModuleArgs {
    struct ExecBase *SysBase;
    struct Library *DOSBase;
    struct Library *OpenGPUBase;    /* the caller's, or NULL */
    ULONG version;                  /* SDL2_MODULE_VERSION the caller was built for */
};

struct SDL2ModuleTable {
    ULONG version;                  /* SDL2_MODULE_VERSION */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before UnLoadSeg: SDL_Quit, libnix's exit list */
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct SDL2ModuleTable *(*SDL2ModuleEntry)(struct SDL2ModuleArgs *args);

#endif
