/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's interface: what its first code takes and gives back. The
 * link stub (stubs/openal, libopenal.a) and the module
 * (library/modules/openal/module) share it. It is the module ABI of
 * include/opengpu/module.h, the same as SDL2.module's and SDL3.module's: an
 * entry taking SysBase, DOSBase, OpenGPUBase and a version, returning the
 * module's table, which starts with struct OGPUModuleTable (its version and
 * the A4 for calls into it).
 *
 * The table's first function fills the caller's jump table (struct
 * OpenALProcs, openal_procs.h's order) with the module's AL and ALC calls,
 * as SDL's SDL_DYNAPI_entry does for SDL: every AL call a program makes
 * then goes to the module's code, with the table's A4 set by the stub.
 *
 * Version 2 (24 October 2026): the first; 2 because it starts with the A4
 * calls (OGPU_MODULE_A4_VERSION), as SDL3.module's version 2 does. */
#ifndef OPENAL_MODULE_H
#define OPENAL_MODULE_H

#include <exec/types.h>
#include <opengpu/module.h>

#include <AL/al.h>
#include <AL/alc.h>

#define OPENAL_MODULE_NAME    "OpenAL"
#define OPENAL_MODULE_FILE    "LIBS:OpenGPU/OpenAL.module"
#define OPENAL_MODULE_VERSION 2
#define OPENAL_MODULE_OPEN_VERSION 2    /* what a stub asks for */

/* The jump table's layout version: openal_procs.h's list, which only grows. */
#define OPENAL_PROCS_VERSION 1

/* The jump table: one pointer per call, in openal_procs.h's order. */
struct OpenALProcs {
#define OPENAL_PROC(rc, fn, params, args) rc (*fn) params;
#define OPENAL_PROCV(fn, params, args) void (*fn) params;
#include "openal_procs.h"
#undef OPENAL_PROC
#undef OPENAL_PROCV
};

typedef struct OGPUModuleArgs OpenALModuleArgs;

struct OpenALModuleTable {
    struct OGPUModuleTable head;    /* OPENAL_MODULE_VERSION, a4, caller_a4 */
    /* Fills procs (size bytes: the caller's struct OpenALProcs) with the
     * module's calls. 0, or -1 when the caller's table is longer than the
     * module's (the program needs calls this module hasn't got) or its
     * layout version is newer. */
    LONG (*fill)(ULONG version, void *procs, ULONG size);
    void (*close)(void);            /* before OGPU_ModuleClose: every device closed, libnix's exit list */
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct OGPUModuleTable *(*OpenALModuleEntry)(OpenALModuleArgs *args);

#endif
