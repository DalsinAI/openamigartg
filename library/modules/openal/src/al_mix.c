/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Mix, OpenAL.module's back end for any AHI: the module mixes every
 * playing source of the device into one 16-bit stereo stream at the
 * device's rate, and its output plays the stream (al_ahi.c: ahi.device,
 * double buffered, as SDL's AHI driver plays).
 *
 * The 68k's work for each sample is integer only:
 *   - each source is read at its rate times its pitch, a 16.16 step, with
 *     linear interpolation between neighbouring frames; at the end of a
 *     buffer the next frame is the first of the next queue entry (or of the
 *     first, looping), so a stream has no seam;
 *   - its two gains (al_spatial.c, worked out when something changes) are
 *     Q14 integers up to just under 2.0; a change of gain is ramped over 64
 *     frames, so it doesn't click (4.1, AL_GAIN);
 *   - the sum is kept in 32 bits and clamped to 16 at the end.
 * A source with both gains 0 isn't mixed, but its offset still moves on
 * (4.3.6). The common case (a buffer at the device's rate, pitch 1) needs
 * no interpolation, and has its own loop. */
#include "al_internal.h"

#include <string.h>

#define AL_RAMP_FRAMES 64
#define AL_STEP_MAX (255u << 16)        /* 255 times faster: the pitch is clamped there */

typedef unsigned long long al_u64;

/* A gain as Q14, 0 to 32767 (just under 2.0). */
static ALint al_q14(ALfloat g)
{
    if (!(g > 0.0f)) {
        return 0;
    }
    if (g >= 32767.0f / 16384.0f) {
        return 32767;
    }
    return (ALint)(g * 16384.0f + 0.5f);
}

/* A pitch as 16.16, at least 1. */
static ALuint al_q16(ALfloat p)
{
    ALuint v;
    if (!(p > 0.0f)) {
        return 1;
    }
    if (p >= 255.0f) {
        return AL_STEP_MAX;
    }
    v = (ALuint)(p * 65536.0f + 0.5f);
    return v ? v : 1;
}

static ALint al_sample(const ALbuffer *b, ALuint frame, int ch)
{
    size_t i = (size_t)frame * (size_t)b->channels + (size_t)ch;
    if (b->bits == 8) {
        return ((const ALbyte *)b->data)[i] * 256;
    }
    return ((const ALshort *)b->data)[i];
}

/* The entry after the current one that has frames, for the last frame's
 * interpolation: round to the start when looping. NULL at the end. */
static const ALbuffer *al_next_entry(const ALsource *s)
{
    ALuint k = s->current, n;
    for (n = 0; n < s->queued; n++) {
        const ALbuffer *b;
        k++;
        if (k >= s->queued) {
            if (!s->looping) {
                return NULL;
            }
            k = 0;
        }
        b = s->queue[k];
        if (b && b->frames) {
            return b;
        }
    }
    return NULL;
}

/* n frames of b, one at a time: any format, the gain ramp, and the last
 * frame's interpolation into the next entry. */
static void al_mix_slow(ALsource *s, const ALbuffer *b, ALint *acc, ALuint n, ALuint step)
{
    const ALbuffer *nb = NULL;
    int looked = 0, stereo = b->channels == 2;
    ALuint pos = s->pos, frac = s->frac;
    ALint g0 = s->mix_cur[0], g1 = s->mix_cur[1];
    while (n--) {
        ALint l0, r0, l1, r1, f = (ALint)(frac >> 1), l, r;
        l0 = al_sample(b, pos, 0);
        r0 = stereo ? al_sample(b, pos, 1) : l0;
        if (pos + 1 < b->frames) {
            l1 = al_sample(b, pos + 1, 0);
            r1 = stereo ? al_sample(b, pos + 1, 1) : l1;
        } else {
            if (!looked) {
                nb = al_next_entry(s);
                looked = 1;
            }
            if (nb) {
                l1 = al_sample(nb, 0, 0);
                r1 = nb->channels == 2 ? al_sample(nb, 0, 1) : l1;
            } else {
                l1 = r1 = 0;
            }
        }
        l = l0 + (((l1 - l0) * f) >> 15);
        r = r0 + (((r1 - r0) * f) >> 15);
        acc[0] += (l * (g0 >> 8)) >> 14;
        acc[1] += (r * (g1 >> 8)) >> 14;
        acc += 2;
        if (s->mix_ramp) {
            if (--s->mix_ramp == 0) {
                g0 = s->mix_target[0] << 8;
                g1 = s->mix_target[1] << 8;
            } else {
                g0 += s->mix_inc[0];
                g1 += s->mix_inc[1];
            }
        }
        frac += step;
        pos += frac >> 16;
        frac &= 0xFFFF;
    }
    s->pos = pos;
    s->frac = frac;
    s->mix_cur[0] = g0;
    s->mix_cur[1] = g1;
}

/* The fast loops: no ramp, and every frame's neighbour inside the buffer.
 * MONO and STEREO, 8 and 16 bit, with interpolation (I) and without (N:
 * a step of exactly 1.0 from a whole frame). */
#define AL_FAST_I(name, T, SCALE, CH)                                                       \
    static void name(const T *data, ALuint *ppos, ALuint *pfrac, ALuint step, ALint *acc,   \
                     ALuint n, ALint gl, ALint gr)                                          \
    {                                                                                       \
        const T *p = data + (size_t)*ppos * CH;                                             \
        ALuint frac = *pfrac;                                                               \
        while (n--) {                                                                       \
            ALint f = (ALint)(frac >> 1);                                                   \
            ALint a = p[0] * SCALE, b = p[CH] * SCALE;                                      \
            ALint v = a + (((b - a) * f) >> 15);                                            \
            if (CH == 1) {                                                                  \
                acc[0] += (v * gl) >> 14;                                                   \
                acc[1] += (v * gr) >> 14;                                                   \
            } else {                                                                        \
                ALint a2 = p[1] * SCALE, b2 = p[CH + 1] * SCALE;                            \
                ALint w = a2 + (((b2 - a2) * f) >> 15);                                     \
                acc[0] += (v * gl) >> 14;                                                   \
                acc[1] += (w * gr) >> 14;                                                   \
            }                                                                               \
            acc += 2;                                                                       \
            frac += step;                                                                   \
            p += (frac >> 16) * CH;                                                         \
            frac &= 0xFFFF;                                                                 \
        }                                                                                   \
        *ppos = (ALuint)((p - data) / CH);                                                  \
        *pfrac = frac;                                                                      \
    }

#define AL_FAST_N(name, T, SCALE, CH)                                                       \
    static void name(const T *data, ALuint *ppos, ALint *acc, ALuint n, ALint gl, ALint gr) \
    {                                                                                       \
        const T *p = data + (size_t)*ppos * CH;                                             \
        *ppos += n;                                                                         \
        while (n--) {                                                                       \
            ALint v = p[0] * SCALE;                                                         \
            if (CH == 1) {                                                                  \
                acc[0] += (v * gl) >> 14;                                                   \
                acc[1] += (v * gr) >> 14;                                                   \
            } else {                                                                        \
                acc[0] += (v * gl) >> 14;                                                   \
                acc[1] += ((p[1] * SCALE) * gr) >> 14;                                      \
            }                                                                               \
            acc += 2;                                                                       \
            p += CH;                                                                        \
        }                                                                                   \
    }

AL_FAST_I(al_fast_m16, ALshort, 1, 1)
AL_FAST_I(al_fast_s16, ALshort, 1, 2)
AL_FAST_I(al_fast_m8, ALbyte, 256, 1)
AL_FAST_I(al_fast_s8, ALbyte, 256, 2)
AL_FAST_N(al_copy_m16, ALshort, 1, 1)
AL_FAST_N(al_copy_s16, ALshort, 1, 2)
AL_FAST_N(al_copy_m8, ALbyte, 256, 1)
AL_FAST_N(al_copy_s8, ALbyte, 256, 2)

/* k frames of the current buffer b (k no more than are left in it). */
static void al_mix_buffer(ALsource *s, const ALbuffer *b, ALint *acc, ALuint k, ALuint step)
{
    while (k) {
        ALuint m;
        if (s->mix_ramp) {
            m = k < s->mix_ramp ? k : s->mix_ramp;
            al_mix_slow(s, b, acc, m, step);
        } else if (s->pos + 1 < b->frames) {
            ALint gl = s->mix_cur[0] >> 8, gr = s->mix_cur[1] >> 8;
            al_u64 lim = ((al_u64)(b->frames - 1 - s->pos) << 16) - s->frac;
            al_u64 mm = (lim + step - 1) / step;
            m = mm < k ? (ALuint)mm : k;
            if (!gl && !gr) {
                /* Silent: the offset moves on all the same. */
                al_u64 t = (al_u64)s->frac + (al_u64)m * step;
                s->pos += (ALuint)(t >> 16);
                s->frac = (ALuint)(t & 0xFFFF);
            } else if (step == 0x10000 && s->frac == 0) {
                if (b->bits == 16) {
                    if (b->channels == 1) al_copy_m16(b->data, &s->pos, acc, m, gl, gr);
                    else al_copy_s16(b->data, &s->pos, acc, m, gl, gr);
                } else {
                    if (b->channels == 1) al_copy_m8(b->data, &s->pos, acc, m, gl, gr);
                    else al_copy_s8(b->data, &s->pos, acc, m, gl, gr);
                }
            } else if (b->bits == 16) {
                if (b->channels == 1) al_fast_m16(b->data, &s->pos, &s->frac, step, acc, m, gl, gr);
                else al_fast_s16(b->data, &s->pos, &s->frac, step, acc, m, gl, gr);
            } else {
                if (b->channels == 1) al_fast_m8(b->data, &s->pos, &s->frac, step, acc, m, gl, gr);
                else al_fast_s8(b->data, &s->pos, &s->frac, step, acc, m, gl, gr);
            }
        } else {
            m = 1;
            al_mix_slow(s, b, acc, 1, step);
        }
        acc += 2 * m;
        k -= m;
    }
}

/* One source into acc, n frames: its gains and pitch as they stand, then
 * its queue entry by entry. An entry played to its end is passed at once
 * (AL_BUFFERS_PROCESSED counts it as soon as the call that played it
 * returns), even when n is used up. */
static void al_mix_source(const ALCdevice *d, ALsource *s, ALint *acc, ALuint n)
{
    ALint t0 = al_q14(s->params.left), t1 = al_q14(s->params.right);
    ALuint spins = 0;
    if (s->mix_fresh) {
        s->mix_fresh = AL_FALSE;
        s->mix_cur[0] = t0 << 8;
        s->mix_cur[1] = t1 << 8;
        s->mix_ramp = 0;
    } else if (t0 != s->mix_target[0] || t1 != s->mix_target[1]) {
        s->mix_ramp = AL_RAMP_FRAMES;
        s->mix_inc[0] = ((t0 << 8) - s->mix_cur[0]) / AL_RAMP_FRAMES;
        s->mix_inc[1] = ((t1 << 8) - s->mix_cur[1]) / AL_RAMP_FRAMES;
    }
    s->mix_target[0] = t0;
    s->mix_target[1] = t1;
    s->mix_pitch = al_q16(s->params.pitch);

    while (s->state == AL_PLAYING) {
        const ALbuffer *b = s->current < s->queued ? s->queue[s->current] : NULL;
        ALuint step, k;
        al_u64 kk;
        if (!b || s->pos >= b->frames) {
            s->pos -= b ? b->frames : 0;
            if (!al_source_next(s)) {
                break;
            }
            /* A looping queue with no frames in it would go round for ever. */
            if (++spins > s->queued) {
                al_source_stop_all(s);
                break;
            }
            continue;
        }
        if (!n) {
            break;
        }
        spins = 0;
        step = (ALuint)(((al_u64)s->mix_pitch * (ALuint)b->frequency) / d->frequency);
        if (step < 1) {
            step = 1;
        }
        if (step > AL_STEP_MAX) {
            step = AL_STEP_MAX;
        }
        kk = ((((al_u64)(b->frames - s->pos)) << 16) - s->frac + step - 1) / step;
        k = kk < n ? (ALuint)kk : n;
        al_mix_buffer(s, b, acc, k, step);
        acc += 2 * k;
        n -= k;
    }
}

void al_mix_render(ALCdevice *d, ALshort *out, ALuint frames)
{
    AL_LOCK();
    if (!d->update_frames || !d->mix) {
        /* Not started (no context yet): silence. */
        memset(out, 0, (size_t)frames * 2 * sizeof *out);
        frames = 0;
    }
    while (frames) {
        ALuint n = frames < d->update_frames ? frames : d->update_frames, i;
        ALint *acc = d->mix;
        ALCcontext *c;
        ALsource *s;
        memset(acc, 0, (size_t)n * 2 * sizeof *acc);
        if (d->connected) {
            for (c = d->contexts; c; c = c->next) {
                if (!c->suspended) {
                    al_context_update(c);
                }
                for (s = c->sources; s; s = s->next) {
                    s->dirty &= AL_DIRTY_PARAMS;     /* the rest is read as it mixes */
                    if (s->state == AL_PLAYING) {
                        al_mix_source(d, s, acc, n);
                    }
                }
            }
        }
        for (i = 0; i < 2 * n; i++) {
            ALint v = acc[i];
            out[i] = (ALshort)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        out += 2 * n;
        frames -= n;
    }
    AL_UNLOCK();
}

/* ---- The back end ------------------------------------------------------------------ */

/* Mix reads every change as it renders its next update, so it needs no
 * word of one (update NULL). */
const struct al_backend al_backend_mix = {
    "AHI Mix",
    NULL,                       /* any AHI: always there to try */
    al_out_open,
    al_out_close,
    al_out_start,
    al_out_stop,
    NULL,
    NULL,
    NULL,
    0,
};
