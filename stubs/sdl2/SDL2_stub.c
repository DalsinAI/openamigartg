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

  The module's code is loaded once for every program, and each program
  gets its own copy of SDL's globals (the layout's residency step 2): the
  module is built -fbaserel32, and every call here sets A4 to the program's
  copy (the table's a4) and puts the program's A4 back afterwards. This
  file is built with -ffixed-a4 for that (include/opengpu/module.h,
  OGPU_A4). opengpu.library (0.5 and later) loads the module with
  OGPU_ModuleOpen; on an older one the stub LoadSegs the module itself,
  which is then this program's copy.
*/

#include "SDL2_stub.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>

/* opengpu.library's calls through the stub's own base, so a program's
   OpenGPUBase is left alone. */
#define OPENGPU_BASE_NAME SDL2Stub_OpenGPUBase
#include <exec/libraries.h>
#include <dos/dos.h>
#include <inline/opengpu.h>
static struct Library *SDL2Stub_OpenGPUBase = NULL;
static APTR SDL2Stub_handle = NULL;

#define SDL_DYNAPI_VERSION 1

SDL_DYNAPI_jump_table SDL2Stub_jump_table;
int SDL2Stub_ready = 0;
APTR SDL2Stub_a4 = NULL;
const struct SDL2GLBridge *SDL2Stub_gl = NULL;
static BPTR SDL2Stub_seg = 0;
static struct SDL2ModuleTable *SDL2Stub_module = NULL;
static struct SDL2StubCloseHook *SDL2Stub_hooks = NULL;

void SDL2Stub_AtClose(struct SDL2StubCloseHook *hook)
{
    hook->next = SDL2Stub_hooks;
    SDL2Stub_hooks = hook;
}

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

static LONG SDL2Stub_DynapiEntry(void)
{
    SDL2STUB_A4();
    return SDL2Stub_module->dynapi_entry(SDL_DYNAPI_VERSION, &SDL2Stub_jump_table, sizeof(SDL2Stub_jump_table));
}

/* The program's GL, for SDL_GL_*: only a module with set_gl (version 3)
   takes it; an older one has no OpenGL and says so. */
static void SDL2Stub_SetGL(void)
{
    if (SDL2Stub_gl && SDL2Stub_module->head.version >= SDL2_MODULE_GL_VERSION) {
        SDL2STUB_A4();
        SDL2Stub_module->set_gl(SDL2Stub_gl);
    }
}

void SDL2Stub_Init(void)
{
    SDL2ModuleArgs args;
    SDL2ModuleEntry entry;

    /* The first SDL call comes from the program's main task (SDL_Init or
       SDL_SetMainReady), before any SDL thread exists. */
    if (SDL2Stub_ready) {
        return;
    }
    SDL2Stub_OpenGPUBase = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
    if (SDL2Stub_OpenGPUBase && (SDL2Stub_OpenGPUBase->lib_Version > 0 ||
                                 SDL2Stub_OpenGPUBase->lib_Revision >= OGPU_MODULE_LIB_REVISION)) {
        SDL2Stub_handle = OGPU_ModuleOpen((CONST_STRPTR)SDL2_MODULE_NAME, SDL2_MODULE_OPEN_VERSION, (APTR *)&SDL2Stub_module);
        if (!SDL2Stub_handle && IoErr() == ERROR_OBJECT_NOT_FOUND) {
            SDL2Stub_Fail("this program needs " SDL2_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
    } else {
        SDL2Stub_seg = LoadSeg((CONST_STRPTR)SDL2_MODULE_FILE);
        if (!SDL2Stub_seg) {
            SDL2Stub_Fail("this program needs " SDL2_MODULE_FILE " (OpenGPU), which isn't installed.");
        }
        entry = (SDL2ModuleEntry)((UBYTE *)BADDR(SDL2Stub_seg) + 4);
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = SDL2Stub_OpenGPUBase;
        args.version = SDL2_MODULE_OPEN_VERSION;
        SDL2Stub_module = (struct SDL2ModuleTable *)entry(&args);
    }
    if (!SDL2Stub_module) {
        SDL2Stub_Fail(SDL2_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    /* The module's calls back into this program (audio, timers, threads)
       run with this program's A4. */
    SDL2Stub_module->head.caller_a4 = ogpu_a4_get();
    SDL2Stub_a4 = SDL2Stub_module->head.a4;
    if (SDL2Stub_DynapiEntry() < 0) {
        SDL2Stub_Fail(SDL2_MODULE_FILE " is older than this program, or couldn't start. Install a newer OpenGPU.");
    }
    SDL2Stub_SetGL();
    SDL2Stub_ready = 1;
}

static void __attribute__((destructor)) SDL2Stub_Close(void)
{
    /* The satellites first (SDL2_mixer closes its audio device in SDL). */
    while (SDL2Stub_hooks) {
        struct SDL2StubCloseHook *h = SDL2Stub_hooks;
        SDL2Stub_hooks = h->next;
        h->close();
    }
    if (SDL2Stub_module) {
        SDL2STUB_A4();
        SDL2Stub_module->close();
        SDL2Stub_module = NULL;
    }
    if (SDL2Stub_handle) {
        OGPU_ModuleClose(SDL2Stub_handle);
        SDL2Stub_handle = NULL;
    }
    if (SDL2Stub_seg) {
        UnLoadSeg(SDL2Stub_seg);
        SDL2Stub_seg = 0;
    }
    if (SDL2Stub_OpenGPUBase) {
        CloseLibrary(SDL2Stub_OpenGPUBase);
        SDL2Stub_OpenGPUBase = NULL;
    }
    SDL2Stub_ready = 0;
}

/* Every function except the varargs ones and the window GL calls
   (SDL2_gl.c). */
#define SDL_DYNAPI_PROC SDL2STUB_PROC
#define SDL_DYNAPI_PROC_NO_VARARGS
#include "SDL2_stub_procs.h"
#undef SDL_DYNAPI_PROC
#undef SDL_DYNAPI_PROC_NO_VARARGS

/* The varargs functions go through their va_list versions, as in SDL. */
#define SDL2STUB_LOGFN(logname, prio)                                                         \
    void SDLCALL SDL_Log##logname(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) \
    {                                                                                         \
        va_list ap;                                                                           \
        SDL2STUB_INIT();                                                                      \
        va_start(ap, fmt);                                                                    \
        {                                                                                     \
            SDL2STUB_A4();                                                                    \
            SDL2Stub_jump_table.SDL_LogMessageV(category, SDL_LOG_PRIORITY_##prio, fmt, ap);           \
        }                                                                                     \
        va_end(ap);                                                                           \
    }

int SDLCALL SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    char buf[128], *str = buf;
    int result;
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        result = SDL2Stub_jump_table.SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (result >= 0 && (size_t)result >= sizeof(buf)) {
            size_t len = (size_t)result + 1;
            str = (char *)SDL2Stub_jump_table.SDL_malloc(len);
            if (str) {
                va_start(ap, fmt);
                result = SDL2Stub_jump_table.SDL_vsnprintf(str, len, fmt, ap);
                va_end(ap);
            }
        }
        if (result >= 0) {
            result = SDL2Stub_jump_table.SDL_SetError("%s", str);
        }
        if (str != buf) {
            SDL2Stub_jump_table.SDL_free(str);
        }
        return result;
    }
}

int SDLCALL SDL_sscanf(const char *buf, SDL_SCANF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        retval = SDL2Stub_jump_table.SDL_vsscanf(buf, fmt, ap);
        va_end(ap);
        return retval;
    }
}

int SDLCALL SDL_snprintf(SDL_OUT_Z_CAP(maxlen) char *buf, size_t maxlen, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        retval = SDL2Stub_jump_table.SDL_vsnprintf(buf, maxlen, fmt, ap);
        va_end(ap);
        return retval;
    }
}

int SDLCALL SDL_asprintf(char **strp, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int retval;
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        retval = SDL2Stub_jump_table.SDL_vasprintf(strp, fmt, ap);
        va_end(ap);
        return retval;
    }
}

void SDLCALL SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        SDL2Stub_jump_table.SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
        va_end(ap);
    }
}

void SDLCALL SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    SDL2STUB_INIT();
    {
        SDL2STUB_A4();
        va_start(ap, fmt);
        SDL2Stub_jump_table.SDL_LogMessageV(category, priority, fmt, ap);
        va_end(ap);
    }
}

SDL2STUB_LOGFN(Verbose, VERBOSE)
SDL2STUB_LOGFN(Debug, DEBUG)
SDL2STUB_LOGFN(Info, INFO)
SDL2STUB_LOGFN(Warn, WARN)
SDL2STUB_LOGFN(Error, ERROR)
SDL2STUB_LOGFN(Critical, CRITICAL)
