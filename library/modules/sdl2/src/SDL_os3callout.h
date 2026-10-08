/* Copyright (c) 2026 Dalsin Limited. OpenGPU's SDL 2, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL's calls into the program, and its threads, in SDL2.module
 * (include/opengpu/module.h, residency step 2). The module is built
 * -fbaserel32: its code reaches its globals through A4, which is the
 * program's copy of SDL's data. So:
 *   - a call into the program (an audio callback, a timer, a thread's
 *     function, an event filter or watcher) runs with the program's A4 and
 *     the module's comes back after (SDL_OS3_CALLn: ogpu_module_callout,
 *     unless the function is SDL's own, which is called as it is);
 *   - a thread SDL starts begins with A4 from the thread that started it
 *     (SDL_systhread.c: OGPU_MODULE_THREAD_ENTRY, ogpu_module_thread_a4).
 * In libSDL2_static.a (SDL in the program) these are plain calls. */
#ifndef SDL_OS3CALLOUT_H
#define SDL_OS3CALLOUT_H

#if defined(SDL_OPEN_LIBRARY_BUILD)
#include "module_rt.h"
#define SDL_OS3_OWN(fn) ogpu_module_owns((const void *)(fn))
#define SDL_OS3_CALL1(rt, fn, a) \
    (SDL_OS3_OWN(fn) ? (rt)(fn)(a) : (rt)ogpu_module_callout((const void *)(fn), (long)(a), 0, 0, 0))
#define SDL_OS3_CALL2(rt, fn, a, b) \
    (SDL_OS3_OWN(fn) ? (rt)(fn)(a, b) : (rt)ogpu_module_callout((const void *)(fn), (long)(a), (long)(b), 0, 0))
#define SDL_OS3_CALL3V(fn, a, b, c) \
    (SDL_OS3_OWN(fn) ? (fn)(a, b, c) : (void)ogpu_module_callout((const void *)(fn), (long)(a), (long)(b), (long)(c), 0))
#else
#define SDL_OS3_CALL1(rt, fn, a) ((rt)(fn)(a))
#define SDL_OS3_CALL2(rt, fn, a, b) ((rt)(fn)(a, b))
#define SDL_OS3_CALL3V(fn, a, b, c) ((fn)(a, b, c))
/* module_rt.h's thread start, without A4. */
#define ogpu_module_thread_a4(child) ((void)(child))
#define OGPU_MODULE_THREAD_ENTRY(name, fn) \
    void name(void) { fn(); }
#endif

#endif
