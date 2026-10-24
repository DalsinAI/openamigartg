/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module: OpenAL 1.1 (library/modules/openal/src) as a module that
 * opengpu.library, or the link stub, loads for a program, built the same way
 * as SDL3.module (library/modules/sdl3/module/sdl3_module.c). It is linked
 * on library/modules/common: module_start.S's first code, and module_rt.c,
 * which sets libnix up the way its startup code would for a program.
 *
 * The module is built -fbaserel32, so opengpu.library loads its code once
 * for every program and gives each program its own copy of OpenAL's state
 * (its devices, contexts, sources and buffers); the stub sets A4 on every
 * call (stubs/openal). Mix's output process starts with the A4 of the
 * program's copy (src/al_ahi.c). Built without -fbaserel32 (SHARED=0 in the
 * Makefile) it is a copy for each program. */
#include <exec/types.h>
#include <string.h>

#include "module_rt.h"
#include "openal_module.h"
#include "al_internal.h"

/* The module's calls, in the jump table's order. */
static const struct OpenALProcs OpenALModule_procs = {
#define OPENAL_PROC(rc, fn, params, args) fn,
#define OPENAL_PROCV(fn, params, args) fn,
#include "openal_procs.h"
#undef OPENAL_PROC
#undef OPENAL_PROCV
};

static LONG OpenALModule_Fill(ULONG version, void *procs, ULONG size)
{
    if (version > OPENAL_PROCS_VERSION || size > sizeof OpenALModule_procs) {
        return -1;
    }
    memcpy(procs, &OpenALModule_procs, size);
    return 0;
}

static void OpenALModule_Close(void)
{
    al_core_shutdown();
    ogpu_module_end();
}

static struct OpenALModuleTable OpenALModule_table = {
    { OPENAL_MODULE_VERSION, 0, 0 },
    OpenALModule_Fill,
    OpenALModule_Close,
};

struct OGPUModuleTable *ogpu_module_entry(OpenALModuleArgs *args)
{
    if (!ogpu_module_begin(args, OPENAL_MODULE_VERSION, &OpenALModule_table.head)) {
        return NULL;
    }
    al_core_init();
    return &OpenALModule_table.head;
}
