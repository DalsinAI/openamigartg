/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's timeline on a PC (tests/openal/run_host.sh), through Mix:
 *   - a queued stream (4.3.5) must play with no gap and no repeat, sample
 *     for sample, while the test unqueues, refills and queues buffers after
 *     every update as a streaming program does; AL_BUFFERS_PROCESSED must
 *     count each buffer as soon as the update that finished it returns.
 *     Once at the device's rate, once resampled (32 kHz into 44.1 kHz, with
 *     pitch), where every output frame must be the linear interpolation of
 *     the whole stream at that point, across the buffers' seams;
 *   - an underrun stops the source with every buffer processed;
 *   - looping, of one buffer and of a queue (4.3.2, AL_LOOPING);
 *   - offsets (4.3.2): set before playing and while playing, in seconds,
 *     samples and bytes, out of range, across a queue, and reset;
 *   - the NULL buffer in a queue; pitch and a silent source move the offset.
 * The stereo 16-bit sources here play at gain 1.0, which Mix applies
 * exactly (Q14 16384), so the output is the input. Exit 0 when every check
 * passes. */
#include "check.h"
#include "host_out.h"

static ALCdevice *dev;
static ALCcontext *ctx;
static ALshort out[2 * 2048];

#define DEVICE_RATE 44100
#define UPDATE 441                      /* ALC_REFRESH 100 */

/* The test signal: frame n of an endless stream, a value each channel
 * that doesn't repeat within the test. */
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

static ALint srci(ALuint s, ALenum p)
{
    ALint v = -12345;
    alGetSourcei(s, p, &v);
    return v;
}

static ALuint new_source(void)
{
    ALuint s;
    alGenSources(1, &s);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    return s;
}

/* ---- A stream at the device's rate --------------------------------------------------- */

static void test_stream_exact(void)
{
    enum { NB = 4, LEN = 1000, UPDATES = 100 };
    ALuint s = new_source(), b[NB];
    long next = 0, filled = 0, unqueued = 0;
    int u, k, gaps = 0, wrong_processed = 0, wrong_offset = 0;
    alGenBuffers(NB, b);
    for (k = 0; k < NB; k++) {
        fill(b[k], filled, LEN, DEVICE_RATE);
        filled += LEN;
    }
    alSourceQueueBuffers(s, NB, b);
    alSourcePlay(s);
    for (u = 0; u < UPDATES; u++) {
        ALint processed;
        al_mix_render(dev, out, UPDATE);
        for (k = 0; k < UPDATE; k++, next++) {
            if (out[2 * k] != sig_l(next) || out[2 * k + 1] != sig_r(next)) {
                if (gaps++ < 3) {
                    printf("  update %d frame %d: %d %d, expected %d %d\n", u, k, out[2 * k], out[2 * k + 1], sig_l(next), sig_r(next));
                }
            }
        }
        /* Every buffer whose last frame has been mixed is processed now. */
        processed = srci(s, AL_BUFFERS_PROCESSED);
        if (processed != (ALint)(next / LEN - unqueued)) {
            wrong_processed++;
        }
        if (srci(s, AL_SAMPLE_OFFSET) != (ALint)(next - unqueued * LEN)) {
            wrong_offset++;
        }
        CHECK(srci(s, AL_SOURCE_STATE) == AL_PLAYING);
        while (processed-- > 0) {
            ALuint done;
            alSourceUnqueueBuffers(s, 1, &done);
            unqueued++;
            fill(done, filled, LEN, DEVICE_RATE);
            filled += LEN;
            alSourceQueueBuffers(s, 1, &done);
        }
    }
    CHECK(gaps == 0);
    CHECK(wrong_processed == 0);
    CHECK(wrong_offset == 0);
    CHECK(unqueued == next / LEN);
    CHECK_AL(AL_NO_ERROR);
    printf("stream at the device's rate: %ld frames, %ld buffers recycled, %d wrong\n", next, unqueued, gaps);
    alSourceStop(s);
    alDeleteSources(1, &s);
    alDeleteBuffers(NB, b);
}

/* ---- A resampled stream ---------------------------------------------------------------- */

static void test_stream_resampled(float pitch, int rate, int len)
{
    enum { NB = 3, UPDATES = 120 };
    ALuint s = new_source(), b[NB];
    long filled = 0, unqueued = 0, frames = 0;
    unsigned long long p = 0;           /* the reference position, 16.16 */
    ALuint pitch_q16 = (ALuint)(pitch * 65536.0f + 0.5f);
    ALuint step = (ALuint)(((unsigned long long)pitch_q16 * (ALuint)rate) / DEVICE_RATE);
    int u, k, wrong = 0, wrong_processed = 0;
    alGenBuffers(NB, b);
    for (k = 0; k < NB; k++) {
        fill(b[k], filled, len, rate);
        filled += len;
    }
    alSourceQueueBuffers(s, NB, b);
    alSourcef(s, AL_PITCH, pitch);
    alSourcePlay(s);
    for (u = 0; u < UPDATES; u++) {
        ALint processed;
        al_mix_render(dev, out, UPDATE);
        for (k = 0; k < UPDATE; k++, frames++) {
            long i = (long)(p >> 16);
            ALint f = (ALint)((p & 0xFFFF) >> 1);
            ALint l = sig_l(i) + (((sig_l(i + 1) - sig_l(i)) * f) >> 15);
            ALint r = sig_r(i) + (((sig_r(i + 1) - sig_r(i)) * f) >> 15);
            if (out[2 * k] != l || out[2 * k + 1] != r) {
                if (wrong++ < 3) {
                    printf("  update %d frame %d (input %ld + %d/32768): %d %d, expected %d %d\n", u, k, i, f,
                           out[2 * k], out[2 * k + 1], l, r);
                }
            }
            p += step;
        }
        processed = srci(s, AL_BUFFERS_PROCESSED);
        if (processed != (ALint)((long)(p >> 16) / len - unqueued)) {
            wrong_processed++;
        }
        while (processed-- > 0) {
            ALuint done;
            alSourceUnqueueBuffers(s, 1, &done);
            unqueued++;
            fill(done, filled, len, rate);
            filled += len;
            alSourceQueueBuffers(s, 1, &done);
        }
    }
    CHECK(wrong == 0);
    CHECK(wrong_processed == 0);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_PLAYING);
    CHECK_AL(AL_NO_ERROR);
    printf("stream at %d Hz, pitch %.2f: %ld frames out of %ld in, step %u/65536, %ld buffers recycled, %d wrong\n",
           rate, (double)pitch, frames, (long)(p >> 16), step, unqueued, wrong);
    alSourceStop(s);
    alDeleteSources(1, &s);
    alDeleteBuffers(NB, b);
}

/* ---- An underrun ------------------------------------------------------------------------ */

static void test_underrun(void)
{
    ALuint s = new_source(), b[2], back[2];
    int k, ok = 1;
    alGenBuffers(2, b);
    fill(b[0], 0, 300, DEVICE_RATE);
    fill(b[1], 300, 300, DEVICE_RATE);
    alSourceQueueBuffers(s, 2, b);
    alSourcePlay(s);
    al_mix_render(dev, out, 1000);
    for (k = 0; k < 1000; k++) {
        ALshort l = k < 600 ? sig_l(k) : 0, r = k < 600 ? sig_r(k) : 0;
        ok &= out[2 * k] == l && out[2 * k + 1] == r;
    }
    CHECK(ok);
    /* 4.3.5: it stops at the end of its queue, every buffer processed. */
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 2 && srci(s, AL_BUFFERS_QUEUED) == 2);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 0);
    /* Exactly to the end: processed as that update returns. */
    alSourceUnqueueBuffers(s, 2, back);
    CHECK(back[0] == b[0] && back[1] == b[1]);
    alSourceQueueBuffers(s, 2, b);
    alSourcePlay(s);
    al_mix_render(dev, out, 300);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 1 && srci(s, AL_SOURCE_STATE) == AL_PLAYING);
    al_mix_render(dev, out, 300);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 2 && srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    /* Queued again after stopping, and played again: from the start. */
    alSourcePlay(s);
    al_mix_render(dev, out, 2);
    CHECK(out[0] == sig_l(0) && out[2] == sig_l(1));
    CHECK_AL(AL_NO_ERROR);
    alSourceStop(s);
    alDeleteSources(1, &s);
    alDeleteBuffers(2, b);
}

/* ---- Looping ---------------------------------------------------------------------------- */

static void test_looping(void)
{
    ALuint s = new_source(), b[3], back;
    int k, ok = 1;
    alGenBuffers(3, b);
    fill(b[0], 0, 100, DEVICE_RATE);
    alSourcei(s, AL_BUFFER, (ALint)b[0]);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcePlay(s);
    al_mix_render(dev, out, 350);
    for (k = 0; k < 350; k++) {
        ok &= out[2 * k] == sig_l(k % 100) && out[2 * k + 1] == sig_r(k % 100);
    }
    CHECK(ok);
    /* The offset loops back to zero; nothing is processed. */
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 50);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 0);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_PLAYING);
    /* AL_LOOPING can change while playing: it ends at the buffer's end. */
    alSourcei(s, AL_LOOPING, AL_FALSE);
    al_mix_render(dev, out, 100);
    ok = 1;
    for (k = 0; k < 100; k++) {
        ok &= out[2 * k] == (k < 50 ? sig_l(50 + k) : 0);
    }
    CHECK(ok);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    alSourcei(s, AL_BUFFER, 0);

    /* A looping queue: round and round, nothing processed. */
    fill(b[1], 100, 100, DEVICE_RATE);
    fill(b[2], 200, 100, DEVICE_RATE);
    alSourceQueueBuffers(s, 3, b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcePlay(s);
    al_mix_render(dev, out, 750);
    ok = 1;
    for (k = 0; k < 750; k++) {
        ok &= out[2 * k] == sig_l(k % 300) && out[2 * k + 1] == sig_r(k % 300);
    }
    CHECK(ok);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 0 && srci(s, AL_SAMPLE_OFFSET) == 150);
    CHECK(srci(s, AL_BUFFER) == (ALint)b[1]);
    alSourceUnqueueBuffers(s, 1, &back);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceStop(s);
    alSourcei(s, AL_LOOPING, AL_FALSE);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteSources(1, &s);
    alDeleteBuffers(3, b);
    CHECK_AL(AL_NO_ERROR);
}

/* ---- Offsets (4.3.2) --------------------------------------------------------------------- */

static void test_offsets(void)
{
    ALuint s = new_source(), b[3], none = 0;
    ALfloat f;
    int k, ok;
    alGenBuffers(3, b);
    fill(b[0], 0, 1000, DEVICE_RATE);

    /* Set while not playing: waits for alSourcePlay. */
    alSourcei(s, AL_BUFFER, (ALint)b[0]);
    alSourcei(s, AL_SAMPLE_OFFSET, 250);
    CHECK_AL(AL_NO_ERROR);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 250);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_INITIAL);
    alSourcePlay(s);
    al_mix_render(dev, out, 10);
    ok = 1;
    for (k = 0; k < 10; k++) {
        ok &= out[2 * k] == sig_l(250 + k) && out[2 * k + 1] == sig_r(250 + k);
    }
    CHECK(ok);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 260);
    alGetSourcef(s, AL_SEC_OFFSET, &f);
    CHECK_NEAR(f, 260.0 / DEVICE_RATE, 1e-6);
    CHECK(srci(s, AL_BYTE_OFFSET) == 260 * 4);

    /* Set while playing: it jumps there. */
    alSourcef(s, AL_SEC_OFFSET, 0.01f);             /* 441 frames */
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 441);
    al_mix_render(dev, out, 2);
    CHECK(out[0] == sig_l(441) && out[2] == sig_l(442));
    alSourcei(s, AL_BYTE_OFFSET, 2002);             /* whole frames: 500 */
    CHECK(srci(s, AL_BYTE_OFFSET) == 2000 && srci(s, AL_SAMPLE_OFFSET) == 500);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(500) && out[1] == sig_r(500));
    /* Out of range: AL_INVALID_VALUE, and it stays where it was. */
    alSourcei(s, AL_SAMPLE_OFFSET, 1000);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 501);
    alSourcef(s, AL_SEC_OFFSET, 1.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcei(s, AL_SAMPLE_OFFSET, 999);
    CHECK_AL(AL_NO_ERROR);
    /* A fraction of a sample: half way between 10 and 11. */
    alSourcef(s, AL_SAMPLE_OFFSET, 10.5f);
    alGetSourcef(s, AL_SAMPLE_OFFSET, &f);
    CHECK(f == 10.5f);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(10) + (((sig_l(11) - sig_l(10)) * 16384) >> 15));
    /* Pitch 2: two input frames an output frame. */
    alSourcei(s, AL_SAMPLE_OFFSET, 0);
    alSourcef(s, AL_PITCH, 2.0f);
    al_mix_render(dev, out, 100);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 200);
    CHECK(out[2 * 50] == sig_l(100));
    alSourcef(s, AL_PITCH, 1.0f);
    /* Silent, the offset still moves on (4.3.6). */
    alSourcef(s, AL_GAIN, 0.0f);
    al_mix_render(dev, out, 100);
    al_mix_render(dev, out, 100);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 400);
    CHECK(out[2 * 99] == 0);
    alSourcef(s, AL_GAIN, 1.0f);
    al_mix_render(dev, out, 100);                   /* the gain ramps back up in its first 64 frames */
    CHECK(out[0] == 0 && out[2 * 64] == sig_l(464) && out[2 * 99] == sig_l(499));
    /* Paused, it holds still; an offset set now applies at once. */
    alSourcePause(s);
    al_mix_render(dev, out, 100);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 500);
    alSourcei(s, AL_SAMPLE_OFFSET, 700);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 700);
    alSourcePlay(s);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(700));
    /* Stopped: 0. A play from STOPPED starts at the start. */
    alSourceStop(s);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 0);
    alSourcePlay(s);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(0));
    /* "An alSourceStop, alSourceRewind, or a second alSourcePlay call will
     * reset the offset to the beginning." */
    alSourceStop(s);
    alSourcei(s, AL_SAMPLE_OFFSET, 300);
    alSourceRewind(s);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 0);
    alSourcePlay(s);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(0));
    alSourceStop(s);
    alSourcei(s, AL_SAMPLE_OFFSET, 300);
    alSourceStop(s);
    alSourcePlay(s);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(0));
    /* Set past the end while stopped: no error then, and the play starts at 0. */
    alSourceStop(s);
    alSourcei(s, AL_SAMPLE_OFFSET, 5000);
    CHECK_AL(AL_NO_ERROR);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 5000);
    alSourcePlay(s);
    CHECK(srci(s, AL_SAMPLE_OFFSET) == 0);
    alSourceStop(s);
    alSourcei(s, AL_BUFFER, 0);

    /* Across a queue: the buffers passed are processed. */
    fill(b[1], 1000, 1000, DEVICE_RATE);
    fill(b[2], 2000, 1000, DEVICE_RATE);
    alSourceQueueBuffers(s, 3, b);
    alSourcePlay(s);
    alSourcei(s, AL_SAMPLE_OFFSET, 2500);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 2);
    CHECK(srci(s, AL_BUFFER) == (ALint)b[2]);
    al_mix_render(dev, out, 3);
    CHECK(out[0] == sig_l(2500) && out[4] == sig_l(2502));
    alSourcei(s, AL_SAMPLE_OFFSET, 1000);           /* the start of the second */
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 1);
    al_mix_render(dev, out, 1);
    CHECK(out[0] == sig_l(1000));
    alSourcef(s, AL_SEC_OFFSET, 3000.0f / DEVICE_RATE);   /* the end of the queue: out of range */
    CHECK_AL(AL_INVALID_VALUE);
    alSourceStop(s);
    CHECK(srci(s, AL_BUFFERS_PROCESSED) == 3);
    alSourceUnqueueBuffers(s, 3, b);

    /* The NULL buffer in a stream plays nothing, and is processed in turn. */
    {
        ALuint q[3];
        q[0] = b[0];
        q[1] = none;
        q[2] = b[1];
        alSourceQueueBuffers(s, 3, q);
        alSourcePlay(s);
        al_mix_render(dev, out, 1500);
        ok = 1;
        for (k = 0; k < 1500; k++) {
            ok &= out[2 * k] == sig_l(k) && out[2 * k + 1] == sig_r(k);
        }
        CHECK(ok);
        CHECK(srci(s, AL_BUFFERS_PROCESSED) == 2 && srci(s, AL_BUFFERS_QUEUED) == 3);
        alSourceStop(s);
        alSourceUnqueueBuffers(s, 3, q);
        CHECK(q[0] == b[0] && q[1] == 0 && q[2] == b[1]);
    }
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
    alDeleteBuffers(3, b);
}

int main(void)
{
    ALCint attrs[] = { ALC_FREQUENCY, DEVICE_RATE, ALC_REFRESH, 100, 0 };
    al_core_init();
    dev = alcOpenDevice(NULL);
    ctx = alcCreateContext(dev, attrs);
    alcMakeContextCurrent(ctx);
    CHECK(dev && ctx && dev->update_frames == UPDATE);
    test_stream_exact();
    test_stream_resampled(1.0f, 32000, 1000);
    test_stream_resampled(1.37f, 22050, 777);
    test_stream_resampled(0.61f, 48000, 1024);
    test_underrun();
    test_looping();
    test_offsets();
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK(alcCloseDevice(dev) == ALC_TRUE);
    al_core_shutdown();
    return check_done("test_al_stream");
}
