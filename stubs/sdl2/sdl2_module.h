/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl2) and the module (library/modules/sdl2/module) share
 * it. It is the module ABI of include/opengpu/module.h: an entry taking
 * SysBase, DOSBase, OpenGPUBase and a version, returning the module's table,
 * whose first word is its version.
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

/* The arguments are opengpu.library's OGPUModuleArgs (include/opengpu/
   module.h, opengpu.library 0.5); the same layout is spelt out here for a
   build against older headers. */
#if defined(__has_include)
#if __has_include(<opengpu/module.h>)
#include <opengpu/module.h>
#endif
#endif
#ifdef OPENGPU_MODULE_H
typedef struct OGPUModuleArgs SDL2ModuleArgs;
#else
typedef struct SDL2ModuleArgs {
    struct ExecBase *SysBase;
    struct Library *DOSBase;
    struct Library *OpenGPUBase;    /* opengpu.library, or NULL */
    ULONG version;                  /* SDL2_MODULE_VERSION the caller was built for */
} SDL2ModuleArgs;
#endif

struct SDL2ModuleTable {
    ULONG version;                  /* SDL2_MODULE_VERSION */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before UnLoadSeg: SDL_Quit, libnix's exit list */
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct SDL2ModuleTable *(*SDL2ModuleEntry)(SDL2ModuleArgs *args);

#endif
