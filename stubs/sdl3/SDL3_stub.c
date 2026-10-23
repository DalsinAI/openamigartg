/*
  SDL 3's link library for AmigaOS 3.2 (Open): libSDL3.a, for programs that
  use SDL 3 from OpenGPU's SDL3.module.

  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
  Altered for AmigaOS 3.x by the AmigaChrome Team, 2026: this is SDL's dynamic API
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
  call loads SDL3.module (SDL 3 with the Amiga back ends, LIBS:OpenGPU/) and
  calls its SDL_DYNAPI_entry, which fills the table with the module's own
  functions. The table's layout is SDL_dynapi_procs.h, the same file the
  module is built from, so a program built against these headers runs on
  any later module: newer modules only add to the end. Programs rebuild
  unchanged and link -lSDL3 as on any other system.

  The module's code is loaded once for every program, and each program
  gets its own copy of SDL's globals (the layout's residency step 2): the
  module is built -fbaserel32, and every call here sets A4 to the program's
  copy (the table's a4) and puts the program's A4 back afterwards. This
  file is built with -ffixed-a4 for that (include/opengpu/module.h,
  OGPU_A4). It works as SDL2.module's link library
  (stubs/sdl2) does; SDL 3's dynamic API is its version 2.
  opengpu.library (0.5 and later) loads the module with
  OGPU_ModuleOpen; on an older one the stub LoadSegs the module itself,
  which is then this program's copy.
*/

#include "SDL3_stub.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>

/* opengpu.library's calls through the stub's own base, so a program's
   OpenGPUBase is left alone. */
#define OPENGPU_BASE_NAME SDL3Stub_OpenGPUBase
#include <exec/libraries.h>
#include <dos/dos.h>
#include <inline/opengpu.h>
static struct Library *SDL3Stub_OpenGPUBase = NULL;
static APTR SDL3Stub_handle = NULL;

#define SDL_DYNAPI_VERSION 2

SDL_DYNAPI_jump_table SDL3Stub_jump_table;
int SDL3Stub_ready = 0;
APTR SDL3Stub_a4 = NULL;
static BPTR SDL3Stub_seg = 0;
static struct SDL3ModuleTable *SDL3Stub_module = NULL;

static void SDL3Stub_Fail(const char *why)
{
    BPTR out = Output();
    if (out) {
        FPuts(out, (CONST_STRPTR) "SDL 3: ");
        FPuts(out, (CONST_STRPTR)why);
        FPuts(out, (CONST_STRPTR) "\n");
    }
    exit(20);
}

static LONG SDL3Stub_DynapiEntry(void)
{
    SDL3STUB_A4();
    return SDL3Stub_module->dynapi_entry(SDL_DYNAPI_VERSION, &SDL3Stub_jump_table, sizeof(SDL3Stub_jump_table));
}

void SDL3Stub_Init(void)
{
    SDL3ModuleArgs args;
    SDL3ModuleEntry entry;

    /* The first SDL call comes from the program's main task, before any
       SDL thread exists. */
    if (SDL3Stub_ready) {
        return;
    }
    SDL3Stub_OpenGPUBase = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
    if (SDL3Stub_OpenGPUBase && (SDL3Stub_OpenGPUBase->lib_Version > 0 ||
                                 SDL3Stub_OpenGPUBase->lib_Revision >= OGPU_MODULE_LIB_REVISION)) {
        SDL3Stub_handle = OGPU_ModuleOpen((CONST_STRPTR)SDL3_MODULE_NAME, SDL3_MODULE_OPEN_VERSION, (APTR *)&SDL3Stub_module);
        if (!SDL3Stub_handle && IoErr() == ERROR_OBJECT_NOT_FOUND) {
            SDL3Stub_Fail("this program needs " SDL3_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
    } else {
        SDL3Stub_seg = LoadSeg((CONST_STRPTR)SDL3_MODULE_FILE);
        if (!SDL3Stub_seg) {
            SDL3Stub_Fail("this program needs " SDL3_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
        entry = (SDL3ModuleEntry)((UBYTE *)BADDR(SDL3Stub_seg) + 4);
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = SDL3Stub_OpenGPUBase;
        args.version = SDL3_MODULE_OPEN_VERSION;
        SDL3Stub_module = (struct SDL3ModuleTable *)entry(&args);
    }
    if (!SDL3Stub_module) {
        SDL3Stub_Fail(SDL3_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    /* The module's calls back into this program (audio, timers, threads)
       run with this program's A4. */
    SDL3Stub_module->head.caller_a4 = ogpu_a4_get();
    SDL3Stub_a4 = SDL3Stub_module->head.a4;
    if (SDL3Stub_DynapiEntry() < 0) {
        SDL3Stub_Fail(SDL3_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    SDL3Stub_ready = 1;
}

static void __attribute__((destructor)) SDL3Stub_Close(void)
{
    if (SDL3Stub_module) {
        SDL3STUB_A4();
        SDL3Stub_module->close();
        SDL3Stub_module = NULL;
    }
    if (SDL3Stub_handle) {
        OGPU_ModuleClose(SDL3Stub_handle);
        SDL3Stub_handle = NULL;
    }
    if (SDL3Stub_seg) {
        UnLoadSeg(SDL3Stub_seg);
        SDL3Stub_seg = 0;
    }
    if (SDL3Stub_OpenGPUBase) {
        CloseLibrary(SDL3Stub_OpenGPUBase);
        SDL3Stub_OpenGPUBase = NULL;
    }
    SDL3Stub_ready = 0;
}

/* Every function except the varargs ones. */
#define SDL_DYNAPI_PROC SDL3STUB_PROC
#define SDL_DYNAPI_PROC_NO_VARARGS
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
#undef SDL_DYNAPI_PROC_NO_VARARGS

/* The varargs functions go through their va_list versions, as in SDL
   (SDL_dynapi.c's SDL_DYNAPI_VARARGS). */
#define SDL3STUB_LOGFN(logname, prio)                                                          \
    void SDLCALL SDL_Log##logname(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) \
    {                                                                                          \
        va_list ap;                                                                            \
        SDL3STUB_INIT();                                                                       \
        {                                                                                      \
            SDL3STUB_A4();                                                                     \
            va_start(ap, fmt);                                                                 \
            SDL3Stub_jump_table.SDL_LogMessageV(category, SDL_LOG_PRIORITY_##prio, fmt, ap);   \
            va_end(ap);                                                                        \
        }                                                                                      \
    }

/* fmt formatted into a string of the module's (SDL_free it), or NULL. */
static char *SDL3Stub_Format(char *buf, size_t size, const char *fmt, va_list ap)
{
    va_list ap2;
    int result;
    char *str = buf;

    va_copy(ap2, ap);
    result = SDL3Stub_jump_table.SDL_vsnprintf(buf, size, fmt, ap);
    if (result >= 0 && (size_t)result >= size) {
        str = NULL;
        result = SDL3Stub_jump_table.SDL_vasprintf(&str, fmt, ap2);
    }
    va_end(ap2);
    return result >= 0 ? str : NULL;
}

bool SDLCALL SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    char buf[128], *str;
    va_list ap;
    SDL3STUB_INIT();
    {
        SDL3STUB_A4();
        va_start(ap, fmt);
        str = SDL3Stub_Format(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (str) {
            SDL3Stub_jump_table.SDL_SetError("%s", str);
            if (str != buf) {
                SDL3Stub_jump_table.SDL_free(str);
            }
        }
        return false;
    }
}

bool SDLCALL SDL_RenderDebugTextFormat(SDL_Renderer *renderer, float x, float y, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    char buf[128], *str;
    bool retval = false;
    va_list ap;
    SDL3STUB_INIT();
    {
        SDL3STUB_A4();
        va_start(ap, fmt);
        str = SDL3Stub_Format(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (str) {
            retval = SDL3Stub_jump_table.SDL_RenderDebugTextFormat(renderer, x, y, "%s", str);
            if (str != buf) {
                SDL3Stub_jump_table.SDL_free(str);
            }
        }
        return retval;
    }
}

/* A varargs function that is its va_list version with the list. */
#define SDL3STUB_VFN(rc, fn, params, vfn, args)  \
    rc SDLCALL fn params                         \
    {                                            \
        rc retval;                               \
        va_list ap;                              \
        SDL3STUB_INIT();                         \
        {                                        \
            SDL3STUB_A4();                       \
            va_start(ap, fmt);                   \
            retval = SDL3Stub_jump_table.vfn args; \
            va_end(ap);                          \
            return retval;                       \
        }                                        \
    }

SDL3STUB_VFN(int, SDL_sscanf, (const char *buf, SDL_SCANF_FORMAT_STRING const char *fmt, ...), SDL_vsscanf, (buf, fmt, ap))
SDL3STUB_VFN(int, SDL_snprintf, (SDL_OUT_Z_CAP(maxlen) char *buf, size_t maxlen, SDL_PRINTF_FORMAT_STRING const char *fmt, ...), SDL_vsnprintf, (buf, maxlen, fmt, ap))
SDL3STUB_VFN(int, SDL_swprintf, (SDL_OUT_Z_CAP(maxlen) wchar_t *buf, size_t maxlen, SDL_PRINTF_FORMAT_STRING const wchar_t *fmt, ...), SDL_vswprintf, (buf, maxlen, fmt, ap))
SDL3STUB_VFN(int, SDL_asprintf, (char **strp, SDL_PRINTF_FORMAT_STRING const char *fmt, ...), SDL_vasprintf, (strp, fmt, ap))
SDL3STUB_VFN(size_t, SDL_IOprintf, (SDL_IOStream *context, SDL_PRINTF_FORMAT_STRING const char *fmt, ...), SDL_IOvprintf, (context, fmt, ap))

void SDLCALL SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL3STUB_INIT();
    {
        SDL3STUB_A4();
        va_start(ap, fmt);
        SDL3Stub_jump_table.SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
        va_end(ap);
    }
}

void SDLCALL SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL3STUB_INIT();
    {
        SDL3STUB_A4();
        va_start(ap, fmt);
        SDL3Stub_jump_table.SDL_LogMessageV(category, priority, fmt, ap);
        va_end(ap);
    }
}

SDL3STUB_LOGFN(Trace, TRACE)
SDL3STUB_LOGFN(Verbose, VERBOSE)
SDL3STUB_LOGFN(Debug, DEBUG)
SDL3STUB_LOGFN(Info, INFO)
SDL3STUB_LOGFN(Warn, WARN)
SDL3STUB_LOGFN(Error, ERROR)
SDL3STUB_LOGFN(Critical, CRITICAL)
