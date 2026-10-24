/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The PC's references for ALLab's scenes (allab.c, allab_scenes.h): the
 * same tone, sweep and stream through OpenAL's core on a PC, with the
 * steps on time (the sweep moved every 882 frames, the stream refilled
 * every 882), as the instance's sound ring would have them:
 *   allab_ref OUT_DIR
 * writes OUT_DIR/alstream.raw (the stream: 16-bit big-endian mono, 22,050
 * Hz, for the Amiga), and the scenes as 16-bit stereo WAV files at 44,100
 * Hz, each scene on its own:
 *   ref_voices_{tone,sweep,stream}.wav   through ACAHI's board (Voices;
 *                                        tests/openal/acahi, host_voices.c)
 *   ref_mix_{tone,sweep,stream}.wav      through Mix
 * tests/openal/allab_compare.py sets a recording of the ring against them.
 * Built like the PC tests (run_host.sh's CORE). */
#include "host_out.h"
#include "host_voices.h"
#include "allab_scenes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP 882

static ALCdevice *dev;
static int voices;
static short *pcm_out;
static long out_frames, out_room;

static void render(ALuint n)
{
    if (out_frames + n > out_room) {
        out_room = (out_frames + n) * 2;
        pcm_out = realloc(pcm_out, (size_t)out_room * 4);
        if (!pcm_out) {
            exit(2);
        }
    }
    if (voices) {
        host_voices_render(dev, pcm_out + 2 * out_frames, n);
    } else {
        al_mix_render(dev, pcm_out + 2 * out_frames, n);
    }
    out_frames += n;
}

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 255), f);
    fputc((int)((v >> 8) & 255), f);
    fputc((int)((v >> 16) & 255), f);
    fputc((int)((v >> 24) & 255), f);
}

static void save(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    long i;
    snprintf(path, sizeof path, "%s/ref_%s_%s.wav", dir, voices ? "voices" : "mix", name);
    f = fopen(path, "wb");
    if (!f) {
        exit(2);
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + (unsigned long)out_frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put32(f, 1 | (2ul << 16));
    put32(f, 44100);
    put32(f, 44100 * 4);
    put32(f, 4 | (16ul << 16));
    fwrite("data", 1, 4, f);
    put32(f, (unsigned long)out_frames * 4);
    for (i = 0; i < out_frames * 2; i++) {
        fputc(pcm_out[i] & 255, f);
        fputc((pcm_out[i] >> 8) & 255, f);
    }
    fclose(f);
    printf("allab_ref: %s, %ld frames\n", path, out_frames);
    out_frames = 0;
}

static ALint srci(ALuint s, ALenum p)
{
    ALint v = 0;
    alGetSourcei(s, p, &v);
    return v;
}

static void play_out(ALuint s)
{
    while (srci(s, AL_SOURCE_STATE) == AL_PLAYING) {
        render(STEP);
    }
}

static void scenes(const char *dir)
{
    ALuint s, b, q[4];
    short *pcm = malloc(AS_TONE_FRAMES * 2);
    long next = 0;
    int t, i;
    float x, z;

    as_tone(pcm);
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, AS_TONE_FRAMES * 2, AS_TONE_RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcef(s, AL_GAIN, AS_TONE_GAIN);
    alSourcePlay(s);
    play_out(s);
    save(dir, "tone");

    as_sweep_tone(pcm);
    alSourcei(s, AL_BUFFER, 0);
    alSourcef(s, AL_GAIN, 1.0f);
    alBufferData(b, AL_FORMAT_MONO16, pcm, AS_SWEEP_FRAMES * 2, AS_SWEEP_RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    for (t = 0; t < AS_SWEEP_STEPS; t++) {
        as_sweep_position(t, &x, &z);
        alSource3f(s, AL_POSITION, x, 0.0f, z);
        if (t == 0) {
            alSourcePlay(s);
        }
        render(STEP);
    }
    alSourceStop(s);
    alSourcei(s, AL_LOOPING, AL_FALSE);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
    save(dir, "sweep");

    alSourcei(s, AL_BUFFER, 0);
    alGenBuffers(4, q);
    for (i = 0; i < 4; i++) {
        short buf[AS_STREAM_BUFFER];
        int k;
        for (k = 0; k < AS_STREAM_BUFFER; k++) {
            buf[k] = as_stream_frame(next++);
        }
        alBufferData(q[i], AL_FORMAT_MONO16, buf, sizeof buf, AS_STREAM_RATE);
    }
    alSourceQueueBuffers(s, 4, q);
    alSourcePlay(s);
    while (next < AS_STREAM_FRAMES) {
        ALint processed;
        render(STEP);
        processed = srci(s, AL_BUFFERS_PROCESSED);
        while (processed-- > 0 && next < AS_STREAM_FRAMES) {
            short buf[AS_STREAM_BUFFER];
            int k, n = AS_STREAM_FRAMES - next < AS_STREAM_BUFFER ? (int)(AS_STREAM_FRAMES - next) : AS_STREAM_BUFFER;
            ALuint u;
            alSourceUnqueueBuffers(s, 1, &u);
            for (k = 0; k < n; k++) {
                buf[k] = as_stream_frame(next++);
            }
            alBufferData(u, AL_FORMAT_MONO16, buf, n * 2, AS_STREAM_RATE);
            alSourceQueueBuffers(s, 1, &u);
        }
    }
    play_out(s);
    save(dir, "stream");
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    alDeleteBuffers(4, q);
    free(pcm);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    FILE *f;
    long i;
    ALCcontext *ctx;

    snprintf(path, sizeof path, "%s/alstream.raw", dir);
    f = fopen(path, "wb");
    if (!f) {
        return 2;
    }
    for (i = 0; i < AS_STREAM_FRAMES; i++) {
        short v = as_stream_frame(i);
        fputc((v >> 8) & 255, f);
        fputc(v & 255, f);
    }
    fclose(f);
    printf("allab_ref: %s, %d frames\n", path, AS_STREAM_FRAMES);

    for (voices = 1; voices >= 0; voices--) {
        host_voices_fitted = voices;
        dev = alcOpenDevice(voices ? "AHI Voices" : "AHI Mix");
        ctx = dev ? alcCreateContext(dev, NULL) : NULL;
        if (!ctx) {
            return 2;
        }
        alcMakeContextCurrent(ctx);
        scenes(dir);
        if (alGetError() != AL_NO_ERROR) {
            printf("allab_ref: an AL error\n");
            return 1;
        }
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(dev);
    }
    free(pcm_out);
    return 0;
}
