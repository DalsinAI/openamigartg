/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module: SDL 2 (with the Amiga back ends) as a module that
 * opengpu.library, or the link stub, loads for a program. Its first code
 * (module_start.s) jumps to SDL2Module_Entry.
 *
 * Step 1 of the layout's residency plan: each program LoadSegs its own copy,
 * so SDL's globals are the program's own. The entry sets libnix up the way
 * its startup code would for a program (SysBase and DOSBase, then its init
 * list: memory, standard I/O on the calling program's Input and Output,
 * auto-opened libraries) and hands back SDL_DYNAPI_entry.
 *
 * For step 2 (code loaded once, -fbaserel32 data per program) nothing here
 * keeps an absolute address of its own data where code without A4 would
 * use it; SDL's threads start through SDL_CreateThread, the one place the
 * pthread shim will hand A4 on. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <setjmp.h>
#include <stdlib.h>

#include "sdl2_module.h"

/* SDL's dynamic API (src/dynapi/SDL_dynapi.c, built with
   SDL_OPEN_LIBRARY_BUILD). */
extern long SDL_DYNAPI_entry(unsigned long apiver, void *table, unsigned long tablesize);
extern void SDL_Quit(void);

/* libnix's globals, which its startup code would set. */
struct ExecBase *SysBase;
extern struct Library *DOSBase;

/* libnix's set lists (module_start.s holds their heads). */
extern long __INIT_LIST__[], __EXIT_LIST__[];

static jmp_buf SDL2Module_failed;
static int SDL2Module_starting;
static unsigned long SDL2Module_level; /* how far the lists have run */

/* libnix's callfuncs, as its startup code runs it: the list's functions in
   order of priority, rising for the init list (dir ~0) and falling, from
   where init stopped, for the exit list (dir 0). */
static void SDL2Module_CallFuncs(long *list, unsigned long dir)
{
    for (;;) {
        unsigned long next = 0, cur = SDL2Module_level ^ dir, pri;
        long *p = list + 1;
        void (*fn)(void);

        while ((fn = (void (*)(void))*p++) != NULL) {
            pri = (unsigned long)*p++;
            if (pri == SDL2Module_level) {
                fn();
            }
            pri ^= dir;
            if (pri < cur && pri > next) {
                next = pri;
            }
        }
        next ^= dir;
        SDL2Module_level = next;
        if (next == dir) {
            break;
        }
    }
}

/* libnix calls exit() when its start-up fails (no memory for standard
   I/O). A module can't end its program: during the entry it fails the
   open instead; later it waits for ever rather than run on broken. */
void exit(int rc)
{
    (void)rc;
    if (SDL2Module_starting) {
        longjmp(SDL2Module_failed, 1);
    }
    Wait(0);
    for (;;) {
    }
}

void _exit(int rc)
{
    exit(rc);
}

static void SDL2Module_Close(void)
{
    SDL_Quit();
    SDL2Module_CallFuncs(__EXIT_LIST__, 0);
}

static struct SDL2ModuleTable SDL2Module_table = {
    SDL2_MODULE_VERSION,
    (LONG (*)(ULONG, void *, ULONG))SDL_DYNAPI_entry,
    SDL2Module_Close,
};

struct SDL2ModuleTable *SDL2Module_Entry(SDL2ModuleArgs *args)
{
    if (!args || args->version > SDL2_MODULE_VERSION) {
        return NULL;
    }
    SysBase = args->SysBase;
    DOSBase = args->DOSBase;
    SDL2Module_starting = 1;
    if (setjmp(SDL2Module_failed)) {
        SDL2Module_starting = 0;
        return NULL;
    }
    SDL2Module_level = 0;
    SDL2Module_CallFuncs(__INIT_LIST__, ~0UL);
    SDL2Module_starting = 0;
    return &SDL2Module_table;
}
