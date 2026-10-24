/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's buffers (OpenAL 1.1 section 5) and the formats they take:
 * the core's four, AL_EXT_FLOAT32's two and AL_EXT_MCFORMATS's fifteen.
 *
 * alBufferData keeps the data the way the mixer reads it:
 *   - 16-bit data as it is (native order: big-endian on the 68k);
 *   - 8-bit mono and stereo data signed (the core's 8-bit data is unsigned,
 *     128 being silence);
 *   - float data as 16 bit, -1.0 to 1.0 onto -32767 to 32767, clamped;
 *   - multichannel data folded to 16-bit stereo. Each side takes its front
 *     channel at 1, the centre and its own rear and side channels at
 *     0.7071, and (6.1) the rear centre at 0.5; LFE is left out. The sum is
 *     scaled by 1 over its weights, so a full-scale signal in every channel
 *     can't clip.
 * AL_BITS, AL_CHANNELS and AL_SIZE give this stored form (5.3.3).
 *
 * Buffer queries follow 5.3.1, which names their own errors: a name that
 * isn't a buffer gives AL_INVALID_OPERATION, and a token the call doesn't
 * take AL_INVALID_VALUE (the general rule, 2.16, would give
 * AL_INVALID_NAME and AL_INVALID_ENUM). */
#include "al_internal.h"
#include "al_attr.h"

#include <stdlib.h>
#include <string.h>

enum { K_U8, K_S16, K_F32 };
enum { L_MONO, L_STEREO, L_QUAD, L_REAR, L_51, L_61, L_71 };

struct al_format {
    ALenum format;
    unsigned char channels;     /* in the data given */
    unsigned char kind;
    unsigned char layout;
};

static const struct al_format al_formats[] = {
    { AL_FORMAT_MONO8, 1, K_U8, L_MONO },
    { AL_FORMAT_MONO16, 1, K_S16, L_MONO },
    { AL_FORMAT_STEREO8, 2, K_U8, L_STEREO },
    { AL_FORMAT_STEREO16, 2, K_S16, L_STEREO },
    { AL_FORMAT_MONO_FLOAT32, 1, K_F32, L_MONO },
    { AL_FORMAT_STEREO_FLOAT32, 2, K_F32, L_STEREO },
    { AL_FORMAT_QUAD8, 4, K_U8, L_QUAD },
    { AL_FORMAT_QUAD16, 4, K_S16, L_QUAD },
    { AL_FORMAT_QUAD32, 4, K_F32, L_QUAD },
    { AL_FORMAT_REAR8, 2, K_U8, L_REAR },
    { AL_FORMAT_REAR16, 2, K_S16, L_REAR },
    { AL_FORMAT_REAR32, 2, K_F32, L_REAR },
    { AL_FORMAT_51CHN8, 6, K_U8, L_51 },
    { AL_FORMAT_51CHN16, 6, K_S16, L_51 },
    { AL_FORMAT_51CHN32, 6, K_F32, L_51 },
    { AL_FORMAT_61CHN8, 7, K_U8, L_61 },
    { AL_FORMAT_61CHN16, 7, K_S16, L_61 },
    { AL_FORMAT_61CHN32, 7, K_F32, L_61 },
    { AL_FORMAT_71CHN8, 8, K_U8, L_71 },
    { AL_FORMAT_71CHN16, 8, K_S16, L_71 },
    { AL_FORMAT_71CHN32, 8, K_F32, L_71 },
};

static const struct al_format *al_format_find(ALenum format)
{
    size_t i;
    for (i = 0; i < sizeof al_formats / sizeof al_formats[0]; i++) {
        if (al_formats[i].format == format) {
            return &al_formats[i];
        }
    }
    return NULL;
}

/* The fold-down weights of one input channel: into the left (side 0) or
 * the right (side 1) output. */
#define W_C 0.70710678f
static float al_fold_weight(int layout, int channel, int side)
{
    switch (layout) {
    case L_QUAD:                /* FL FR RL RR */
        switch (channel) {
        case 0: return side ? 0.0f : 1.0f;
        case 1: return side ? 1.0f : 0.0f;
        case 2: return side ? 0.0f : W_C;
        default: return side ? W_C : 0.0f;
        }
    case L_REAR:                /* RL RR */
        return (channel == side) ? 1.0f : 0.0f;
    case L_51:                  /* FL FR FC LFE RL RR */
    case L_71:                  /* FL FR FC LFE RL RR SL SR */
        switch (channel) {
        case 0: return side ? 0.0f : 1.0f;
        case 1: return side ? 1.0f : 0.0f;
        case 2: return W_C;
        case 3: return 0.0f;
        default: return ((channel & 1) == side) ? W_C : 0.0f;
        }
    default:                    /* L_61: FL FR FC LFE RC SL SR */
        switch (channel) {
        case 0: return side ? 0.0f : 1.0f;
        case 1: return side ? 1.0f : 0.0f;
        case 2: return W_C;
        case 3: return 0.0f;
        case 4: return 0.5f;
        case 5: return side ? 0.0f : W_C;
        default: return side ? W_C : 0.0f;
        }
    }
}

/* One input sample as -1.0 to 1.0 (16 bit: over 32768). */
static float al_sample_float(const struct al_format *f, const void *data, size_t i)
{
    switch (f->kind) {
    case K_U8:
        return (float)((int)((const ALubyte *)data)[i] - 128) / 128.0f;
    case K_S16:
        return (float)((const ALshort *)data)[i] / 32768.0f;
    default: {
        float v;
        memcpy(&v, (const ALubyte *)data + i * 4, 4);
        return v;
    }
    }
}

/* -1.0 to 1.0 onto 16 bit, clamped and rounded; a NaN is silence. */
static ALshort al_float_to_s16(float v)
{
    float s;
    ALint i;
    if (!(v == v)) {
        return 0;
    }
    s = v * 32767.0f;
    if (s >= 32767.0f) {
        return 32767;
    }
    if (s <= -32767.0f) {
        return -32767;
    }
    i = (ALint)(s < 0.0f ? s - 0.5f : s + 0.5f);
    return (ALshort)i;
}

/* data (frames of the given format) into the stored form at out. */
static void al_convert(const struct al_format *f, const void *data, ALuint frames, void *out)
{
    size_t n = (size_t)frames * f->channels, i;
    if (f->layout == L_MONO || f->layout == L_STEREO) {
        switch (f->kind) {
        case K_U8:
            for (i = 0; i < n; i++) {
                ((ALbyte *)out)[i] = (ALbyte)(((const ALubyte *)data)[i] ^ 0x80);
            }
            break;
        case K_S16:
            memcpy(out, data, n * 2);
            break;
        default:
            for (i = 0; i < n; i++) {
                ((ALshort *)out)[i] = al_float_to_s16(al_sample_float(f, data, i));
            }
            break;
        }
        return;
    }
    {
        float wl[8], wr[8], suml = 0.0f, sumr = 0.0f;
        ALuint fr;
        int ch;
        for (ch = 0; ch < f->channels; ch++) {
            wl[ch] = al_fold_weight(f->layout, ch, 0);
            wr[ch] = al_fold_weight(f->layout, ch, 1);
            suml += wl[ch];
            sumr += wr[ch];
        }
        for (ch = 0; ch < f->channels; ch++) {
            wl[ch] /= suml;
            wr[ch] /= sumr;
        }
        for (fr = 0; fr < frames; fr++) {
            float l = 0.0f, r = 0.0f;
            size_t base = (size_t)fr * f->channels;
            for (ch = 0; ch < f->channels; ch++) {
                float v = al_sample_float(f, data, base + (size_t)ch);
                l += wl[ch] * v;
                r += wr[ch] * v;
            }
            ((ALshort *)out)[2 * fr] = al_float_to_s16(l);
            ((ALshort *)out)[2 * fr + 1] = al_float_to_s16(r);
        }
    }
}

ALbuffer *al_buffer_get(ALCcontext *context, ALuint name)
{
    ALbuffer *b = al_names_get(&al_buffer_names, name);
    return (b && b->device == context->device) ? b : NULL;
}

void al_buffer_free(ALbuffer *buffer)
{
    free(buffer->data);
    free(buffer);
}

/* ---- Names (5.2) ---------------------------------------------------------------- */

void AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
{
    ALsizei i;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (n < 0 || (n > 0 && !buffers)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    for (i = 0; i < n; i++) {
        ALbuffer *b = calloc(1, sizeof *b);
        if (b) {
            b->device = c->device;
            b->channels = 1;    /* table 5-3 and 5-4's defaults */
            b->bits = 16;
            b->name = al_names_add(&al_buffer_names, b);
        }
        if (!b || !b->name) {
            /* None made, then (2.12). */
            free(b);
            while (i-- > 0) {
                ALbuffer *old = al_names_get(&al_buffer_names, buffers[i]);
                al_names_remove(&al_buffer_names, buffers[i]);
                al_buffer_free(old);
                c->device->buffers--;
            }
            al_set_error(c, AL_OUT_OF_MEMORY);
            al_leave(c);
            return;
        }
        c->device->buffers++;
        buffers[i] = b->name;
    }
    al_leave(c);
}

void AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
{
    ALsizei i;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (n < 0 || (n > 0 && !buffers)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    /* 2.13: any name not valid, and none is deleted; 0 is a legal NOP
     * (5.2.2). A buffer in a queue can't be deleted (2.13, 5.1). */
    for (i = 0; i < n; i++) {
        if (buffers[i] && !al_buffer_get(c, buffers[i])) {
            al_set_error(c, AL_INVALID_NAME);
            al_leave(c);
            return;
        }
    }
    for (i = 0; i < n; i++) {
        ALbuffer *b = buffers[i] ? al_buffer_get(c, buffers[i]) : NULL;
        if (b && b->refs) {
            al_set_error(c, AL_INVALID_OPERATION);
            al_leave(c);
            return;
        }
    }
    for (i = 0; i < n; i++) {
        ALbuffer *b = buffers[i] ? al_buffer_get(c, buffers[i]) : NULL;
        if (b) {                /* not a name given twice */
            al_names_remove(&al_buffer_names, b->name);
            al_buffer_free(b);
            c->device->buffers--;
        }
    }
    al_leave(c);
}

ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
{
    ALboolean r;
    ALCcontext *c = al_enter();
    if (!c) {
        return AL_FALSE;
    }
    /* AL_NONE is a valid buffer name (4.3.2, AL_BUFFER). */
    r = (buffer == 0 || al_buffer_get(c, buffer)) ? AL_TRUE : AL_FALSE;
    al_leave(c);
    return r;
}

/* ---- Data (5.3.4) ---------------------------------------------------------------- */

void AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    const struct al_format *f;
    ALbuffer *b;
    ALuint frame_bytes, frames;
    ALint channels, bits;
    void *stored;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    b = al_buffer_get(c, buffer);
    if (!b) {
        al_set_error(c, AL_INVALID_NAME);
        al_leave(c);
        return;
    }
    f = al_format_find(format);
    if (!f) {
        al_set_error(c, AL_INVALID_ENUM);
        al_leave(c);
        return;
    }
    frame_bytes = (ALuint)f->channels * (f->kind == K_U8 ? 1u : f->kind == K_S16 ? 2u : 4u);
    if (size < 0 || freq <= 0 || (ALuint)size % frame_bytes || (size > 0 && !data)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    if (size == 0) {            /* table 5-2: "Setting an AL_SIZE of 0 is a legal NOP." */
        al_leave(c);
        return;
    }
    /* 5.1: only an UNUSED buffer (in no queue) can be changed. */
    if (b->refs) {
        al_set_error(c, AL_INVALID_OPERATION);
        al_leave(c);
        return;
    }
    frames = (ALuint)size / frame_bytes;
    channels = f->channels == 1 ? 1 : 2;
    /* 8 bit stays 8 bit only unfolded: a fold sums channels into 16 bit. */
    bits = (f->kind == K_U8 && (f->layout == L_MONO || f->layout == L_STEREO)) ? 8 : 16;
    stored = malloc((size_t)frames * (size_t)channels * (size_t)(bits / 8));
    if (!stored) {
        al_set_error(c, AL_OUT_OF_MEMORY);
        al_leave(c);
        return;
    }
    al_convert(f, data, frames, stored);
    free(b->data);
    b->data = stored;
    b->format = format;
    b->frequency = freq;
    b->channels = channels;
    b->bits = bits;
    b->frames = frames;
    al_leave(c);
}

/* ---- Attributes (5.3) ------------------------------------------------------------- */

static int al_buffer_forms(ALenum param)
{
    switch (param) {
    case AL_FREQUENCY:
    case AL_BITS:
    case AL_CHANNELS:
    case AL_SIZE:
        return AL_I | AL_IV | AL_READONLY;      /* set only by alBufferData */
    default:
        return 0;
    }
}

/* The core has no attribute a set call takes (5.3.2: for extensions). */
static void al_buffer_set(ALuint buffer, ALenum param, int form, const void *values)
{
    ALenum e;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (!al_buffer_get(c, buffer)) {
        al_set_error(c, AL_INVALID_NAME);
    } else if ((e = al_set_check(al_buffer_forms(param), form, param)) != AL_NO_ERROR) {
        al_set_error(c, e);
    } else if (!values) {
        al_set_error(c, AL_INVALID_VALUE);
    }
    al_leave(c);
}

void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat value)
{
    al_buffer_set(buffer, param, AL_F, &value);
}

void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    (void)value2;
    (void)value3;
    al_buffer_set(buffer, param, AL_3F, &value1);
}

void AL_APIENTRY alBufferfv(ALuint buffer, ALenum param, const ALfloat *values)
{
    al_buffer_set(buffer, param, AL_FV, values);
}

void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value)
{
    al_buffer_set(buffer, param, AL_I, &value);
}

void AL_APIENTRY alBuffer3i(ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3)
{
    (void)value2;
    (void)value3;
    al_buffer_set(buffer, param, AL_3I, &value1);
}

void AL_APIENTRY alBufferiv(ALuint buffer, ALenum param, const ALint *values)
{
    al_buffer_set(buffer, param, AL_IV, values);
}

/* A get call: the value, or 0 with the error set. */
static int al_buffer_get_param(ALCcontext *c, ALuint buffer, ALenum param, int form, ALint *value)
{
    ALbuffer *b = al_buffer_get(c, buffer);
    if (!b) {
        al_set_error(c, AL_INVALID_OPERATION);
        return 0;
    }
    if (al_get_check(al_buffer_forms(param), form) != AL_NO_ERROR) {
        al_set_error(c, AL_INVALID_VALUE);
        return 0;
    }
    switch (param) {
    case AL_FREQUENCY:
        *value = b->frequency;
        break;
    case AL_BITS:
        *value = b->bits;
        break;
    case AL_CHANNELS:
        *value = b->channels;
        break;
    default:
        *value = (ALint)(b->frames * (ALuint)b->channels * (ALuint)(b->bits / 8));
        break;
    }
    return 1;
}

void AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
{
    ALint v;
    ALCcontext *c;
    if (!value || !(c = al_enter())) {
        return;
    }
    if (al_buffer_get_param(c, buffer, param, AL_I, &v)) {
        *value = v;
    }
    al_leave(c);
}

void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum param, ALint *values)
{
    ALint v;
    ALCcontext *c;
    if (!values || !(c = al_enter())) {
        return;
    }
    if (al_buffer_get_param(c, buffer, param, AL_IV, &v)) {
        *values = v;
    }
    al_leave(c);
}

static void al_buffer_get_none(ALuint buffer, ALenum param, int form)
{
    ALint v;
    ALCcontext *c = al_enter();
    if (c) {
        al_buffer_get_param(c, buffer, param, form, &v);
        al_leave(c);
    }
}

void AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value)
{
    if (value) {
        al_buffer_get_none(buffer, param, AL_F);
    }
}

void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    if (value1 && value2 && value3) {
        al_buffer_get_none(buffer, param, AL_3F);
    }
}

void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum param, ALfloat *values)
{
    if (values) {
        al_buffer_get_none(buffer, param, AL_FV);
    }
}

void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    if (value1 && value2 && value3) {
        al_buffer_get_none(buffer, param, AL_3I);
    }
}
