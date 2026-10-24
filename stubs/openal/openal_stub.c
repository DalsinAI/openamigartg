/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libopenal.a: OpenAL 1.1 for programs on AmigaOS 3.2, from OpenGPU's
 * OpenAL.module. Programs include <AL/al.h> and <AL/alc.h> as on any other
 * system and link -lopenal.
 *
 * Every AL and ALC call here jumps through a table (struct OpenALProcs,
 * openal_procs.h's order). The first call loads OpenAL.module
 * (LIBS:OpenGPU/) and asks it to fill the table with its own calls. The
 * table only grows at its end, so a program built today runs on any later
 * module. This is how libSDL3.a reaches SDL3.module (stubs/sdl3).
 *
 * The module's code is loaded once for every program, and each program
 * gets its own copy of the module's data: the module is built -fbaserel32,
 * and every call here sets A4 to the program's copy (the table's a4) and
 * puts the program's A4 back afterwards. This file is built with
 * -ffixed-a4 for that (include/opengpu/module.h, OGPU_A4). opengpu.library
 * (0.5 and later) loads the module with OGPU_ModuleOpen; on an older one
 * the stub LoadSegs the module itself, which is then this program's copy.
 *
 * Without the module a program still runs, with no sound: the first call
 * says once on its Output that the module isn't installed, and every call
 * then does nothing and returns 0 (alcOpenDevice NULL, "no sound driver/
 * device has been found", section 6.1.1), as a program that finds no
 * sound device expects. (libSDL3.a ends the program instead: SDL can't
 * draw without its module, but a game can play without sound.)
 *
 * alGetProcAddress and alcGetProcAddress give the stub's own entry for a
 * call, which sets A4, never the module's function itself. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "openal_module.h"

/* opengpu.library's calls through the stub's own base, so a program's
   OpenGPUBase is left alone. */
#define OPENGPU_BASE_NAME OpenALStub_OpenGPUBase
#include <inline/opengpu.h>
static struct Library *OpenALStub_OpenGPUBase = NULL;
static APTR OpenALStub_handle = NULL;
static BPTR OpenALStub_seg = 0;
static struct OpenALModuleTable *OpenALStub_module = NULL;

struct OpenALProcs OpenALStub_procs;
int OpenALStub_ready = 0;
int OpenALStub_failed = 0;
APTR OpenALStub_a4 = NULL;

/* The module couldn't be loaded: said once, and every call does nothing. */
static void OpenALStub_Fail(const char *why)
{
    BPTR out = Output();
    if (out) {
        FPuts(out, (CONST_STRPTR) "OpenAL: ");
        FPuts(out, (CONST_STRPTR)why);
        FPuts(out, (CONST_STRPTR) "\n");
    }
    if (OpenALStub_handle) {
        OGPU_ModuleClose(OpenALStub_handle);
        OpenALStub_handle = NULL;
    }
    if (OpenALStub_seg) {
        UnLoadSeg(OpenALStub_seg);
        OpenALStub_seg = 0;
    }
    OpenALStub_module = NULL;
    OpenALStub_failed = 1;
    OpenALStub_ready = 1;
}

static LONG OpenALStub_Fill(void)
{
    OGPU_A4(OpenALStub_a4);
    return OpenALStub_module->fill(OPENAL_PROCS_VERSION, &OpenALStub_procs, sizeof(OpenALStub_procs));
}

void OpenALStub_Init(void);
void OpenALStub_Init(void)
{
    OpenALModuleArgs args;
    OpenALModuleEntry entry;

    if (OpenALStub_ready) {
        return;
    }
    OpenALStub_OpenGPUBase = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
    if (OpenALStub_OpenGPUBase && (OpenALStub_OpenGPUBase->lib_Version > 0 ||
                                   OpenALStub_OpenGPUBase->lib_Revision >= OGPU_MODULE_LIB_REVISION)) {
        OpenALStub_handle = OGPU_ModuleOpen((CONST_STRPTR)OPENAL_MODULE_NAME, OPENAL_MODULE_OPEN_VERSION,
                                            (APTR *)&OpenALStub_module);
        if (!OpenALStub_handle && IoErr() == ERROR_OBJECT_NOT_FOUND) {
            OpenALStub_Fail("this program's sound needs " OPENAL_MODULE_FILE " (OpenGPU), which isn't installed.");
            return;
        }
    } else {
        OpenALStub_seg = LoadSeg((CONST_STRPTR)OPENAL_MODULE_FILE);
        if (!OpenALStub_seg) {
            OpenALStub_Fail("this program's sound needs " OPENAL_MODULE_FILE " (OpenGPU), which isn't installed.");
            return;
        }
        entry = (OpenALModuleEntry)((UBYTE *)BADDR(OpenALStub_seg) + 4);
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = OpenALStub_OpenGPUBase;
        args.version = OPENAL_MODULE_OPEN_VERSION;
        OpenALStub_module = (struct OpenALModuleTable *)entry(&args);
    }
    if (!OpenALStub_module) {
        OpenALStub_Fail(OPENAL_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
        return;
    }
    OpenALStub_module->head.caller_a4 = ogpu_a4_get();
    OpenALStub_a4 = OpenALStub_module->head.a4;
    if (OpenALStub_Fill() < 0) {
        {
            OGPU_A4(OpenALStub_a4);
            OpenALStub_module->close();
        }
        OpenALStub_Fail(OPENAL_MODULE_FILE " is older than this program. Install a newer OpenGPU.");
        return;
    }
    OpenALStub_ready = 1;
}

static void __attribute__((destructor)) OpenALStub_Close(void)
{
    if (OpenALStub_module) {
        OGPU_A4(OpenALStub_a4);
        OpenALStub_module->close();
        OpenALStub_module = NULL;
    }
    if (OpenALStub_handle) {
        OGPU_ModuleClose(OpenALStub_handle);
        OpenALStub_handle = NULL;
    }
    if (OpenALStub_seg) {
        UnLoadSeg(OpenALStub_seg);
        OpenALStub_seg = 0;
    }
    if (OpenALStub_OpenGPUBase) {
        CloseLibrary(OpenALStub_OpenGPUBase);
        OpenALStub_OpenGPUBase = NULL;
    }
    OpenALStub_ready = OpenALStub_failed;   /* after a failure, later calls still do nothing */
}

#define OPENAL_STUB_ENTER(zero)            \
    do {                                   \
        if (!OpenALStub_ready) {           \
            OpenALStub_Init();             \
        }                                  \
        if (OpenALStub_failed || !OpenALStub_module) { \
            return zero;                   \
        }                                  \
    } while (0)

/* Every call but the two GetProcAddress. */
#define OPENAL_PROC(rc, fn, params, args)        \
    rc AL_APIENTRY fn params                     \
    {                                            \
        OPENAL_STUB_ENTER((rc)0);                \
        {                                        \
            OGPU_A4(OpenALStub_a4);              \
            return OpenALStub_procs.fn args;     \
        }                                        \
    }
#define OPENAL_PROCV(fn, params, args)           \
    void AL_APIENTRY fn params                   \
    {                                            \
        OPENAL_STUB_ENTER();                     \
        {                                        \
            OGPU_A4(OpenALStub_a4);              \
            OpenALStub_procs.fn args;            \
        }                                        \
    }
#define OPENAL_PROC_OWN(rc, fn, params, args)
#include "openal_procs.h"
#undef OPENAL_PROC
#undef OPENAL_PROCV
#undef OPENAL_PROC_OWN

/* The stub's own entries by name. */
struct OpenALStub_name {
    const char *name;
    void *entry;
};
static const struct OpenALStub_name OpenALStub_names[] = {
#define OPENAL_PROC(rc, fn, params, args) { #fn, (void *)fn },
#define OPENAL_PROCV(fn, params, args) { #fn, (void *)fn },
#include "openal_procs.h"
#undef OPENAL_PROC
#undef OPENAL_PROCV
};

static void *OpenALStub_Entry(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof OpenALStub_names / sizeof OpenALStub_names[0]; i++) {
        if (!strcmp(OpenALStub_names[i].name, name)) {
            return OpenALStub_names[i].entry;
        }
    }
    return NULL;
}

/* The module says whether it has the call (and records the error for a
 * NULL name or a bad device); the program gets this stub's entry for it. */
void *AL_APIENTRY alGetProcAddress(const ALchar *fname)
{
    void *r;
    OPENAL_STUB_ENTER(NULL);
    {
        OGPU_A4(OpenALStub_a4);
        r = OpenALStub_procs.alGetProcAddress(fname);
    }
    return r ? OpenALStub_Entry(fname) : NULL;
}

void *AL_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcname)
{
    void *r;
    OPENAL_STUB_ENTER(NULL);
    {
        OGPU_A4(OpenALStub_a4);
        r = OpenALStub_procs.alcGetProcAddress(device, funcname);
    }
    return r ? OpenALStub_Entry(funcname) : NULL;
}
