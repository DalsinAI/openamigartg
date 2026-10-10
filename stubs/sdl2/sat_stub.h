/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The link stub of a satellite module (sat_module.h): libSDL2_image.a and
 * libSDL2_mixer.a are each this file, included once with
 *   SAT_NAME    the module's name ("SDL2_image": LIBS:OpenGPU/SDL2_image.module)
 *   SAT_PROCS   its ABI file ("SDL_image_procs.h")
 * and the library's header included first. Every function jumps through a
 * table the module fills on the first call, with the module's A4 set for
 * the call and the program's put back after (OGPU_A4; built -ffixed-a4, as
 * libSDL2.a is). The first call also starts SDL (libSDL2.a), whose jump
 * table the module then calls SDL through. */
#ifndef SAT_STUB_H
#define SAT_STUB_H

#include "SDL2_stub.h"
#include "sat_module.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdlib.h>

#define OPENGPU_BASE_NAME sat_OpenGPUBase
#include <exec/libraries.h>
#include <inline/opengpu.h>

#define SAT_FILE OGPU_MODULE_DIR SAT_NAME OGPU_MODULE_EXT

typedef struct {
#define SAT_PROC(rc, fn, params, args, ret) rc (SDLCALL *fn) params;
#include SAT_PROCS
#undef SAT_PROC
} SatJumpTable;

static SatJumpTable sat_table;
static int sat_ready = 0;
static APTR sat_a4 = NULL;
static struct Library *sat_OpenGPUBase = NULL;
static APTR sat_handle = NULL;
static BPTR sat_seg = 0;
static struct SatModuleTable *sat_module = NULL;
static struct SDL2StubCloseHook sat_hook;

static void sat_fail(const char *why)
{
    BPTR out = Output();
    if (out) {
        FPuts(out, (CONST_STRPTR)SAT_NAME ": ");
        FPuts(out, (CONST_STRPTR)why);
        FPuts(out, (CONST_STRPTR) "\n");
    }
    exit(20);
}

static void sat_close(void)
{
    if (sat_module) {
        LONG alive = SDL2Stub_ready;
        OGPU_A4(sat_a4);
        sat_module->close(alive);
    }
    sat_module = NULL;
    sat_ready = 0;
    if (sat_handle) {
        OGPU_ModuleClose(sat_handle);
        sat_handle = NULL;
    }
    if (sat_seg) {
        UnLoadSeg(sat_seg);
        sat_seg = 0;
    }
    if (sat_OpenGPUBase) {
        CloseLibrary(sat_OpenGPUBase);
        sat_OpenGPUBase = NULL;
    }
}

static void sat_init(void)
{
    ULONG filled;
    const void *sdl_table = &SDL2Stub_jump_table;
    APTR sdl_a4;

    SDL2STUB_INIT();
    sdl_a4 = SDL2Stub_a4;
    sat_OpenGPUBase = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
    if (sat_OpenGPUBase && (sat_OpenGPUBase->lib_Version > 0 ||
                            sat_OpenGPUBase->lib_Revision >= OGPU_MODULE_LIB_REVISION)) {
        sat_handle = OGPU_ModuleOpen((CONST_STRPTR)SAT_NAME, SAT_MODULE_OPEN_VERSION, (APTR *)&sat_module);
        if (!sat_handle && IoErr() == ERROR_OBJECT_NOT_FOUND) {
            sat_fail("this program needs " SAT_FILE " (OpenGPU), which isn't installed.");
        }
    } else {
        /* An opengpu.library older than 0.5: the module is this program's own. */
        struct OGPUModuleArgs args;
        sat_seg = LoadSeg((CONST_STRPTR)SAT_FILE);
        if (!sat_seg) {
            sat_fail("this program needs " SAT_FILE " (OpenGPU), which isn't installed.");
        }
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = sat_OpenGPUBase;
        args.version = SAT_MODULE_OPEN_VERSION;
        sat_module = (struct SatModuleTable *)OGPU_MODULE_ENTRY(sat_seg)(&args);
    }
    if (!sat_module) {
        sat_fail(SAT_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    sat_module->head.caller_a4 = ogpu_a4_get();
    sat_a4 = sat_module->head.a4;
    {
        OGPU_A4(sat_a4);
        filled = sat_module->bind(sdl_table, sizeof(SDL2Stub_jump_table), sdl_a4, &sat_table, sizeof(sat_table));
    }
    if (filled < sizeof(sat_table)) {
        sat_fail(SAT_FILE " is older than this program. Install a newer OpenGPU.");
    }
    sat_hook.close = sat_close;
    SDL2Stub_AtClose(&sat_hook);
    sat_ready = 1;
}

/* When the program's destructors reach this one before libSDL2.a's. */
static void __attribute__((destructor)) sat_destructor(void)
{
    sat_close();
}

#define SAT_PROC(rc, fn, params, args, ret)  \
    rc SDLCALL fn params                     \
    {                                        \
        if (!sat_ready) {                    \
            sat_init();                      \
        }                                    \
        {                                    \
            OGPU_A4(sat_a4);                 \
            ret sat_table.fn args;           \
        }                                    \
    }
#include SAT_PROCS
#undef SAT_PROC

#endif
