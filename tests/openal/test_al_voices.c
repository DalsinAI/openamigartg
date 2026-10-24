/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Voices, OpenAL.module's back end for ACAHI's Host mix, on a PC
 * (tests/openal/run_host.sh): the board is the runtime's own acahi.c
 * (tests/openal/acahi), with host_voices.c in the part of AHI and
 * acaudio.audio. The board plays host_voices_chunk frames (64) between two
 * interrupts, as the instance plays a few lines' worth.
 *   - the devices: Voices when the board has a Host mix mode and voices,
 *     Mix otherwise, and each by name;
 *   - the board's volume and pan from the 3D gains: the board's left and
 *     right come back to them;
 *   - one buffer, looping, paused, deleted, offsets: the board's output is
 *     the buffer, frame for frame (a stereo source at gain 1.0, which the
 *     board plays exactly);
 *   - a queued stream with no gap and no repeat while the test unqueues,
 *     refills and queues after every interrupt, AL_BUFFERS_PROCESSED right
 *     after each voice event: with the service task and without it (the AL
 *     calls alone take the events in), at the board's rate and resampled;
 *   - virtual sources: more sources than voices, the loudest on voices, the
 *     others' time running on, a free voice taken at the virtual source's
 *     offset, a voice taken from a quieter or a paused source;
 *   - alcSuspendContext: plays and stops at once, the gains in one pass at
 *     alcProcessContext;
 *   - a random workout of every call on 40 sources and 8 voices;
 *   - Mix's output against the same scenes through the board: a tone, a
 *     sweep round the listener, a resampled stream, a crowd with Doppler,
 *     an 8-bit sound at another pitch (signal to difference ratio printed).
 * Exit 0 when every check passes. */
#include "check.h"
#include "host_out.h"
#include "host_voices.h"
#include "acahi.h"

#define RATE 44100
#define PI 3.14159265358979323846

static ALCdevice *dev;
static ALCcontext *ctx;
static ALshort out[2 * 65536];

/* The test signal: frame n of an endless stream, a value each channel
 * that doesn't repeat within 32749 frames. */
static ALshort sig_l(long n)
{
    return (ALshort)(n % 32749 - 16374);
}

static ALshort sig_r(long n)
{
    return (ALshort)((n * 7) % 32749 - 16374);
}

static void fill(ALuint b, long first, int frames, int rate)
{
    ALshort *pcm = malloc((size_t)frames * 4);
    int k;
    for (k = 0; k < frames; k++) {
        pcm[2 * k] = sig_l(first + k);
        pcm[2 * k + 1] = sig_r(first + k);
    }
    alBufferData(b, AL_FORMAT_STEREO16, pcm, frames * 4, rate);
    free(pcm);
}

static int is_frame(const ALshort *o, long n)
{
    return o[0] == sig_l(n) && o[1] == sig_r(n);
}

static ALint srci(ALuint s, ALenum p)
{
    ALint v = -12345;
    alGetSourcei(s, p, &v);
    return v;
}

static ALsource *SRC(ALuint name)
{
    return al_names_get(&al_source_names, name);
}

static ALuint new_source(void)
{
    ALuint s;
    alGenSources(1, &s);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    return s;
}

static void render(ALuint n)
{
    host_voices_render(dev, out, n);
}

static void open_voices(ALuint free_voices, ALuint chunk, int service)
{
    host_voices_fitted = 1;
    host_voices_free = free_voices;
    host_voices_chunk = chunk;
    host_voices_service_auto = service;
    dev = alcOpenDevice("AHI Voices");
    CHECK(dev != NULL);
    ctx = alcCreateContext(dev, NULL);
    CHECK(ctx != NULL);
    alcMakeContextCurrent(ctx);
}

static void close_voices(void)
{
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK(alcCloseDevice(dev));
    dev = NULL;
    ctx = NULL;
    host_voices_service_auto = 1;
    host_voices_chunk = 64;
    host_voices_free = 32;
    CHECK(host_voices_lock_depth == 0);
}

/* ---- The devices ---------------------------------------------------------------------- */

static void test_devices(void)
{
    const ALCchar *s;
    ALCdevice *d;
    ALCcontext *c;
    ALCint v = 0, attrs[] = { ALC_FREQUENCY, 22050, ALC_REFRESH, 25, 0 };

    host_voices_fitted = 0;
    s = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Mix"));
    s = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Voices") && !strcmp(s + 11, "AHI Mix") && s[19] == 0);
    CHECK(alcOpenDevice("AHI Voices") == NULL);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    d = alcOpenDevice(NULL);
    CHECK(d && !strcmp(alcGetString(d, ALC_DEVICE_SPECIFIER), "AHI Mix"));
    CHECK(alcCloseDevice(d));

    /* The board, with voices: Voices is the default. */
    host_voices_fitted = 1;
    host_voices_free = 32;
    s = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Voices"));
    d = alcOpenDevice(NULL);
    CHECK(d && !strcmp(alcGetString(d, ALC_DEVICE_SPECIFIER), "AHI Voices"));
    CHECK(host_voices_held(d) == 16);
    /* The board's rate whatever the context asks: a hint (6.2.2). */
    c = alcCreateContext(d, attrs);
    CHECK(c != NULL);
    alcGetIntegerv(d, ALC_FREQUENCY, 1, &v);
    CHECK(v == 44100);
    alcGetIntegerv(d, ALC_REFRESH, 1, &v);
    CHECK(v == 50);
    alcGetIntegerv(d, ALC_MONO_SOURCES, 1, &v);
    CHECK(v == 256);
    alcGetIntegerv(d, ALC_CONNECTED, 1, &v);
    CHECK(v == ALC_TRUE);
    alcDestroyContext(c);
    CHECK(alcCloseDevice(d));

    /* Fewer voices free: half, then a quarter; under four, Mix. */
    host_voices_free = 10;
    d = alcOpenDevice(NULL);
    CHECK(d && host_voices_held(d) == 8);
    CHECK(alcCloseDevice(d));
    host_voices_free = 6;
    d = alcOpenDevice("AHI Voices");
    CHECK(d && host_voices_held(d) == 4);
    CHECK(alcCloseDevice(d));
    host_voices_free = 3;
    d = alcOpenDevice(NULL);
    CHECK(d && !strcmp(alcGetString(d, ALC_DEVICE_SPECIFIER), "AHI Mix"));
    CHECK(alcCloseDevice(d));
    CHECK(alcOpenDevice("AHI Voices") == NULL);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    /* Mix by name, whatever the board has. */
    host_voices_free = 32;
    d = alcOpenDevice("AHI Mix");
    CHECK(d && !strcmp(alcGetString(d, ALC_DEVICE_SPECIFIER), "AHI Mix"));
    CHECK(alcCloseDevice(d));
}

/* ---- The board's settings ---------------------------------------------------------------- */

static void test_settings(void)
{
    int i, j, bad = 0;
    for (i = 0; i <= 40; i++) {
        for (j = 0; j <= 40; j++) {
            struct al_params p;
            ALint vol, pan;
            double gl, gr;
            memset(&p, 0, sizeof p);
            p.left = (ALfloat)(i * 0.05);
            p.right = (ALfloat)(j * 0.05);
            p.gain = p.left > p.right ? p.left : p.right;
            al_voices_settings(&p, 1, &vol, &pan);
            /* acahi.c: left = vol * (0x10000 - pan) >> 16, right = vol * pan >> 16. */
            gl = (double)(((long long)vol * (0x10000 - pan)) >> 16) / 65536.0;
            gr = (double)(((long long)vol * pan) >> 16) / 65536.0;
            if (fabs(gl - p.left) > 3.0 / 65536.0 || fabs(gr - p.right) > 3.0 / 65536.0) {
                bad++;
            }
        }
    }
    CHECK(bad == 0);
    {
        struct al_params p;
        ALint vol, pan;
        memset(&p, 0, sizeof p);
        p.left = p.right = p.gain = 0.5f;
        al_voices_settings(&p, 2, &vol, &pan);
        CHECK(vol == 0x8000 && pan == 0x8000);         /* stereo: the gain to both, centred balance */
        p.left = p.right = p.gain = 0.0f;
        al_voices_settings(&p, 1, &vol, &pan);
        CHECK(vol == 0 && pan == 0x8000);
        p.left = p.right = p.gain = 3.0f;
        al_voices_settings(&p, 1, &vol, &pan);
        CHECK(vol == 0x40000);                          /* the board's largest */
    }
}

/* ---- One buffer, looping, paused, deleted -------------------------------------------------- */

static void test_one_shot(void)
{
    ALuint s, b;
    long i, wrong = 0, loud = 0;
    open_voices(32, 64, 1);
    s = new_source();
    alGenBuffers(1, &b);
    fill(b, 0, 3000, RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcePlay(s);
    CHECK_AL(AL_NO_ERROR);
    CHECK(SRC(s)->voice >= 0);
    CHECK(host_voices_on(dev, (ALuint)SRC(s)->voice));
    render(4000);
    for (i = 0; i < 4000; i++) {
        if (i < 3000) {
            wrong += !is_frame(out + 2 * i, i);
        } else {
            loud += out[2 * i] != 0 || out[2 * i + 1] != 0;
        }
    }
    CHECK(wrong == 0);
    CHECK(loud == 0);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 1);
    CHECK(SRC(s)->voice == -1);
    /* Again, from an offset set while stopped. */
    alSourcei(s, AL_SAMPLE_OFFSET, 1000);
    alSourcePlay(s);
    render(64);
    CHECK(is_frame(out, 1000) && is_frame(out + 2 * 63, 1063));
    /* Deleted while playing: silent at once. */
    {
        ALint k = SRC(s)->voice;
        CHECK(k >= 0);
        alDeleteSources(1, &s);
        CHECK_AL(AL_NO_ERROR);
        CHECK(!host_voices_on(dev, (ALuint)k));
        render(64);
        CHECK(out[0] == 0 && out[127] == 0);
    }
    alDeleteBuffers(1, &b);
    CHECK_AL(AL_NO_ERROR);
    CHECK(host_voices_refused == 0);
    close_voices();
}

static void test_looping(void)
{
    ALuint s, b;
    long i, wrong = 0, rendered, end;
    open_voices(32, 64, 1);
    s = new_source();
    alGenBuffers(1, &b);
    fill(b, 0, 500, RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcePlay(s);
    for (rendered = 0; rendered < 20000; rendered += 400) {
        render(400);
        for (i = 0; i < 400; i++) {
            wrong += !is_frame(out + 2 * i, (rendered + i) % 500);
        }
    }
    CHECK(wrong == 0);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_PLAYING);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 0);
    /* Looping off: the loop playing ends, then silence. */
    alSourcei(s, AL_LOOPING, AL_FALSE);
    end = 500 - rendered % 500;
    render(1000);
    for (i = 0; i < 1000; i++) {
        if (i < end) {
            wrong += !is_frame(out + 2 * i, (rendered + i) % 500);
        } else {
            wrong += out[2 * i] != 0 || out[2 * i + 1] != 0;
        }
    }
    CHECK(wrong == 0);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    /* A queue of three looping. */
    {
        ALuint q[3];
        alSourcei(s, AL_BUFFER, 0);
        alGenBuffers(3, q);
        fill(q[0], 0, 300, RATE);
        fill(q[1], 300, 200, RATE);
        fill(q[2], 500, 700, RATE);
        alSourceQueueBuffers(s, 3, q);
        alSourcei(s, AL_LOOPING, AL_TRUE);
        alSourcePlay(s);
        for (rendered = 0; rendered < 12000; rendered += 600) {
            render(600);
            for (i = 0; i < 600; i++) {
                wrong += !is_frame(out + 2 * i, (rendered + i) % 1200);
            }
        }
        CHECK(wrong == 0);
        alSourceStop(s);
        CHECK(srci(s, AL_BUFFERS_PROCESSED) == 3);
        alSourcei(s, AL_BUFFER, 0);
        alDeleteBuffers(3, q);
    }
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    CHECK_AL(AL_NO_ERROR);
    close_voices();
}

static void test_pause_and_offsets(void)
{
    ALuint s, b;
    long i, wrong = 0;
    ALint off, off2;
    ALfloat sec;
    open_voices(32, 64, 1);
    s = new_source();
    alGenBuffers(1, &b);
    fill(b, 0, 100000, RATE);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcePlay(s);
    render(10000);
    /* The offset: the board's position at the last channel info (50 a
     * second, served at the end of a chunk). */
    off = srci(s, AL_SAMPLE_OFFSET);
    CHECK(off <= 10000 && off >= 10000 - 882 - 64);
    alGetSourcef(s, AL_SEC_OFFSET, &sec);
    CHECK_NEAR(sec, off / 44100.0, 1e-6);
    CHECK(srci(s, AL_BYTE_OFFSET) == off * 4);
    /* Paused: silent, and held where it is. */
    alSourcePause(s);
    render(3000);
    for (i = 0; i < 3000; i++) {
        wrong += out[2 * i] != 0 || out[2 * i + 1] != 0;
    }
    off = srci(s, AL_SAMPLE_OFFSET);
    CHECK(off == 10000);
    render(3000);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 10000);
    alSourcePlay(s);
    render(500);
    for (i = 0; i < 500; i++) {
        wrong += !is_frame(out + 2 * i, 10000 + i);
    }
    CHECK(wrong == 0);
    /* An offset set while playing: the board jumps there at once. */
    alSourcei(s, AL_SAMPLE_OFFSET, 50000);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 50000);
    render(100);
    CHECK(is_frame(out, 50000) && is_frame(out + 2 * 99, 50099));
    alSourcef(s, AL_SEC_OFFSET, 1.0f);
    render(64);
    CHECK(is_frame(out, 44100));
    /* Paused, then moved: held at the new place. */
    alSourcePause(s);
    alSourcei(s, AL_SAMPLE_OFFSET, 70000);
    render(2000);
    CHECK(out[0] == 0 && out[3999] == 0);
    off2 = srci(s, AL_SAMPLE_OFFSET);
    CHECK(off2 == 70000);
    alSourcePlay(s);
    render(64);
    CHECK(is_frame(out, 70000));
    /* Rewind: initial, silent. */
    alSourceRewind(s);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_INITIAL && SRC(s)->voice == -1);
    render(64);
    CHECK(out[0] == 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
    CHECK_AL(AL_NO_ERROR);
    close_voices();
}

/* ---- A stream ------------------------------------------------------------------------- */

/* NB buffers of LEN frames, refilled after every interrupt; the output
 * must count on with no gap and no repeat, and AL_BUFFERS_PROCESSED must
 * be every buffer the board has finished, right after each chunk. */
static void stream_run(ALuint chunk, int service, int len, long total_buffers)
{
    enum { NB = 4 };
    ALuint s, b[NB];
    long filled = 0, unqueued = 0, rendered = 0, expect = 0;
    int k, gaps = 0, wrong_processed = 0;
    open_voices(32, chunk, service);
    s = new_source();
    alGenBuffers(NB, b);
    for (k = 0; k < NB; k++) {
        fill(b[k], filled, len, RATE);
        filled += len;
    }
    alSourceQueueBuffers(s, NB, b);
    alSourcePlay(s);
    while (rendered < total_buffers * len) {
        ALint processed;
        long i;
        render(chunk);
        for (i = 0; i < (long)chunk; i++) {
            if (expect < total_buffers * len) {
                gaps += !is_frame(out + 2 * i, expect);
                expect++;
            }
        }
        rendered += chunk;
        processed = srci(s, AL_BUFFERS_PROCESSED);
        {
            long done = rendered / len;
            if (done > filled / len) {
                done = filled / len;
            }
            wrong_processed += processed != done - unqueued;
        }
        while (processed-- > 0) {
            ALuint u;
            alSourceUnqueueBuffers(s, 1, &u);
            unqueued++;
            if (filled < total_buffers * len) {
                fill(u, filled, len, RATE);
                filled += len;
                alSourceQueueBuffers(s, 1, &u);
            }
        }
    }
    CHECK(gaps == 0);
    CHECK(wrong_processed == 0);
    render(chunk);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    CHECK(host_voices_refused == 0);
    printf("stream: %ld buffers of %d frames, chunk %u, %s: %d frames wrong, %d counts wrong\n", total_buffers, len,
           chunk, service ? "with the task" : "AL calls alone", gaps, wrong_processed);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(NB, b);
    CHECK_AL(AL_NO_ERROR);
    close_voices();
}

/* At 22050 Hz: the board steps half a frame each output frame, so a stream
 * of F frames plays 2F output frames, every even one a frame of it. */
static void test_stream_resampled(void)
{
    enum { NB = 4, LEN = 1000, TOTAL = 60 };
    ALuint s, b[NB];
    long filled = 0, rendered = 0, gaps = 0, last_loud = -1, i;
    int k;
    open_voices(32, 64, 1);
    s = new_source();
    alGenBuffers(NB, b);
    for (k = 0; k < NB; k++) {
        fill(b[k], filled, LEN, 22050);
        filled += LEN;
    }
    alSourceQueueBuffers(s, NB, b);
    alSourcePlay(s);
    while (rendered < 2 * TOTAL * LEN + 2048) {
        ALint processed;
        render(64);
        for (i = 0; i < 64; i++) {
            long n = rendered + i;
            if (out[2 * i] || out[2 * i + 1]) {
                last_loud = n;
            }
            if (!(n & 1) && n / 2 < TOTAL * LEN) {
                gaps += !is_frame(out + 2 * i, n / 2);
            }
        }
        rendered += 64;
        processed = srci(s, AL_BUFFERS_PROCESSED);
        while (processed-- > 0) {
            ALuint u;
            alSourceUnqueueBuffers(s, 1, &u);
            if (filled < TOTAL * LEN) {
                fill(u, filled, LEN, 22050);
                filled += LEN;
                alSourceQueueBuffers(s, 1, &u);
            }
        }
    }
    CHECK(gaps == 0);
    CHECK(last_loud == 2 * TOTAL * LEN - 1);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    printf("stream at 22050 Hz: %d frames into %ld output frames, %ld wrong\n", TOTAL * LEN, last_loud + 1, gaps);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(NB, b);
    close_voices();
}

/* An underrun: the queue runs out, the source stops with everything
 * processed; queued again and played, it goes on. */
static void test_underrun(void)
{
    ALuint s, b[2];
    long i, wrong = 0;
    open_voices(32, 64, 0);
    s = new_source();
    alGenBuffers(2, b);
    fill(b[0], 0, 1000, RATE);
    alSourceQueueBuffers(s, 1, &b[0]);
    alSourcePlay(s);
    render(1500);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 1);
    fill(b[1], 1000, 1000, RATE);
    alSourceQueueBuffers(s, 1, &b[1]);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    {
        ALuint u;
        alSourceUnqueueBuffers(s, 1, &u);
        CHECK(u == b[0]);
    }
    alSourcePlay(s);
    render(1000);
    for (i = 0; i < 1000; i++) {
        wrong += !is_frame(out + 2 * i, 1000 + i);
    }
    CHECK(wrong == 0);
    /* Queued while the last buffer plays: no gap. */
    fill(b[0], 2000, 1000, RATE);
    alSourcePlay(s);
    render(500);
    alSourceQueueBuffers(s, 1, &b[0]);
    render(1500);
    for (i = 0; i < 1500; i++) {
        wrong += !is_frame(out + 2 * i, 1500 + i);
    }
    CHECK(wrong == 0);
    alSourceStop(s);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(2, b);
    CHECK_AL(AL_NO_ERROR);
    close_voices();
}

/* ---- Virtual sources -------------------------------------------------------------------- */

static void test_virtual(void)
{
    enum { N = 20 };
    ALuint s[N], b;
    ALshort *pcm = malloc(44100 * 2);
    int i, voiced = 0, k;
    ALint off;
    for (i = 0; i < 44100; i++) {
        pcm[i] = (ALshort)(8000.0 * sin(2.0 * PI * 220.0 * i / 44100.0));
    }
    open_voices(32, 64, 1);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, 44100 * 2, 44100);
    for (i = 0; i < N; i++) {
        s[i] = new_source();
        alSourcei(s[i], AL_BUFFER, (ALint)b);
        alSourcei(s[i], AL_LOOPING, AL_TRUE);
        alSourcef(s[i], AL_GAIN, 0.05f * (ALfloat)(i + 1));
        alSourcei(s[i], AL_SAMPLE_OFFSET, 1000 * i);    /* each from a place of its own */
    }
    alSourcePlayv(N, s);
    CHECK_AL(AL_NO_ERROR);
    for (i = 0; i < N; i++) {
        voiced += SRC(s[i])->voice >= 0;
        if (i >= 4) {
            CHECK(SRC(s[i])->voice >= 0);       /* the 16 loudest */
        }
    }
    CHECK(voiced == 16);
    render(8820);
    for (i = 0; i < 4; i++) {
        off = srci(s[i], AL_SAMPLE_OFFSET) - 1000 * i;
        CHECK(off >= 8820 - 882 && off <= 8820);   /* virtual: time runs on, to the timer's tick */
        CHECK(srci(s[i], AL_SOURCE_STATE) == AL_PLAYING);
    }
    /* A voice comes free: the loudest virtual source takes it where it is. */
    alSourceStop(s[N - 1]);
    CHECK(SRC(s[3])->voice >= 0);
    k = SRC(s[3])->voice;
    off = srci(s[3], AL_SAMPLE_OFFSET);
    CHECK((ALint)host_voices_buffer_frame(dev, (ALuint)k) == off);
    CHECK(off - 3000 >= 8820 - 882 && off - 3000 <= 8820);
    render(882);
    CHECK(host_voices_on(dev, (ALuint)k));
    /* Louder than twice the quietest playing on a voice: takes its voice. */
    alSourcef(s[0], AL_GAIN, 1.0f);
    CHECK(SRC(s[0])->voice >= 0);
    CHECK(SRC(s[3])->voice == -1);
    /* A paused source's voice goes to a virtual one first. */
    alSourcePause(s[N - 2]);
    CHECK(SRC(s[N - 2])->voice == -1);
    CHECK(SRC(s[3])->voice >= 0);
    render(882);
    /* Every voice in use once, by a source playing. */
    {
        int used[AL_VOICES_MAX] = { 0 }, twice = 0;
        for (i = 0; i < N; i++) {
            if (SRC(s[i])->voice >= 0) {
                twice += used[SRC(s[i])->voice]++ > 0;
                CHECK(SRC(s[i])->state == AL_PLAYING);
            }
        }
        CHECK(twice == 0);
    }
    for (i = 0; i < N; i++) {
        alSourceStop(s[i]);
        alSourcei(s[i], AL_BUFFER, 0);
    }
    alDeleteSources(N, s);
    alDeleteBuffers(1, &b);
    CHECK_AL(AL_NO_ERROR);
    free(pcm);
    close_voices();
}

/* ---- alcSuspendContext ---------------------------------------------------------------- */

static void test_suspend(void)
{
    enum { N = 8 };
    ALuint s[N + 1], b;
    static ALshort pcm[2000];
    unsigned long vols, sounds;
    int i;
    for (i = 0; i < 2000; i++) {
        pcm[i] = (ALshort)(i * 13 % 20000 - 10000);
    }
    open_voices(32, 64, 1);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, RATE);
    for (i = 0; i <= N; i++) {
        alGenSources(1, &s[i]);
        alSourcei(s[i], AL_BUFFER, (ALint)b);
        alSourcei(s[i], AL_LOOPING, AL_TRUE);
        alSource3f(s[i], AL_POSITION, (ALfloat)i - 4.0f, 0.0f, -1.0f);
    }
    alSourcePlayv(N, s);
    render(1000);
    vols = host_voices_vols;
    sounds = host_voices_sounds;
    alcSuspendContext(ctx);
    for (i = 0; i < N; i++) {
        alSource3f(s[i], AL_POSITION, 4.0f - (ALfloat)i, 0.0f, -2.0f);
    }
    alListenerf(AL_GAIN, 0.5f);
    CHECK(host_voices_vols == vols);                /* recorded only */
    alSourcePlay(s[N]);                             /* plays at once */
    CHECK(host_voices_sounds > sounds && SRC(s[N])->voice >= 0 && host_voices_on(dev, (ALuint)SRC(s[N])->voice));
    alSourceStop(s[0]);                             /* stops at once */
    CHECK(SRC(s[0])->voice == -1);
    render(1000);
    vols = host_voices_vols;
    alcProcessContext(ctx);
    CHECK(host_voices_vols - vols == N);            /* the seven moved and the new one, in one pass */
    for (i = 1; i <= N; i++) {
        struct al_params p;
        ALint vol, pan;
        al_source_params(ctx, SRC(s[i]), 1, &p);
        al_voices_settings(&p, 1, &vol, &pan);
        CHECK(host_voices_board(dev)->v[SRC(s[i])->voice].cur.vol == vol);
        CHECK(host_voices_board(dev)->v[SRC(s[i])->voice].cur.pan == pan);
    }
    for (i = 0; i <= N; i++) {
        alSourceStop(s[i]);
        alSourcei(s[i], AL_BUFFER, 0);
    }
    alDeleteSources(N + 1, s);
    alDeleteBuffers(1, &b);
    CHECK_AL(AL_NO_ERROR);
    close_voices();
}

/* ---- A random workout ------------------------------------------------------------------- */

static unsigned long rnd_state = 12345;
static unsigned rnd(unsigned n)
{
    rnd_state = rnd_state * 1103515245ul + 12345ul;
    return (unsigned)((rnd_state >> 16) & 0x7fff) % n;
}

static void test_workout(void)
{
    enum { NS = 40, NB = 12 };
    static const ALenum formats[] = { AL_FORMAT_MONO8, AL_FORMAT_MONO16, AL_FORMAT_STEREO8, AL_FORMAT_STEREO16 };
    static const int rates[] = { 8000, 11025, 22050, 32000, 44100, 48000 };
    ALuint s[NS], b[NB];
    ALubyte *data = malloc(48000 * 4);
    int i, it, errors = 0, shared = 0;
    for (i = 0; i < 48000 * 4; i++) {
        data[i] = (ALubyte)(i * 37 + (i >> 7));
    }
    open_voices(8, 64, 1);
    alGenBuffers(NB, b);
    for (i = 0; i < NB; i++) {
        alBufferData(b[i], formats[i % 4], data, (ALsizei)(200 + rnd(8000)) * 4, rates[i % 6]);
    }
    alGenSources(NS, s);
    for (it = 0; it < 3000; it++) {
        ALuint x = s[rnd(NS)];
        switch (rnd(14)) {
        case 0: alSourcePlay(x); break;
        case 1: alSourceStop(x); break;
        case 2: alSourcePause(x); break;
        case 3: alSourceRewind(x); break;
        case 4:
            if (srci(x, AL_SOURCE_STATE) != AL_PLAYING && srci(x, AL_SOURCE_STATE) != AL_PAUSED) {
                alSourcei(x, AL_BUFFER, 0);
                alSourcei(x, AL_BUFFER, (ALint)b[rnd(NB)]);
            }
            break;
        case 5: alSourcei(x, AL_LOOPING, (ALint)rnd(2)); break;
        case 6: alSource3f(x, AL_POSITION, (ALfloat)rnd(20) - 10.0f, 0.0f, (ALfloat)rnd(20) - 10.0f); break;
        case 7: alSourcef(x, AL_GAIN, (ALfloat)rnd(100) / 50.0f); break;
        case 8: alSourcef(x, AL_PITCH, 0.25f + (ALfloat)rnd(100) / 40.0f); break;
        case 9:
            if (srci(x, AL_SOURCE_TYPE) != AL_STATIC) {
                /* any buffer into an empty queue; else the current one again (its format) */
                ALint q = srci(x, AL_BUFFERS_QUEUED);
                if (q == 0) {
                    alSourceQueueBuffers(x, 1, &b[rnd(NB)]);
                } else {
                    ALint cur = 0;
                    alGetSourcei(x, AL_BUFFER, &cur);
                    if (cur) {
                        ALuint cb = (ALuint)cur;
                        alSourceQueueBuffers(x, 1, &cb);
                    }
                }
            }
            break;
        case 10: {
            ALint p = srci(x, AL_BUFFERS_PROCESSED);
            if (p > 0) {
                ALuint u[64];
                alSourceUnqueueBuffers(x, p > 64 ? 64 : p, u);
            }
            break;
        }
        case 11:
            if (srci(x, AL_SOURCE_STATE) == AL_PLAYING || srci(x, AL_SOURCE_STATE) == AL_PAUSED) {
                alSourcei(x, AL_SAMPLE_OFFSET, (ALint)rnd(150));
            }
            break;
        case 12:
            if (rnd(4) == 0) {
                alcSuspendContext(ctx);
            } else {
                alcProcessContext(ctx);
            }
            break;
        default:
            alListener3f(AL_POSITION, (ALfloat)rnd(10), 0.0f, 0.0f);
            break;
        }
        if (alGetError() != AL_NO_ERROR) {
            errors++;
        }
        render(1 + rnd(900));
        if (it % 50 == 0) {
            int used[AL_VOICES_MAX] = { 0 };
            for (i = 0; i < NS; i++) {
                ALsource *o = SRC(s[i]);
                if (o->voice >= 0) {
                    shared += used[o->voice]++ > 0;
                    shared += o->state != AL_PLAYING && o->state != AL_PAUSED;
                }
            }
        }
    }
    alcProcessContext(ctx);
    CHECK(errors == 0);
    CHECK(shared == 0);
    CHECK(host_voices_refused == 0);
    CHECK(host_voices_lock_depth == 0);
    printf("workout: 3000 calls on %d sources and 8 voices, %d AL errors, %lu SetSound, %lu SetVol, %lu SetFreq\n", NS,
           errors, host_voices_sounds, host_voices_vols, host_voices_freqs);
    for (i = 0; i < NS; i++) {
        alSourceStop(s[i]);
        alSourcei(s[i], AL_BUFFER, 0);
    }
    alDeleteSources(NS, s);
    alDeleteBuffers(NB, b);
    CHECK_AL(AL_NO_ERROR);
    free(data);
    close_voices();
}

/* ---- Mix against the board ------------------------------------------------------------- */

#define SCENE_STEP 882                  /* the scenes' AL calls, 50 a second */

struct scene {
    const char *name;
    long frames;
    double min_snr;                     /* dB */
    int max_diff;                       /* -1: not checked */
    void (*step)(int voices, long t);   /* the AL calls at step t */
};

static ALuint sc_src[8], sc_buf[16];

static void sc_tone_buffer(ALuint b, int rate, double f, int frames, double amp)
{
    ALshort *pcm = malloc((size_t)frames * 2);
    int i;
    for (i = 0; i < frames; i++) {
        pcm[i] = (ALshort)(amp * sin(2.0 * PI * f * i / rate));
    }
    alBufferData(b, AL_FORMAT_MONO16, pcm, frames * 2, rate);
    free(pcm);
}

static void scene_tone(int voices, long t)
{
    (void)voices;
    if (t == 0) {
        alGenSources(1, sc_src);
        alGenBuffers(1, sc_buf);
        sc_tone_buffer(sc_buf[0], 44100, 440.0, 44100, 12000.0);
        alSourcei(sc_src[0], AL_BUFFER, (ALint)sc_buf[0]);
        alSourcef(sc_src[0], AL_GAIN, 0.8f);
        alSourcePlay(sc_src[0]);
    }
}

static void scene_sweep(int voices, long t)
{
    double a = 2.0 * PI * (double)t / 200.0;
    (void)voices;
    if (t == 0) {
        alGenSources(1, sc_src);
        alGenBuffers(1, sc_buf);
        sc_tone_buffer(sc_buf[0], 22050, 441.0, 22050, 12000.0);
        alSourcei(sc_src[0], AL_BUFFER, (ALint)sc_buf[0]);
        alSourcei(sc_src[0], AL_LOOPING, AL_TRUE);
    }
    /* From the left, behind, right, ahead: as ALTone. */
    alSource3f(sc_src[0], AL_POSITION, (ALfloat)(-2.0 * cos(a)), 0.0f, (ALfloat)(2.0 * sin(a)));
    if (t == 0) {
        alSourcePlay(sc_src[0]);
    }
}

static void scene_stream(int voices, long t)
{
    (void)voices;
    if (t == 0) {
        ALshort *pcm = malloc(4096 * 4);
        int k, i;
        double ph = 0.0;
        alGenSources(1, sc_src);
        alGenBuffers(8, sc_buf);
        for (k = 0; k < 8; k++) {
            for (i = 0; i < 4096; i++) {
                double f = 110.0 + 330.0 * (k * 4096 + i) / 32768.0;
                pcm[2 * i] = (ALshort)(9000.0 * sin(ph));
                pcm[2 * i + 1] = (ALshort)(9000.0 * sin(ph * 0.5));
                ph += 2.0 * PI * f / 32000.0;
            }
            alBufferData(sc_buf[k], AL_FORMAT_STEREO16, pcm, 4096 * 4, 32000);
        }
        free(pcm);
        alSourceQueueBuffers(sc_src[0], 8, sc_buf);
        alSourcePlay(sc_src[0]);
    }
}

static void scene_crowd(int voices, long t)
{
    int i;
    (void)voices;
    if (t == 0) {
        alGenSources(6, sc_src);
        alGenBuffers(6, sc_buf);
        for (i = 0; i < 6; i++) {
            sc_tone_buffer(sc_buf[i], i & 1 ? 22050 : 44100, 110.0 * (i + 1), 22050, 6000.0);
            alSourcei(sc_src[i], AL_BUFFER, (ALint)sc_buf[i]);
            alSourcei(sc_src[i], AL_LOOPING, AL_TRUE);
            alSource3f(sc_src[i], AL_POSITION, (ALfloat)(3.0 * cos(i)), 0.0f, (ALfloat)(3.0 * sin(i)));
            alSourcef(sc_src[i], AL_GAIN, 0.4f + 0.1f * (ALfloat)i);
        }
        alSourcef(sc_src[5], AL_REFERENCE_DISTANCE, 2.0f);
        alSourcePlayv(6, sc_src);
    }
    /* One goes past the listener, with Doppler. */
    alSource3f(sc_src[0], AL_POSITION, (ALfloat)(-10.0 + 20.0 * t / 100.0), 0.0f, -1.0f);
    alSource3f(sc_src[0], AL_VELOCITY, 10.0f, 0.0f, 0.0f);
    if (t == 50) {
        alSourceStop(sc_src[3]);
    }
}

static void scene_eight_bit(int voices, long t)
{
    (void)voices;
    if (t == 0) {
        static ALubyte pcm[11025];
        int i;
        for (i = 0; i < 11025; i++) {
            pcm[i] = (ALubyte)(128.0 + 100.0 * sin(2.0 * PI * 147.0 * i / 11025.0));
        }
        alGenSources(1, sc_src);
        alGenBuffers(1, sc_buf);
        alBufferData(sc_buf[0], AL_FORMAT_MONO8, pcm, sizeof pcm, 11025);
        alSourcei(sc_src[0], AL_BUFFER, (ALint)sc_buf[0]);
        alSourcei(sc_src[0], AL_LOOPING, AL_TRUE);
        alSourcef(sc_src[0], AL_PITCH, 1.5f);
        alSource3f(sc_src[0], AL_POSITION, 1.0f, 0.0f, 0.0f);
        alSourcePlay(sc_src[0]);
    }
}

/* The scene through a device: Mix pulled an update at a time, or the
 * board. */
static void scene_play(const struct scene *sc, int voices, ALshort *pcm)
{
    long t, done = 0;
    int i;
    if (voices) {
        open_voices(32, 64, 1);
    } else {
        host_voices_fitted = 0;
        dev = alcOpenDevice("AHI Mix");
        ctx = alcCreateContext(dev, NULL);
        alcMakeContextCurrent(ctx);
    }
    CHECK(dev && ctx);
    memset(sc_src, 0, sizeof sc_src);
    memset(sc_buf, 0, sizeof sc_buf);
    for (t = 0; done < sc->frames; t++) {
        ALuint n = sc->frames - done < SCENE_STEP ? (ALuint)(sc->frames - done) : SCENE_STEP;
        sc->step(voices, t);
        if (voices) {
            host_voices_render(dev, pcm + 2 * done, n);
        } else {
            al_mix_render(dev, pcm + 2 * done, n);
        }
        done += n;
    }
    CHECK_AL(AL_NO_ERROR);
    for (i = 0; i < 8; i++) {
        if (sc_src[i]) {
            alSourceStop(sc_src[i]);
            alSourcei(sc_src[i], AL_BUFFER, 0);
            alDeleteSources(1, &sc_src[i]);
        }
    }
    for (i = 0; i < 16; i++) {
        if (sc_buf[i]) {
            alDeleteBuffers(1, &sc_buf[i]);
        }
    }
    CHECK_AL(AL_NO_ERROR);
    if (voices) {
        close_voices();
    } else {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(dev);
    }
}

static void test_mix_against_board(void)
{
    /* Where they differ: Mix ramps a change of gain over 64 frames, the
     * board steps; Mix's step is 16.16 (32 kHz into 44.1 kHz: 47554/65536,
     * 0.9/65536 short), the board's 32.32; the board holds a buffer's last
     * frame where Mix interpolates into the next (README, "What the board
     * does differently"); AHI's frequencies are whole hertz (16,537.5
     * plays at 16,538). The limits are a few dB under what was measured. */
    static const struct scene scenes[] = {
        { "a 440 Hz tone at 44.1 kHz", 44100, 80.0, 1, scene_tone },
        { "a sweep round the listener (22.05 kHz)", 4 * 44100, 48.0, -1, scene_sweep },
        { "a stereo stream at 32 kHz", 45000, 36.0, -1, scene_stream },
        { "six sources, one with Doppler", 2 * 44100, 46.0, -1, scene_crowd },
        { "8-bit at 11025 Hz, pitch 1.5", 44100, 29.0, -1, scene_eight_bit },
    };
    size_t k;
    for (k = 0; k < sizeof scenes / sizeof scenes[0]; k++) {
        const struct scene *sc = &scenes[k];
        ALshort *mix = calloc((size_t)sc->frames * 2, sizeof *mix);
        ALshort *board = calloc((size_t)sc->frames * 2, sizeof *board);
        double sig = 0.0, diff = 0.0, snr;
        long i;
        int maxd = 0;
        scene_play(sc, 0, mix);
        scene_play(sc, 1, board);
        for (i = 0; i < 2 * sc->frames; i++) {
            int dd = board[i] - mix[i];
            sig += (double)mix[i] * mix[i];
            diff += (double)dd * dd;
            if (abs(dd) > maxd) {
                maxd = abs(dd);
            }
        }
        snr = diff > 0.0 ? 10.0 * log10(sig / diff) : 200.0;
        printf("Mix against the board: %s: %.1f dB, largest difference %d of 32768\n", sc->name, snr, maxd);
        CHECK(sig > 0.0);
        CHECK(snr >= sc->min_snr);
        if (sc->max_diff >= 0) {
            CHECK(maxd <= sc->max_diff);
        }
        free(mix);
        free(board);
    }
}

int main(void)
{
    test_devices();
    test_settings();
    test_one_shot();
    test_looping();
    test_pause_and_offsets();
    stream_run(64, 1, 1000, 300);
    stream_run(64, 0, 1000, 300);
    stream_run(441, 1, 512, 200);
    stream_run(128, 0, 200, 400);
    test_stream_resampled();
    test_underrun();
    test_virtual();
    test_suspend();
    test_workout();
    test_mix_against_board();
    return check_done("test_al_voices");
}
