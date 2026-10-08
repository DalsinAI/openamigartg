/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * CallCost: what one call through libGL.a or libSDL2.a costs, the stub's
 * way into the module (with A4 set and put back, since residency step 2)
 * and the module's function together. It times a call that does almost
 * nothing in the module, many times over:
 *   GL:  glGetError() with no context (Mesa returns at once), and
 *        glIsEnabled(GL_BLEND) (one argument);
 *   SDL: SDL_GetCPUCount() (kept once known) and SDL_GetPixelFormatName(n)
 *        (one argument).
 * Built against either stub, with the GCC 16 stove:
 *   m68k-amigaos-gcc -m68040 -m68881 -O2 -noixemul -DCALLCOST_GL -I<Mesa>/include
 *       -o CallCostGL call_cost.c -L<GL module build> -lGL
 *   m68k-amigaos-gcc -m68040 -m68881 -O2 -noixemul -DCALLCOST_SDL -I<build>/include/SDL2
 *       -o CallCostSDL call_cost.c -L<build> -lSDL2
 *   CallCost [COUNT n]            (default 200000) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

#ifdef CALLCOST_GL
#include <GL/gl.h>
#define WHAT "libGL.a"
#define CALL0() ((void)glGetError())
#define CALL1(i) ((void)glIsEnabled(GL_BLEND + ((i) & 0)))
#define NAME0 "glGetError()"
#define NAME1 "glIsEnabled(GL_BLEND)"
#else
#include "SDL.h"
#define WHAT "libSDL2.a"
#define CALL0() ((void)SDL_GetCPUCount())
#define CALL1(i) ((void)SDL_GetPixelFormatName((Uint32)(i) & 0xff))
#define NAME0 "SDL_GetCPUCount()"
#define NAME1 "SDL_GetPixelFormatName(n)"
#endif

struct Device *TimerBase;

static double now_us(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) * 1e6 / hz;
}

int main(int argc, char **argv)
{
    struct timerequest tr;
    long n = 200000, i;
    double t0, t1, t2;
    if (argc == 3 && !strcmp(argv[1], "COUNT")) n = atol(argv[2]);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &tr.tr_node, 0)) return 20;
    TimerBase = tr.tr_node.io_Device;
    CALL0();                                    /* loads the module */
    t0 = now_us();
    for (i = 0; i < n; i++) CALL0();
    t1 = now_us();
    for (i = 0; i < n; i++) CALL1(i);
    t2 = now_us();
    printf("CallCost (%s): %s %.3f us a call, %s %.3f us a call (%ld calls each)\n", WHAT,
           NAME0, (t1 - t0) / n, NAME1, (t2 - t1) / n, n);
    CloseDevice(&tr.tr_node);
    return 0;
}
