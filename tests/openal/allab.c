/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ALLab: OpenAL.module in a lab (Design-OpenAL section 6, "In a lab"), on
 * the Amiga, through the module (allab.m) or with OpenAL inside it (allab).
 *
 *   ALLab [DEVICE name] SCENES file   the tone, the sweep and the stream of
 *                                     file (allab_scenes.h), half a second
 *                                     apart, for the instance's sound ring
 *                                     to be recorded and set against the
 *                                     PC's references (allab_ref.c)
 *   ALLab [DEVICE name] TIME          the 68k's time for a source's update
 *   ALLab [DEVICE name] HOLD seconds [LEFT|RIGHT|CENTRE] [hz]
 *                                     a tone, for as long, on one side
 *
 * name is "AHI Voices" or "AHI Mix"; without DEVICE, the default device.
 * It prints what it does and what it measured, and returns 0, or 5 when
 * OpenAL gave an error or the stream ran dry. */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include "allab_scenes.h"

struct Device *TimerBase;
static struct timerequest *treq;
static struct MsgPort *tport;
static int errors;

static void check(const char *what)
{
    ALenum e = alGetError();
    if (e != AL_NO_ERROR) {
        printf("ALLab: %s: %s\n", what, alGetString(e));
        errors++;
    }
}

static int clock_open(void)
{
    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof *treq) : NULL;
    if (!treq || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)treq, 0)) {
        return 0;
    }
    TimerBase = treq->tr_node.io_Device;
    return 1;
}

static void clock_close(void)
{
    if (TimerBase) {
        CloseDevice((struct IORequest *)treq);
    }
    if (treq) {
        DeleteIORequest((struct IORequest *)treq);
    }
    if (tport) {
        DeleteMsgPort(tport);
    }
}

/* Microseconds since some point, from the E clock. */
static double clock_us(void)
{
    struct EClockVal ev;
    ULONG rate = ReadEClock(&ev);
    return ((double)ev.ev_hi * 4294967296.0 + (double)ev.ev_lo) * 1e6 / (double)rate;
}

static ALint srci(ALuint s, ALenum p)
{
    ALint v = 0;
    alGetSourcei(s, p, &v);
    return v;
}

static void wait_stopped(ALuint s)
{
    while (srci(s, AL_SOURCE_STATE) == AL_PLAYING) {
        Delay(1);
    }
}

static void gap(void)
{
    Delay((LONG)(AS_GAP_SECONDS * 50));
}

/* ---- The scenes ------------------------------------------------------------------------ */

static void scene_tone(void)
{
    ALuint s, b;
    short *pcm = malloc(AS_TONE_FRAMES * sizeof *pcm);
    as_tone(pcm);
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, AS_TONE_FRAMES * 2, AS_TONE_RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcef(s, AL_GAIN, AS_TONE_GAIN);
    alSourcePlay(s);
    check("the tone");
    printf("ALLab: 1. a 440 Hz tone, 1 second, in the centre\n");
    wait_stopped(s);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    free(pcm);
}

/* The position moves on each 441 frames of the buffer: the steps follow
 * the source's offset, as the board plays it. */
static void scene_sweep(void)
{
    ALuint s, b;
    short *pcm = malloc(AS_SWEEP_FRAMES * sizeof *pcm);
    long played = 0, last = 0;
    int step = 0, late = 0;
    float x, z;
    as_sweep_tone(pcm);
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, AS_SWEEP_FRAMES * 2, AS_SWEEP_RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    as_sweep_position(0, &x, &z);
    alSource3f(s, AL_POSITION, x, 0.0f, z);
    alSourcePlay(s);
    check("the sweep");
    printf("ALLab: 2. the tone round the listener, 4 seconds\n");
    while (step < AS_SWEEP_STEPS) {
        long off = srci(s, AL_SAMPLE_OFFSET), now;
        if (off < last) {
            played += AS_SWEEP_FRAMES;
        }
        last = off;
        now = (played + off) / AS_SWEEP_STEP;
        if (now > step) {
            late += now > step + 1;
            step = (int)now;
            if (step < AS_SWEEP_STEPS) {
                as_sweep_position(step, &x, &z);
                alSource3f(s, AL_POSITION, x, 0.0f, z);
            }
        }
        Delay(1);
    }
    alSourceStop(s);
    check("the sweep's end");
    printf("ALLab: sweep: %d steps, %d of them a step or more late\n", AS_SWEEP_STEPS, late);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    free(pcm);
}

static int scene_stream(const char *file)
{
    ALuint s, b[4];
    short *pcm = malloc(AS_STREAM_BUFFER * 2);
    BPTR fh = Open((CONST_STRPTR)file, MODE_OLDFILE);
    long frames = 0;
    int queued = 0, dry = 0, i, eof = 0;
    if (!fh || !pcm) {
        printf("ALLab: no stream file %s\n", file);
        free(pcm);
        return 5;
    }
    alGenSources(1, &s);
    alGenBuffers(4, b);
    for (i = 0; i < 4 && !eof; i++) {
        LONG n = Read(fh, pcm, AS_STREAM_BUFFER * 2);
        if (n <= 0) {
            eof = 1;
            break;
        }
        alBufferData(b[i], AL_FORMAT_MONO16, pcm, n & ~1, AS_STREAM_RATE);
        alSourceQueueBuffers(s, 1, &b[i]);
        frames += n / 2;
        queued++;
    }
    alSourcePlay(s);
    check("the stream");
    printf("ALLab: 3. the stream %s\n", file);
    while (!eof) {
        ALint processed = srci(s, AL_BUFFERS_PROCESSED);
        while (processed-- > 0 && !eof) {
            ALuint u;
            LONG n;
            alSourceUnqueueBuffers(s, 1, &u);
            n = Read(fh, pcm, AS_STREAM_BUFFER * 2);
            if (n <= 0) {
                eof = 1;
                break;
            }
            alBufferData(u, AL_FORMAT_MONO16, pcm, n & ~1, AS_STREAM_RATE);
            alSourceQueueBuffers(s, 1, &u);
            frames += n / 2;
            queued++;
        }
        if (srci(s, AL_SOURCE_STATE) != AL_PLAYING && !eof) {
            dry++;
            alSourcePlay(s);
        }
        Delay(1);
    }
    wait_stopped(s);
    check("the stream's end");
    Close(fh);
    printf("ALLab: stream: %ld frames in %d buffers, %d times dry\n", frames, queued, dry);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(4, b);
    free(pcm);
    return dry ? 5 : 0;
}

/* ---- The 68k's time --------------------------------------------------------------------- */

static void time_updates(void)
{
    enum { N = 1000, M = 16, ROUNDS = 100 };
    ALuint s[M], b;
    short *pcm = malloc(AS_SWEEP_FRAMES * sizeof *pcm);
    double t0, t1;
    int i, k;
    as_sweep_tone(pcm);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, AS_SWEEP_FRAMES * 2, AS_SWEEP_RATE);
    alGenSources(M, s);
    for (k = 0; k < M; k++) {
        alSourcei(s[k], AL_BUFFER, (ALint)b);
        alSourcei(s[k], AL_LOOPING, AL_TRUE);
        alSourcef(s[k], AL_GAIN, 0.05f);
    }
    alSourcePlay(s[0]);
    check("the timing's sources");
    Delay(5);
    /* A position: the gains worked out and sent. */
    t0 = clock_us();
    for (i = 0; i < N; i++) {
        alSource3f(s[0], AL_POSITION, (ALfloat)(i % 21 - 10), 0.0f, -1.0f);
    }
    t1 = clock_us();
    printf("ALLab: time: alSource3f(AL_POSITION) on a playing source: %.1f us each (%d)\n", (t1 - t0) / N, N);
    /* A pitch: the frequency. */
    t0 = clock_us();
    for (i = 0; i < N; i++) {
        alSourcef(s[0], AL_PITCH, 0.5f + (ALfloat)(i % 10) * 0.1f);
    }
    t1 = clock_us();
    printf("ALLab: time: alSourcef(AL_PITCH): %.1f us each\n", (t1 - t0) / N);
    alSourcef(s[0], AL_PITCH, 1.0f);
    /* The listener: every playing source's gains. */
    alSourcePlayv(M, s);
    Delay(5);
    t0 = clock_us();
    for (i = 0; i < N / 10; i++) {
        alListener3f(AL_POSITION, (ALfloat)(i % 7), 0.0f, 0.0f);
    }
    t1 = clock_us();
    printf("ALLab: time: alListener3f(AL_POSITION) with %d sources playing: %.1f us each, %.1f us a source\n", M,
           (t1 - t0) / (N / 10), (t1 - t0) / (N / 10) / M);
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    /* A frame's batch: suspend, every source moved, process. */
    t0 = clock_us();
    for (i = 0; i < ROUNDS; i++) {
        alcSuspendContext(alcGetCurrentContext());
        for (k = 0; k < M; k++) {
            alSource3f(s[k], AL_POSITION, (ALfloat)((i + k) % 21 - 10), 0.0f, -2.0f);
        }
        alcProcessContext(alcGetCurrentContext());
    }
    t1 = clock_us();
    printf("ALLab: time: %d sources moved between suspend and process: %.1f us a batch, %.1f us a source\n", M,
           (t1 - t0) / ROUNDS, (t1 - t0) / ROUNDS / M);
    /* A query. */
    t0 = clock_us();
    for (i = 0; i < N; i++) {
        srci(s[0], AL_SAMPLE_OFFSET);
    }
    t1 = clock_us();
    printf("ALLab: time: alGetSourcei(AL_SAMPLE_OFFSET): %.1f us each\n", (t1 - t0) / N);
    /* Play and stop. */
    alSourceStopv(M, s);
    t0 = clock_us();
    for (i = 0; i < N / 10; i++) {
        alSourcePlay(s[0]);
        alSourceStop(s[0]);
    }
    t1 = clock_us();
    printf("ALLab: time: alSourcePlay and alSourceStop: %.1f us a pair\n", (t1 - t0) / (N / 10));
    check("the timing");
    for (k = 0; k < M; k++) {
        alSourcei(s[k], AL_BUFFER, 0);
    }
    alDeleteSources(M, s);
    alDeleteBuffers(1, &b);
    free(pcm);
}

/* ---- A tone held, for the broker's test ---------------------------------------------- */

static void hold(int seconds, float side, double hz)
{
    ALuint s, b;
    short *pcm = malloc(44100 * 2);
    int i;
    for (i = 0; i < 44100; i++) {
        pcm[i] = (short)(10000.0 * sin(2.0 * AS_PI * hz * i / 44100.0));
    }
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, 44100 * 2, 44100);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(s, AL_POSITION, side, 0.0f, side == 0.0f ? -1.0f : 0.0f);
    alSourcePlay(s);
    check("the held tone");
    printf("ALLab: holding %.0f Hz for %d seconds\n", hz, seconds);
    Delay(seconds * 50);
    printf("ALLab: held: %s\n", srci(s, AL_SOURCE_STATE) == AL_PLAYING ? "playing to the end" : "stopped early");
    alSourceStop(s);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    free(pcm);
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    ALCdevice *dev;
    ALCcontext *ctx;
    ALCint freq = 0, refresh = 0, connected = 0;
    int a = 1, rc = 0;

    if (argc > 2 && !strcmp(argv[1], "DEVICE")) {
        name = argv[2];
        a = 3;
    }
    if (a >= argc) {
        printf("ALLab [DEVICE name] SCENES file | TIME | HOLD seconds [LEFT|RIGHT|CENTRE] [hz]\n");
        return 5;
    }
    dev = alcOpenDevice(name);
    if (!dev) {
        printf("ALLab: no device %s (0x%x)\n", name ? name : "(default)", (unsigned)alcGetError(NULL));
        return 5;
    }
    ctx = alcCreateContext(dev, NULL);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        printf("ALLab: no context (0x%x)\n", (unsigned)alcGetError(dev));
        alcCloseDevice(dev);
        return 5;
    }
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &freq);
    alcGetIntegerv(dev, ALC_REFRESH, 1, &refresh);
    printf("ALLab: %s at %ld Hz, %ld updates a second; default \"%s\"\n", alcGetString(dev, ALC_DEVICE_SPECIFIER),
           (long)freq, (long)refresh, alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER));

    if (!strcmp(argv[a], "SCENES") && a + 1 < argc) {
        scene_tone();
        gap();
        scene_sweep();
        gap();
        rc = scene_stream(argv[a + 1]);
    } else if (!strcmp(argv[a], "TIME")) {
        if (!clock_open()) {
            printf("ALLab: no timer.device\n");
            rc = 5;
        } else {
            time_updates();
        }
        clock_close();
    } else if (!strcmp(argv[a], "HOLD") && a + 1 < argc) {
        float side = 0.0f;
        double hz = 440.0;
        if (a + 2 < argc) {
            side = !strcmp(argv[a + 2], "LEFT") ? -1.0f : !strcmp(argv[a + 2], "RIGHT") ? 1.0f : 0.0f;
        }
        if (a + 3 < argc) {
            hz = atof(argv[a + 3]);
        }
        hold(atoi(argv[a + 1]), side, hz);
    } else {
        printf("ALLab: what is %s?\n", argv[a]);
        rc = 5;
    }

    alcGetIntegerv(dev, ALC_CONNECTED, 1, &connected);
    printf("ALLab: device %s; %d AL errors\n", connected ? "connected" : "disconnected", errors);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return rc || errors || !connected ? 5 : 0;
}
