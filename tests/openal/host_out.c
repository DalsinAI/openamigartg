/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Mix's output for the tests (library/modules/openal/src/al_ahi.c is the
 * Amiga's real one). By default it plays nothing by itself: a test pulls
 * the mixed stream with al_mix_render, an update at a time, and so knows
 * exactly what was mixed when. On a PC (AL_HOST), with host_out_threaded
 * set, a thread pulls updates as al_ahi.c's process does, for the test that
 * calls OpenAL from several threads while it mixes. The same file builds
 * for the 68k (without the thread), so the tests can run there too
 * (library/modules/openal/Makefile, tests). */
#include "host_out.h"

#include <stdlib.h>
#ifdef AL_HOST
#include <pthread.h>
#include <time.h>
#endif

ALuint host_out_frequency = 44100;      /* the output's own rate, as AHI's prefs would give */
int host_out_fail_open;                 /* alcOpenDevice finds no output */
int host_out_fail_start;                /* the output can't start (memory) */
int host_out_threaded;                  /* a thread mixes, as on the Amiga */
unsigned long host_out_updates;         /* updates the thread mixed */

struct host_out {
#ifdef AL_HOST
    pthread_t thread;
#endif
    int running;
    int quit;                   /* read and written atomically */
    ALshort *buf;
};

ALCboolean al_out_open(ALCdevice *d)
{
    struct host_out *o;
    if (host_out_fail_open) {
        return ALC_FALSE;
    }
    o = calloc(1, sizeof *o);
    if (!o) {
        return ALC_FALSE;
    }
    d->out = o;
    d->frequency = host_out_frequency;
    return ALC_TRUE;
}

void al_out_close(ALCdevice *d)
{
    free(d->out);
    d->out = NULL;
}

#ifdef AL_HOST
static void *host_out_main(void *arg)
{
    ALCdevice *d = arg;
    struct host_out *o = d->out;
    struct timespec t = { 0, 200000 };
    while (!__atomic_load_n(&o->quit, __ATOMIC_ACQUIRE)) {
        al_mix_render(d, o->buf, d->update_frames);
        __atomic_add_fetch(&host_out_updates, 1, __ATOMIC_RELAXED);
        nanosleep(&t, NULL);
    }
    return NULL;
}
#endif

ALCboolean al_out_start(ALCdevice *d)
{
    struct host_out *o = d->out;
    if (host_out_fail_start) {
        return ALC_FALSE;
    }
#ifdef AL_HOST
    if (host_out_threaded) {
        o->buf = malloc((size_t)d->update_frames * 4);
        if (!o->buf) {
            return ALC_FALSE;
        }
        __atomic_store_n(&o->quit, 0, __ATOMIC_RELEASE);
        if (pthread_create(&o->thread, NULL, host_out_main, d)) {
            free(o->buf);
            o->buf = NULL;
            return ALC_FALSE;
        }
        o->running = 1;
    }
#else
    (void)o;
#endif
    return ALC_TRUE;
}

void al_out_stop(ALCdevice *d)
{
#ifdef AL_HOST
    struct host_out *o = d->out;
    if (o && o->running) {
        __atomic_store_n(&o->quit, 1, __ATOMIC_RELEASE);
        pthread_join(o->thread, NULL);
        o->running = 0;
        free(o->buf);
        o->buf = NULL;
    }
#else
    (void)d;
#endif
}
