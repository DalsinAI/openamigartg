/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's locks on a PC (tests/openal/run_host.sh): "AL and ALC are
 * threadsafe" (6.2). The output mixes in a thread of its own, as al_ahi.c's
 * process does on the Amiga, while one thread streams a source (unqueue,
 * refill, queue) and another makes and deletes sources and buffers, moves
 * them, plays and stops them, and suspends and processes the context. Then
 * a context is destroyed and another made while the output runs, and
 * everything is closed. Run with AddressSanitizer and UBSan, and once more
 * with ThreadSanitizer. Exit 0 when every check passes and nothing hangs. */
#include "check.h"
#include "host_out.h"

#include <pthread.h>
#include <time.h>

static ALCdevice *dev;
static ALCcontext *ctx;
static int streamed, streaming_errors;     /* the streamer's, read after it is joined */

static void nap(long us)
{
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = us * 1000;
    nanosleep(&t, NULL);
}

static void *streamer(void *arg)
{
    ALuint s, b[4];
    ALshort pcm[2 * 256];
    int k, n = 0;
    (void)arg;
    for (k = 0; k < 2 * 256; k++) {
        pcm[k] = (ALshort)(k * 31);
    }
    alGenSources(1, &s);
    alGenBuffers(4, b);
    for (k = 0; k < 4; k++) {
        alBufferData(b[k], AL_FORMAT_STEREO16, pcm, sizeof pcm, 22050);
    }
    alSourceQueueBuffers(s, 4, b);
    alSourcePlay(s);
    while (n < 400) {
        ALint processed = 0, state = 0;
        alGetSourcei(s, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint done;
            alSourceUnqueueBuffers(s, 1, &done);
            alBufferData(done, AL_FORMAT_STEREO16, pcm, sizeof pcm, 22050);
            alSourceQueueBuffers(s, 1, &done);
            n++;
        }
        alGetSourcei(s, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(s);        /* an underrun: start again, as a stream does */
        }
        if (alGetError() != AL_NO_ERROR) {
            streaming_errors++;
        }
        nap(100);
    }
    alSourceStop(s);
    alDeleteSources(1, &s);
    alDeleteBuffers(4, b);
    streamed = n;
    return NULL;
}

static void *fiddler(void *arg)
{
    ALuint s[8], b;
    ALshort pcm[300];
    int i, k;
    (void)arg;
    for (k = 0; k < 300; k++) {
        pcm[k] = (ALshort)(k * 100 - 15000);
    }
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, 11025);
    for (i = 0; i < 300; i++) {
        alGenSources(8, s);
        for (k = 0; k < 8; k++) {
            alSourcei(s[k], AL_BUFFER, (ALint)b);
            alSource3f(s[k], AL_POSITION, (float)(k - 4), 0.0f, (float)-i);
            alSourcef(s[k], AL_PITCH, 0.5f + 0.25f * (float)k);
            alSourcei(s[k], AL_LOOPING, k & 1);
        }
        alSourcePlayv(8, s);
        if (i % 7 == 0) {
            alcSuspendContext(ctx);
        }
        alListener3f(AL_POSITION, (float)(i % 10), 0.0f, 0.0f);
        alSourcef(s[2], AL_SEC_OFFSET, 0.001f);
        if (i % 7 == 0) {
            alcProcessContext(ctx);
        }
        nap(50);
        alSourceStop(s[3]);
        alDeleteSources(8, s);  /* some still playing */
    }
    alDeleteBuffers(1, &b);
    return NULL;
}

int main(void)
{
    ALCint attrs[] = { ALC_FREQUENCY, 22050, ALC_REFRESH, 200, 0 };
    pthread_t t1, t2;
    unsigned long before;
    ALuint b;
    al_core_init();
    host_out_threaded = 1;
    dev = alcOpenDevice(NULL);
    ctx = alcCreateContext(dev, attrs);
    CHECK(dev && ctx);
    alcMakeContextCurrent(ctx);
    before = __atomic_load_n(&host_out_updates, __ATOMIC_RELAXED);
    pthread_create(&t1, NULL, streamer, NULL);
    pthread_create(&t2, NULL, fiddler, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(streamed == 400);
    CHECK(streaming_errors == 0);
    CHECK(__atomic_load_n(&host_out_updates, __ATOMIC_RELAXED) > before);
    CHECK_AL(AL_NO_ERROR);

    /* A context ended and another made while the output plays. */
    {
        ALCcontext *second = alcCreateContext(dev, NULL);
        ALuint s;
        alcMakeContextCurrent(second);
        alGenBuffers(1, &b);
        {
            ALshort pcm[100];
            memset(pcm, 0, sizeof pcm);
            alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, 22050);
        }
        alGenSources(1, &s);
        alSourcei(s, AL_BUFFER, (ALint)b);
        alSourcei(s, AL_LOOPING, AL_TRUE);
        alSourcePlay(s);
        nap(2000);
        alcMakeContextCurrent(ctx);
        alcDestroyContext(second);      /* its playing source goes with it */
        CHECK_ALC(dev, ALC_NO_ERROR);
    }
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);             /* the last: the output stops */
    CHECK(!dev->running);
    CHECK(alcCloseDevice(dev) == ALC_FALSE);        /* b is left */
    ctx = alcCreateContext(dev, NULL);  /* and starts again */
    CHECK(dev->running);
    alcMakeContextCurrent(ctx);
    alDeleteBuffers(1, &b);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK(alcCloseDevice(dev) == ALC_TRUE);
    al_core_shutdown();
    printf("threads: %d buffers streamed, %lu updates mixed meanwhile\n", streamed,
           __atomic_load_n(&host_out_updates, __ATOMIC_RELAXED) - before);
    return check_done("test_al_thread");
}
