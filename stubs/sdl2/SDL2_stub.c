/*
  SDL 2's link library for AmigaOS 3.2 (Open): libSDL2.a, for programs that
  use SDL 2 from OpenGPU's SDL2.module.

  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
  Altered for AmigaOS 3.x by Dalsin Limited, 2026: this is SDL's dynamic API
  (src/dynapi/SDL_dynapi.c) cut down to its caller's half. It is not the
  original software.

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  How it works: every SDL_ function here jumps through a table. The first
  call loads SDL2.module (SDL 2 with the Amiga back ends, LIBS:OpenGPU/) and
  calls its SDL_DYNAPI_entry, which fills the table with the module's own
  functions. The table's layout is SDL_dynapi_procs.h, the same file the
  module is built from, so a program built against these headers runs on
  any later module: newer modules only add to the end. Programs rebuild
  unchanged and link -lSDL2 as on any other system.

  Each program gets its own copy of the module (the layout's step 1), so
  SDL's globals are its own. opengpu.library (0.5 and later) loads it with
  OGPU_ModuleOpen; on an older one the stub LoadSegs the module itself.
*/

#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_vulkan.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>

#include "sdl2_module.h"

/* opengpu.library's calls through the stub's own base, so a program's
   OpenGPUBase is left alone. */
#define OPENGPU_BASE_NAME SDL2Stub_OpenGPUBase
#include <exec/libraries.h>
#include <dos/dos.h>
#include <inline/opengpu.h>
static struct Library *SDL2Stub_OpenGPUBase = NULL;
static APTR SDL2Stub_handle = NULL;

#define SDL_DYNAPI_VERSION 1

/* The jump table: one pointer per function, in SDL_dynapi_procs.h order. */
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) typedef rc (SDLCALL *SDL_DYNAPIFN_##fn) params;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC

typedef struct
{
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) SDL_DYNAPIFN_##fn fn;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
} SDL_DYNAPI_jump_table;

static SDL_DYNAPI_jump_table jump_table;
static int jump_table_ready = 0;
static BPTR SDL2Stub_seg = 0;
static struct SDL2ModuleTable *SDL2Stub_module = NULL;

static void SDL2Stub_Fail(const char *why)
{
    BPTR out = Output();
    if (out) {
        FPuts(out, (CONST_STRPTR) "SDL 2: ");
        FPuts(out, (CONST_STRPTR)why);
        FPuts(out, (CONST_STRPTR) "\n");
    }
    exit(20);
}

static void SDL2Stub_Init(void)
{
    SDL2ModuleArgs args;
    SDL2ModuleEntry entry;

    /* The first SDL call comes from the program's main task (SDL_Init or
       SDL_SetMainReady), before any SDL thread exists. */
    if (jump_table_ready) {
        return;
    }
    SDL2Stub_OpenGPUBase = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
#ifdef OPENGPU_MODULE_H
    if (SDL2Stub_OpenGPUBase && (SDL2Stub_OpenGPUBase->lib_Version > 0 ||
                                 SDL2Stub_OpenGPUBase->lib_Revision >= OGPU_MODULE_LIB_REVISION)) {
        SDL2Stub_handle = OGPU_ModuleOpen((CONST_STRPTR)SDL2_MODULE_NAME, SDL2_MODULE_VERSION, (APTR *)&SDL2Stub_module);
        if (!SDL2Stub_handle && IoErr() == ERROR_OBJECT_NOT_FOUND) {
            SDL2Stub_Fail("this program needs " SDL2_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
    } else
#endif
    {
        SDL2Stub_seg = LoadSeg((CONST_STRPTR)SDL2_MODULE_FILE);
        if (!SDL2Stub_seg) {
            SDL2Stub_Fail("this program needs " SDL2_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
        entry = (SDL2ModuleEntry)((UBYTE *)BADDR(SDL2Stub_seg) + 4);
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = SDL2Stub_OpenGPUBase;
        args.version = SDL2_MODULE_VERSION;
        SDL2Stub_module = entry(&args);
    }
    if (!SDL2Stub_module || SDL2Stub_module->dynapi_entry(SDL_DYNAPI_VERSION, &jump_table, sizeof(jump_table)) < 0) {
        SDL2Stub_Fail(SDL2_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    jump_table_ready = 1;
}

static void __attribute__((destructor)) SDL2Stub_Close(void)
{
    if (SDL2Stub_module) {
        SDL2Stub_module->close();
        SDL2Stub_module = NULL;
    }
#ifdef OPENGPU_MODULE_H
    if (SDL2Stub_handle) {
        OGPU_ModuleClose(SDL2Stub_handle);
        SDL2Stub_handle = NULL;
    }
#endif
    if (SDL2Stub_seg) {
        UnLoadSeg(SDL2Stub_seg);
        SDL2Stub_seg = 0;
    }
    if (SDL2Stub_OpenGPUBase) {
        CloseLibrary(SDL2Stub_OpenGPUBase);
        SDL2Stub_OpenGPUBase = NULL;
    }
    jump_table_ready = 0;
}

#define SDL2STUB_INIT() do { if (!jump_table_ready) SDL2Stub_Init(); } while (0)

/* Every function except the varargs ones. */
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) \
    rc SDLCALL fn params                           \
    {                                              \
        SDL2STUB_INIT();                           \
        ret jump_table.fn args;                    \
    }
#define SDL_DYNAPI_PROC_NO_VARARGS
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
#undef SDL_DYNAPI_PROC_NO_VARARGS

/* The varargs functions go through their va_list versions, as in SDL. */
#define SDL2STUB_LOGFN(logname, prio)                                                         \
    void SDLCALL SDL_Log##logname(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) \
    {                                                                                         \
        va_list ap;                                                                           \
        SDL2STUB_INIT();                                                                      \
        va_start(ap, fmt);                                                                    \
        jump_table.SDL_LogMessageV(category, SDL_LOG_PRIORITY_##prio, fmt, ap);               \
        va_end(ap);                                                                           \
    }

int SDLCALL SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    char buf[128], *str = buf;
    int result;
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    result = jump_table.SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (result >= 0 && (size_t)result >= sizeof(buf)) {
        size_t len = (size_t)result + 1;
        str = (char *)jump_table.SDL_malloc(len);
        if (str) {
            va_start(ap, fmt);
            result = jump_table.SDL_vsnprintf(str, len, fmt, ap);
            va_end(ap);
        }
    }
    if (result >= 0) {
        result = jump_table.SDL_SetError("%s", str);
    }
    if (str != buf) {
        jump_table.SDL_free(str);
    }
    return result;
}

int SDLCALL SDL_sscanf(const char *buf, SDL_SCANF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    retval = jump_table.SDL_vsscanf(buf, fmt, ap);
    va_end(ap);
    return retval;
}

int SDLCALL SDL_snprintf(SDL_OUT_Z_CAP(maxlen) char *buf, size_t maxlen, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    retval = jump_table.SDL_vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return retval;
}

int SDLCALL SDL_asprintf(char **strp, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    retval = jump_table.SDL_vasprintf(strp, fmt, ap);
    va_end(ap);
    return retval;
}

void SDLCALL SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    jump_table.SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void SDLCALL SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL2STUB_INIT();
    va_start(ap, fmt);
    jump_table.SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
}

SDL2STUB_LOGFN(Verbose, VERBOSE)
SDL2STUB_LOGFN(Debug, DEBUG)
SDL2STUB_LOGFN(Info, INFO)
SDL2STUB_LOGFN(Warn, WARN)
SDL2STUB_LOGFN(Error, ERROR)
SDL2STUB_LOGFN(Critical, CRITICAL)
