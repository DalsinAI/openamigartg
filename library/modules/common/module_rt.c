/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A module's stand-in for libnix's program startup (module_rt.h). It is the
 * same work as SDL2.module's sdl2_module.c, shared for the Team's modules:
 *   - SysBase and DOSBase from the caller;
 *   - libnix's init list on open and exit list on close, run as its startup
 *     runs them;
 *   - exit() can't end the program from inside a module: while the module
 *     starts it fails the open, and afterwards it waits for ever rather than
 *     run on broken;
 *   - getenv() through dos.library's GetVar, as libnix's doesn't work in a
 *     module. Values are kept until the module closes, as callers keep the
 *     pointers getenv gives. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "module_rt.h"

/* libnix's globals, which its startup code would set. */
struct ExecBase *SysBase;
/* DOSBase: libnix defines it (proto/dos.h declares it). */
struct Library *ogpu_module_OpenGPUBase;

/* libnix's set lists (module_start.s holds their heads). */
extern long __INIT_LIST__[], __EXIT_LIST__[];

static jmp_buf failed;
static int starting;
static unsigned long level;                 /* how far the lists have run */

/* libnix's callfuncs, as its startup code runs it: the list's functions in
 * order of priority, rising for the init list (dir ~0) and falling, from
 * where init stopped, for the exit list (dir 0). */
static void callfuncs(long *list, unsigned long dir)
{
    for (;;) {
        unsigned long next = 0, cur = level ^ dir, pri;
        long *p = list + 1;
        void (*fn)(void);
        while ((fn = (void (*)(void))*p++) != NULL) {
            pri = (unsigned long)*p++;
            if (pri == level) fn();
            pri ^= dir;
            if (pri < cur && pri > next) next = pri;
        }
        next ^= dir;
        level = next;
        if (next == dir) break;
    }
}

void exit(int rc)
{
    (void)rc;
    if (starting) longjmp(failed, 1);
    Wait(0);
    for (;;) {
    }
}

void _exit(int rc)
{
    exit(rc);
}

/* ---- getenv ---------------------------------------------------------------------- */

struct var {
    struct var *next;
    char *value;                            /* NULL: not set when last asked */
    ULONG room;                             /* value's size */
    void **rooms;                           /* every room it has had, linked by their first word */
    char name[1];
};
static struct var *vars;
static struct SignalSemaphore vars_lock;

char *getenv(const char *name)
{
    char buf[512];
    LONG n;
    struct var *v;
    char *r = NULL;
    if (!name || !DOSBase) return NULL;
    n = GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof buf, 0);
    ObtainSemaphore(&vars_lock);
    for (v = vars; v && strcmp(v->name, name); v = v->next) {
    }
    if (!v && (v = AllocVec(sizeof *v + strlen(name), MEMF_ANY | MEMF_CLEAR)) != NULL) {
        strcpy(v->name, name);
        v->next = vars;
        vars = v;
    }
    if (v && n >= 0) {
        /* A longer value than before gets new room; the old stays, as a
         * caller may still hold it. */
        if ((ULONG)n + 1 > v->room) {
            void **nv = AllocVec(sizeof(void *) + (ULONG)n + 1, MEMF_ANY);
            if (nv) {
                nv[0] = v->rooms;
                v->rooms = nv;
                v->value = (char *)(nv + 1);
                v->room = (ULONG)n + 1;
            }
        }
        if ((ULONG)n + 1 <= v->room) {
            memcpy(v->value, buf, (size_t)n + 1);
            r = v->value;
        }
    }
    ReleaseSemaphore(&vars_lock);
    return r;
}

/* ---- open and close -------------------------------------------------------------- */

int ogpu_module_begin(const struct OGPUModuleArgs *args, ULONG version)
{
    if (!args || args->version > version) return 0;
    SysBase = args->SysBase;
    DOSBase = (struct DosLibrary *)args->DOSBase;
    ogpu_module_OpenGPUBase = args->OpenGPUBase;
    InitSemaphore(&vars_lock);
    vars = NULL;
    starting = 1;
    if (setjmp(failed)) {
        starting = 0;
        return 0;
    }
    level = 0;
    callfuncs(__INIT_LIST__, ~0UL);
    starting = 0;
    return 1;
}

void ogpu_module_end(void)
{
    callfuncs(__EXIT_LIST__, 0);
    while (vars) {
        struct var *v = vars;
        vars = v->next;
        while (v->rooms) {
            void **r = v->rooms;
            v->rooms = r[0];
            FreeVec(r);
        }
        FreeVec(v);
    }
}
