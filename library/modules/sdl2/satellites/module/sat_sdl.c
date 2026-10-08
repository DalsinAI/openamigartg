/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A satellite module's SDL: every SDL_ function the satellite calls is
 * here, and jumps through the program's own SDL jump table (libSDL2.a's,
 * which SDL2.module filled) with SDL2.module's A4 for this program set for
 * the call. The table's layout is SDL's SDL_dynapi_procs.h, as libSDL2.a's
 * is (stubs/sdl2/SDL2_stub.h).
 *
 * The module is built -fbaserel32: its own globals (the table pointer and
 * SDL's A4) are reached through A4. So each function takes them into
 * locals before it sets SDL's A4, and puts this module's A4 back after
 * (OGPU_A4); the arguments are the function's own, never a global. */
#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_vulkan.h"

#include <exec/types.h>
#include <opengpu/module.h>
#include <stdarg.h>

#include "sat_rt.h"

#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) typedef rc (SDLCALL *SDL_DYNAPIFN_##fn) params;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC

typedef struct
{
#define SDL_DYNAPI_PROC(rc, fn, params, args, ret) SDL_DYNAPIFN_##fn fn;
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
} SDL_DYNAPI_jump_table;

static const SDL_DYNAPI_jump_table *sat_sdl = NULL;
static APTR sat_sdl_a4 = NULL;

int sat_sdl_bind(const void *table, ULONG size, APTR a4)
{
    if (!table || size < sizeof(SDL_DYNAPI_jump_table)) {
        return 0;
    }
    sat_sdl = (const SDL_DYNAPI_jump_table *)table;
    sat_sdl_a4 = a4;
    return 1;
}

int sat_sdl_bound(void)
{
    return sat_sdl != NULL;
}

void sat_sdl_unbind(void)
{
    sat_sdl = NULL;
}

#define SAT_SDL_PROC(rc, fn, params, args, ret)        \
    rc SDLCALL fn params                               \
    {                                                  \
        const SDL_DYNAPI_jump_table *t_ = sat_sdl;     \
        SDL_DYNAPIFN_##fn f_ = t_->fn;                 \
        APTR a4_ = sat_sdl_a4;                         \
        {                                              \
            OGPU_A4(a4_);                              \
            ret f_ args;                               \
        }                                              \
    }

#define SDL_DYNAPI_PROC SAT_SDL_PROC
#define SDL_DYNAPI_PROC_NO_VARARGS
#include "SDL_dynapi_procs.h"
#undef SDL_DYNAPI_PROC
#undef SDL_DYNAPI_PROC_NO_VARARGS

/* The varargs functions, through their va_list versions above. */
int SDLCALL SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    char buf[256];
    const SDL_DYNAPI_jump_table *t = sat_sdl;
    APTR a4 = sat_sdl_a4;
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    {
        OGPU_A4(a4);
        return t->SDL_SetError("%s", buf);
    }
}

void SDLCALL SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
}

void SDLCALL SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

#define SAT_SDL_LOGFN(name, prio)                                                     \
    void SDLCALL SDL_Log##name(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) \
    {                                                                                  \
        va_list ap;                                                                    \
        va_start(ap, fmt);                                                             \
        SDL_LogMessageV(category, SDL_LOG_PRIORITY_##prio, fmt, ap);                   \
        va_end(ap);                                                                    \
    }
SAT_SDL_LOGFN(Verbose, VERBOSE)
SAT_SDL_LOGFN(Debug, DEBUG)
SAT_SDL_LOGFN(Info, INFO)
SAT_SDL_LOGFN(Warn, WARN)
SAT_SDL_LOGFN(Error, ERROR)
SAT_SDL_LOGFN(Critical, CRITICAL)

int SDLCALL SDL_snprintf(SDL_OUT_Z_CAP(maxlen) char *buf, size_t maxlen, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = SDL_vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return r;
}

int SDLCALL SDL_sscanf(const char *buf, SDL_SCANF_FORMAT_STRING const char *fmt, ...)
{
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = SDL_vsscanf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int SDLCALL SDL_asprintf(char **strp, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = SDL_vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}
