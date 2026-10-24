/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ALTone: OpenAL on an Amiga, for a lab (Design-OpenAL section 6, "In a
 * lab"). It plays, through the default device:
 *   1. a 440 Hz tone, 1 second, in the centre;
 *   2. the same tone going round the listener once in 4 seconds (a 3D
 *      sweep: left, behind, right, ahead), its position set 50 times a
 *      second;
 *   3. a stream of 4 queued buffers of 4096 frames, a tone rising from 220
 *      to 880 Hz over 3 seconds, refilled as each is processed.
 * It prints what the device says (its name, rate, update rate, whether it
 * is connected), how many buffers it streamed and how many times the
 * stream ran dry, and returns 0, or 5 when OpenAL gave an error.
 *   ALTone        (altone.m: OpenAL.module through libopenal.a;
 *                  altone: libopenal_static.a, OpenAL inside the program) */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <proto/dos.h>

#define RATE 22050
#define PI 3.14159265358979

static int errors;

static void check(const char *what)
{
    ALenum e = alGetError();
    if (e != AL_NO_ERROR) {
        printf("ALTone: %s: %s\n", what, alGetString(e));
        errors++;
    }
}

/* frames of a tone from f0 to f1 Hz, its phase carried on in *phase. */
static void tone(ALshort *pcm, int frames, double f0, double f1, double *phase)
{
    int k;
    for (k = 0; k < frames; k++) {
        double f = f0 + (f1 - f0) * k / frames;
        pcm[k] = (ALshort)(12000.0 * sin(*phase));
        *phase += 2.0 * PI * f / RATE;
        if (*phase > 2.0 * PI) {
            *phase -= 2.0 * PI;
        }
    }
}

int main(void)
{
    ALCdevice *dev;
    ALCcontext *ctx;
    ALuint src, buf, sbuf[4];
    ALshort *pcm;
    ALCint freq = 0, refresh = 0, connected = 0;
    double phase = 0.0;
    int i, streamed = 0, dry = 0, step = 0;

    dev = alcOpenDevice(NULL);
    if (!dev) {
        printf("ALTone: no sound device\n");
        return 5;
    }
    ctx = alcCreateContext(dev, NULL);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        printf("ALTone: no context (0x%x)\n", (unsigned)alcGetError(dev));
        alcCloseDevice(dev);
        return 5;
    }
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &freq);
    alcGetIntegerv(dev, ALC_REFRESH, 1, &refresh);
    printf("ALTone: %s, %s at %ld Hz, %ld updates a second\n", alcGetString(dev, ALC_DEVICE_SPECIFIER),
           alGetString(AL_VERSION), (long)freq, (long)refresh);

    pcm = malloc(RATE * sizeof(ALshort));
    if (!pcm) {
        return 5;
    }
    alGenSources(1, &src);
    alGenBuffers(1, &buf);
    check("making a source and a buffer");

    /* 1. The tone, a second, in the centre. */
    tone(pcm, RATE, 440.0, 440.0, &phase);
    alBufferData(buf, AL_FORMAT_MONO16, pcm, RATE * sizeof(ALshort), RATE);
    alSourcei(src, AL_BUFFER, (ALint)buf);
    alSourcePlay(src);
    check("the tone");
    printf("ALTone: 1. a 440 Hz tone, 1 second, in the centre\n");
    Delay(55);

    /* 2. Round the listener in 4 seconds. */
    alSourcei(src, AL_LOOPING, AL_TRUE);
    alSourcePlay(src);
    printf("ALTone: 2. the tone round the listener, 4 seconds\n");
    for (step = 0; step < 200; step++) {
        double a = 2.0 * PI * step / 200.0;
        /* From the left (-x), behind (+z), right (+x), ahead (-z). */
        alSource3f(src, AL_POSITION, (ALfloat)(-2.0 * cos(a)), 0.0f, (ALfloat)(2.0 * sin(a)));
        Delay(1);
    }
    alSourceStop(src);
    alSourcei(src, AL_LOOPING, AL_FALSE);
    alSourcei(src, AL_BUFFER, 0);
    alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
    check("the sweep");

    /* 3. A stream: 4 buffers of 4096 frames, refilled as processed. */
    printf("ALTone: 3. a stream rising from 220 to 880 Hz, 3 seconds\n");
    alGenBuffers(4, sbuf);
    phase = 0.0;
    for (i = 0; i < 4; i++) {
        tone(pcm, 4096, 220.0 + 660.0 * streamed * 4096 / (3.0 * RATE),
             220.0 + 660.0 * (streamed + 1) * 4096 / (3.0 * RATE), &phase);
        alBufferData(sbuf[i], AL_FORMAT_MONO16, pcm, 4096 * sizeof(ALshort), RATE);
        streamed++;
    }
    alSourceQueueBuffers(src, 4, sbuf);
    alSourcePlay(src);
    while (streamed * 4096 < 3 * RATE) {
        ALint processed = 0, state = 0;
        alGetSourcei(src, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint b;
            alSourceUnqueueBuffers(src, 1, &b);
            tone(pcm, 4096, 220.0 + 660.0 * streamed * 4096 / (3.0 * RATE),
                 220.0 + 660.0 * (streamed + 1) * 4096 / (3.0 * RATE), &phase);
            alBufferData(b, AL_FORMAT_MONO16, pcm, 4096 * sizeof(ALshort), RATE);
            alSourceQueueBuffers(src, 1, &b);
            streamed++;
        }
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            dry++;
            alSourcePlay(src);
        }
        Delay(1);
    }
    for (;;) {
        ALint state = 0;
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            break;
        }
        Delay(2);
    }
    check("the stream");
    alcGetIntegerv(dev, ALC_CONNECTED, 1, &connected);
    printf("ALTone: %d buffers streamed, %d times dry; device %s\n", streamed, dry,
           connected ? "connected" : "disconnected (no sound)");

    alSourcei(src, AL_BUFFER, 0);
    alDeleteSources(1, &src);
    alDeleteBuffers(1, &buf);
    alDeleteBuffers(4, sbuf);
    check("ending");
    free(pcm);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return errors || !connected ? 5 : 0;
}
