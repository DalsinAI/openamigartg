/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Inside a satellite module (stubs/sdl2/sat_module.h): its SDL calls, and
 * SDL's calls back into it. Built -fbaserel32 with the module's objects. */
#ifndef SAT_RT_H
#define SAT_RT_H

#include <exec/types.h>

/* The program's SDL (sat_sdl.c): its jump table, the table's size, and
 * SDL2.module's A4 for this program. 0 when the table is smaller than the
 * one the module was built against. */
int sat_sdl_bind(const void *table, ULONG size, APTR a4);
/* 1 once bound and until sat_sdl_unbind. */
int sat_sdl_bound(void);
void sat_sdl_unbind(void);

/* A call from SDL into this module (an audio callback): SDL passes the
 * callin as the callback's user data and ogpu_sat_callin3 as the callback;
 * ogpu_sat_callin3 sets this copy's A4 and calls fn(arg, b, c). In a
 * static build (no module) the callin is a plain call. */
struct SatCallin {
    APTR a4;
    void *fn;
    void *arg;
};
void *ogpu_sat_callin(struct SatCallin *c, void *fn, void *arg);
void ogpu_sat_callin3(struct SatCallin *c, long b, long d);

#endif
