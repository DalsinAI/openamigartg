/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A satellite module's first table and entry (stubs/sdl2/sat_module.h),
 * included once by each module's own file with
 *   SAT_PROCS           its ABI file ("SDL_image_procs.h")
 *   SAT_CLOSE_WITH_SDL  what to close in SDL when the program ends with SDL
 *                       still open (statements)
 *   SAT_CLOSE_ALWAYS    what to close outside SDL (statements)
 * after the library's header. Linked on library/modules/common
 * (module_start.S first, module_rt.c), as SDL2.module is. */
#ifndef SAT_MODULE_BODY_H
#define SAT_MODULE_BODY_H

#include <string.h>

#include "module_rt.h"
#include "sat_module.h"
#include "sat_rt.h"

typedef struct {
#define SAT_PROC(rc, fn, params, args, ret) rc (SDLCALL *fn) params;
#include SAT_PROCS
#undef SAT_PROC
} SatJumpTable;

static ULONG sat_bind(const void *sdl, ULONG sdl_size, APTR sdl_a4, void *table, ULONG size)
{
    SatJumpTable full;
    if (!sat_sdl_bind(sdl, sdl_size, sdl_a4)) {
        return 0;
    }
#define SAT_PROC(rc, fn, params, args, ret) full.fn = fn;
#include SAT_PROCS
#undef SAT_PROC
    if (size > sizeof(full)) {
        size = sizeof(full);
    }
    memcpy(table, &full, size);
    return size;
}

static void sat_close(LONG sdl_alive)
{
    if (sdl_alive && sat_sdl_bound()) {
        SAT_CLOSE_WITH_SDL
    }
    SAT_CLOSE_ALWAYS
    sat_sdl_unbind();
    ogpu_module_end();
}

static struct SatModuleTable sat_module_table = {
    { SAT_MODULE_VERSION, 0, 0 },
    sat_bind,
    sat_close,
};

struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
{
    if (!ogpu_module_begin(args, SAT_MODULE_VERSION, &sat_module_table.head)) {
        return NULL;
    }
    return &sat_module_table.head;
}

void *ogpu_sat_callin(struct SatCallin *c, void *fn, void *arg)
{
    c->a4 = ogpu_module_a4();
    c->fn = fn;
    c->arg = arg;
    return c;
}

#endif
