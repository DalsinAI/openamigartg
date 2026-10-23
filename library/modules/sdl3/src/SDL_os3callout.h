/* Copyright (c) 2026 AmigaChrome Team. OpenGPU's SDL 3, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL's calls into the program, and its threads, in SDL3.module: the same as
 * SDL2.module's (library/modules/sdl2/src/SDL_os3callout.h). The module is
 * built -fbaserel32: its code reaches its globals through A4, which is the
 * program's copy of SDL's data. So:
 *   - a call into the program (a thread's function, a timer, an event filter
 *     or watcher, a main-thread callback, an audio stream or postmix
 *     callback, the SDL_main callbacks and SDL_RunApp's main) runs with the
 *     program's A4 and the module's comes back after (SDL_OS3_CALL*:
 *     ogpu_module_callout, unless the function is SDL's own, which is called
 *     as it is). The callout passes four longs and gives one back, so a
 *     callback that takes or returns a Uint64 (SDL_AddTimerNS) is called
 *     directly, with the module's A4;
 *   - a thread SDL starts begins with A4 from the thread that started it
 *     (SDL_systhread.c: OGPU_MODULE_THREAD_ENTRY, ogpu_module_thread_a4).
 * In libSDL3_static.a (SDL in the program) these are plain calls. */
#ifndef SDL_OS3CALLOUT_H
#define SDL_OS3CALLOUT_H

#if defined(SDL_OPEN_LIBRARY_BUILD)
#include "module_rt.h"
#define SDL_OS3_OWN(fn) ogpu_module_owns((const void *)(fn))
#define SDL_OS3_OUT(fn, a, b, c, d) \
    ogpu_module_callout((const void *)(fn), (long)(a), (long)(b), (long)(c), (long)(d))
#define SDL_OS3_CALL1(rt, fn, a) \
    (SDL_OS3_OWN(fn) ? (rt)(fn)(a) : (rt)SDL_OS3_OUT(fn, a, 0, 0, 0))
#define SDL_OS3_CALL2(rt, fn, a, b) \
    (SDL_OS3_OWN(fn) ? (rt)(fn)(a, b) : (rt)SDL_OS3_OUT(fn, a, b, 0, 0))
#define SDL_OS3_CALL3(rt, fn, a, b, c) \
    (SDL_OS3_OWN(fn) ? (rt)(fn)(a, b, c) : (rt)SDL_OS3_OUT(fn, a, b, c, 0))
#define SDL_OS3_CALL1V(fn, a) \
    (SDL_OS3_OWN(fn) ? (fn)(a) : (void)SDL_OS3_OUT(fn, a, 0, 0, 0))
#define SDL_OS3_CALL2V(fn, a, b) \
    (SDL_OS3_OWN(fn) ? (fn)(a, b) : (void)SDL_OS3_OUT(fn, a, b, 0, 0))
#define SDL_OS3_CALL3V(fn, a, b, c) \
    (SDL_OS3_OWN(fn) ? (fn)(a, b, c) : (void)SDL_OS3_OUT(fn, a, b, c, 0))
#define SDL_OS3_CALL4V(fn, a, b, c, d) \
    (SDL_OS3_OWN(fn) ? (fn)(a, b, c, d) : (void)SDL_OS3_OUT(fn, a, b, c, d))
#else
#define SDL_OS3_CALL1(rt, fn, a) ((rt)(fn)(a))
#define SDL_OS3_CALL2(rt, fn, a, b) ((rt)(fn)(a, b))
#define SDL_OS3_CALL3(rt, fn, a, b, c) ((rt)(fn)(a, b, c))
#define SDL_OS3_CALL1V(fn, a) ((fn)(a))
#define SDL_OS3_CALL2V(fn, a, b) ((fn)(a, b))
#define SDL_OS3_CALL3V(fn, a, b, c) ((fn)(a, b, c))
#define SDL_OS3_CALL4V(fn, a, b, c, d) ((fn)(a, b, c, d))
/* module_rt.h's thread start, without A4. */
#define ogpu_module_thread_a4(child) ((void)(child))
#define OGPU_MODULE_THREAD_ENTRY(name, fn) \
    void name(void) { fn(); }
#endif

#endif
