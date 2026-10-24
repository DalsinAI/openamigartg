/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's sources (OpenAL 1.1 section 4.3): names, attributes, the
 * buffer queue, the four states and their transitions, and offsets.
 *
 * The timeline (which queue entry plays, and where in it) is the core's;
 * the back end moves it on as it plays (al_source_next). The entries before
 * the current one are processed. A stopped source has every entry processed
 * (4.3.2); a looping one none, as each will play again.
 *
 * Offsets (4.3.2, AL_SEC_OFFSET and the others) count from the start of the
 * queue. Set on an active (playing or paused) source, the source jumps
 * there at once, or the call fails with AL_INVALID_VALUE when the queue is
 * shorter; set on an inactive one, it waits for the next alSourcePlay (and
 * is forgotten by alSourceStop, alSourceRewind or a play that finds the
 * queue shorter). AL_SEC_OFFSET and AL_BYTE_OFFSET are counted in the
 * queue's stored form (al_buffer.c), as AL_SIZE is. */
#include "al_internal.h"
#include "al_attr.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- The core's calls ------------------------------------------------------------ */

ALsource *al_source_get(ALCcontext *context, ALuint name)
{
    ALsource *s = al_names_get(&al_source_names, name);
    return (s && s->context == context) ? s : NULL;
}

void al_source_touch(ALsource *source, unsigned dirty)
{
    source->dirty |= dirty;
    source->context->changed = AL_TRUE;
}

static void al_queue_clear(ALsource *s)
{
    ALuint i;
    for (i = 0; i < s->queued; i++) {
        if (s->queue[i]) {
            s->queue[i]->refs--;
        }
    }
    s->queued = 0;
    s->current = 0;
    s->pos = s->frac = 0;
}

void al_source_free(ALsource *source)
{
    ALCcontext *c = source->context;
    if (c->device->backend->forget) {
        c->device->backend->forget(source);
    }
    al_queue_clear(source);
    free(source->queue);
    if (source->prev) {
        source->prev->next = source->next;
    } else {
        c->sources = source->next;
    }
    if (source->next) {
        source->next->prev = source->prev;
    }
    c->nsources--;
    al_names_remove(&al_source_names, source->name);
    free(source);
}

ALuint al_source_processed(const ALsource *s)
{
    switch (s->state) {
    case AL_STOPPED:
        return s->queued;
    case AL_INITIAL:
        return 0;
    default:
        return s->looping ? 0 : s->current;
    }
}

void al_source_stop_all(ALsource *s)
{
    s->state = AL_STOPPED;
    s->current = s->queued;
    s->pos = s->frac = 0;
    s->pending_type = 0;
    al_source_touch(s, AL_DIRTY_STATE);
}

int al_source_next(ALsource *s)
{
    s->current++;
    if (s->current >= s->queued) {
        if (s->looping && s->queued) {
            s->current = 0;     /* 4.3.2, AL_LOOPING: back to the start of the queue */
        } else {
            al_source_stop_all(s);
            return 0;
        }
    }
    s->dirty |= AL_DIRTY_STATE;
    return 1;
}

/* Time going on for a source no back end plays (Voices' virtual sources):
 * out frames of the output at out_rate, at the source's rate times its
 * pitch as a 16.16 step (Mix's: al_mix.c), entry by entry, looping or
 * stopping at the end of the queue as playing would. */
void al_source_advance(ALsource *s, ALuint out, ALuint out_rate)
{
    const ALbuffer *f = al_source_format(s);
    ALdouble p = s->params.pitch;
    unsigned long long left, step;
    ALuint spins = 0;
    if (!f || !out || !out_rate || s->state != AL_PLAYING) {
        return;
    }
    p = p > 0.0 ? (p >= 255.0 ? 255.0 : p) : 0.0;
    step = (unsigned long long)(p * 65536.0 + 0.5);
    step = step * (ALuint)f->frequency / out_rate;
    if (step < 1) {
        step = 1;
    }
    if (step > (255ull << 16)) {
        step = 255ull << 16;
    }
    left = (unsigned long long)out * step;      /* 16.16 frames to move */
    while (s->state == AL_PLAYING) {
        const ALbuffer *b = s->current < s->queued ? s->queue[s->current] : NULL;
        unsigned long long room;
        if (!b || s->pos >= b->frames) {
            if (!al_source_next(s)) {
                break;
            }
            s->pos = s->frac = 0;
            if (++spins > s->queued) {      /* a looping queue with no frames */
                al_source_stop_all(s);
                break;
            }
            continue;
        }
        spins = 0;
        room = ((unsigned long long)(b->frames - s->pos) << 16) - s->frac;
        if (left < room) {
            unsigned long long t = ((unsigned long long)s->pos << 16) + s->frac + left;
            s->pos = (ALuint)(t >> 16);
            s->frac = (ALuint)(t & 0xFFFF);
            break;
        }
        left -= room;
        s->pos = b->frames;
    }
}

/* The first buffer with data: it gives the queue's format (4.3.5: every
 * buffer in a queue has the same). */
const ALbuffer *al_source_format(const ALsource *s)
{
    ALuint i;
    for (i = 0; i < s->queued; i++) {
        if (s->queue[i] && s->queue[i]->format) {
            return s->queue[i];
        }
    }
    return NULL;
}

ALint al_source_channels(const ALsource *s)
{
    const ALbuffer *b = al_source_format(s);
    return b ? b->channels : 1;
}

static ALuint al_entry_frames(const ALbuffer *b)
{
    return b ? b->frames : 0;
}

static ALdouble al_queue_frames(const ALsource *s, ALuint upto)
{
    ALdouble n = 0.0;
    ALuint i;
    for (i = 0; i < upto && i < s->queued; i++) {
        n += al_entry_frames(s->queue[i]);
    }
    return n;
}

/* An offset of type as frames, or < 0 when the queue has no format. */
static ALdouble al_offset_frames(const ALsource *s, ALenum type, ALdouble value)
{
    const ALbuffer *f = al_source_format(s);
    if (!f) {
        return -1.0;
    }
    switch (type) {
    case AL_SEC_OFFSET: {
        /* Seconds come as a float: 0.01f is 440.99999 frames at 44100 Hz,
         * and means 441. Within a float's precision of a whole frame, it
         * is that frame. */
        ALdouble frames = value * f->frequency, whole = floor(frames + 0.5);
        return fabs(frames - whole) <= frames * (1.0 / 8388608.0) ? whole : frames;
    }
    case AL_SAMPLE_OFFSET:
        return value;
    default:                    /* AL_BYTE_OFFSET: whole frames */
        return floor(value / (f->channels * (f->bits / 8)));
    }
}

/* Jump to frames from the start of the queue (less than its length): the
 * entries passed are processed. */
static void al_source_seek(ALsource *s, ALdouble frames)
{
    ALuint k = 0;
    ALdouble whole;
    while (k + 1 < s->queued && frames >= al_entry_frames(s->queue[k])) {
        frames -= al_entry_frames(s->queue[k]);
        k++;
    }
    /* To the mixer's step, 1/65536 of a frame, rounded. */
    whole = floor(frames);
    s->current = k;
    s->pos = (ALuint)whole;
    s->frac = (ALuint)((frames - whole) * 65536.0 + 0.5);
    if (s->frac > 0xFFFF) {
        s->pos++;
        s->frac = 0;
    }
}

static ALdouble al_source_offset(const ALsource *s, ALenum type)
{
    const ALbuffer *f = al_source_format(s);
    ALdouble frames;
    if (!f) {
        return 0.0;
    }
    if (s->state == AL_PLAYING || s->state == AL_PAUSED) {
        frames = al_queue_frames(s, s->current) + s->pos + s->frac / 65536.0;
    } else if (s->pending_type) {
        frames = al_offset_frames(s, s->pending_type, s->pending_value);
    } else {
        return 0.0;
    }
    switch (type) {
    case AL_SEC_OFFSET:
        return frames / f->frequency;
    case AL_SAMPLE_OFFSET:
        return frames;
    default:
        return floor(frames) * f->channels * (f->bits / 8);
    }
}

static ALenum al_source_set_offset(ALsource *s, ALenum type, ALdouble value)
{
    ALdouble frames;
    if (!(value >= 0.0)) {
        return AL_INVALID_VALUE;
    }
    if (s->state != AL_PLAYING && s->state != AL_PAUSED) {
        s->pending_type = type;
        s->pending_value = value;
        return AL_NO_ERROR;
    }
    frames = al_offset_frames(s, type, value);
    if (frames < 0.0 || frames >= al_queue_frames(s, s->queued)) {
        return AL_INVALID_VALUE;
    }
    al_source_seek(s, frames);
    al_source_touch(s, AL_DIRTY_STATE | AL_DIRTY_SEEK);
    return AL_NO_ERROR;
}

/* ---- Names (4.3.1) --------------------------------------------------------------- */

static void al_source_defaults(ALsource *s)
{
    s->gain = 1.0f;
    s->max_gain = 1.0f;
    s->pitch = 1.0f;
    s->ref_distance = 1.0f;
    s->rolloff = 1.0f;
    s->max_distance = FLT_MAX;
    s->cone_inner = 360.0f;
    s->cone_outer = 360.0f;
    s->type = AL_UNDETERMINED;
    s->state = AL_INITIAL;
    s->voice = -1;
}

void AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
{
    ALsizei i;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    /* 2.12: AL_INVALID_VALUE when the names can't be stored or the objects
     * can't be made for want of something other than memory: here the
     * context's source count (ALC_MONO_SOURCES + ALC_STEREO_SOURCES). */
    if (n < 0 || (n > 0 && !sources) || (ALuint)n > c->max_sources - c->nsources) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    for (i = 0; i < n; i++) {
        ALsource *s = calloc(1, sizeof *s);
        if (s) {
            s->name = al_names_add(&al_source_names, s);
        }
        if (!s || !s->name) {
            free(s);
            while (i-- > 0) {
                al_source_free(al_names_get(&al_source_names, sources[i]));
            }
            al_set_error(c, AL_OUT_OF_MEMORY);
            al_leave(c);
            return;
        }
        s->context = c;
        al_source_defaults(s);
        s->next = c->sources;
        if (c->sources) {
            c->sources->prev = s;
        }
        c->sources = s;
        c->nsources++;
        al_source_params(c, s, 1, &s->params);
        al_source_touch(s, AL_DIRTY_PARAMS);
        sources[i] = s->name;
    }
    al_leave(c);
}

void AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    ALsizei i;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (n < 0 || (n > 0 && !sources)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    for (i = 0; i < n; i++) {
        if (!al_source_get(c, sources[i])) {
            al_set_error(c, AL_INVALID_NAME);
            al_leave(c);
            return;
        }
    }
    /* 4.3.1: a playing source is stopped, then deleted. */
    for (i = 0; i < n; i++) {
        ALsource *s = al_source_get(c, sources[i]);
        if (s) {                /* not a name given twice */
            al_source_free(s);
        }
    }
    c->changed = AL_TRUE;
    al_leave(c);
}

ALboolean AL_APIENTRY alIsSource(ALuint source)
{
    ALboolean r;
    ALCcontext *c = al_enter();
    if (!c) {
        return AL_FALSE;
    }
    r = al_source_get(c, source) ? AL_TRUE : AL_FALSE;
    al_leave(c);
    return r;
}

/* ---- Attributes (4.3.2) ------------------------------------------------------------ */

static int al_source_forms(ALenum param, int *count)
{
    *count = 1;
    switch (param) {
    case AL_PITCH:
    case AL_GAIN:
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_CONE_OUTER_GAIN:
        return AL_F | AL_FV;
    case AL_REFERENCE_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_MAX_DISTANCE:
    case AL_CONE_INNER_ANGLE:
    case AL_CONE_OUTER_ANGLE:
    case AL_SEC_OFFSET:
    case AL_SAMPLE_OFFSET:
    case AL_BYTE_OFFSET:
        return AL_F | AL_FV | AL_I | AL_IV;
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION:
        *count = 3;
        return AL_3F | AL_FV | AL_3I | AL_IV;
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
    case AL_BUFFER:
        return AL_I | AL_IV;
    case AL_SOURCE_STATE:
    case AL_BUFFERS_QUEUED:
    case AL_BUFFERS_PROCESSED:
    case AL_SOURCE_TYPE:
        return AL_I | AL_IV | AL_READONLY;
    default:
        *count = 0;
        return 0;
    }
}

/* alSourcei(AL_BUFFER) (4.3.2, "Current Buffer"). */
static ALenum al_source_set_buffer(ALCcontext *c, ALsource *s, ALint name)
{
    ALbuffer *b = NULL;
    if (s->state == AL_PLAYING || s->state == AL_PAUSED) {
        return AL_INVALID_OPERATION;
    }
    if (name != 0) {
        b = name > 0 ? al_buffer_get(c, (ALuint)name) : NULL;
        if (!b) {
            return AL_INVALID_VALUE;
        }
    }
    if (b && s->room < 1) {
        ALbuffer **q = realloc(s->queue, 4 * sizeof *q);
        if (!q) {
            return AL_OUT_OF_MEMORY;
        }
        s->queue = q;
        s->room = 4;
    }
    al_queue_clear(s);
    if (b) {
        s->queue[0] = b;
        b->refs++;
        s->queued = 1;
        s->type = AL_STATIC;
    } else {
        s->type = AL_UNDETERMINED;
    }
    /* A stopped source stays stopped, all its entries processed. */
    s->current = s->state == AL_STOPPED ? s->queued : 0;
    al_source_touch(s, AL_DIRTY_QUEUE | AL_DIRTY_PARAMS);
    return AL_NO_ERROR;
}

/* A set call: d holds the values as doubles, i the same as ints (from an
 * integer call, exact), or both are NULL. */
static void al_source_set(ALuint source, ALenum param, int form, const ALdouble *d, const ALint *iv)
{
    int count, k;
    ALenum e;
    ALsource *s;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    s = al_source_get(c, source);
    if (!s) {
        al_set_error(c, AL_INVALID_NAME);
        al_leave(c);
        return;
    }
    e = al_set_check(al_source_forms(param, &count), form, param);
    if (e == AL_NO_ERROR && !d) {
        e = AL_INVALID_VALUE;
    }
    for (k = 0; e == AL_NO_ERROR && k < count; k++) {
        if (!AL_FINITE(d[k])) {
            e = AL_INVALID_VALUE;
        }
    }
    if (e != AL_NO_ERROR) {
        al_set_error(c, e);
        al_leave(c);
        return;
    }
    switch (param) {
    case AL_PITCH:
        if (!(d[0] > 0.0)) {
            e = AL_INVALID_VALUE;
        } else {
            s->pitch = (ALfloat)d[0];
        }
        break;
    case AL_GAIN:
    case AL_REFERENCE_DISTANCE:
    case AL_ROLLOFF_FACTOR:
    case AL_MAX_DISTANCE:
        if (!(d[0] >= 0.0)) {
            e = AL_INVALID_VALUE;
        } else if (param == AL_GAIN) {
            s->gain = (ALfloat)d[0];
        } else if (param == AL_REFERENCE_DISTANCE) {
            s->ref_distance = (ALfloat)d[0];
        } else if (param == AL_ROLLOFF_FACTOR) {
            s->rolloff = (ALfloat)d[0];
        } else {
            s->max_distance = (ALfloat)d[0];
        }
        break;
    case AL_MIN_GAIN:
    case AL_MAX_GAIN:
    case AL_CONE_OUTER_GAIN:
        if (!(d[0] >= 0.0 && d[0] <= 1.0)) {
            e = AL_INVALID_VALUE;
        } else if (param == AL_MIN_GAIN) {
            s->min_gain = (ALfloat)d[0];
        } else if (param == AL_MAX_GAIN) {
            s->max_gain = (ALfloat)d[0];
        } else {
            s->cone_outer_gain = (ALfloat)d[0];
        }
        break;
    case AL_CONE_INNER_ANGLE:
        s->cone_inner = (ALfloat)d[0];
        break;
    case AL_CONE_OUTER_ANGLE:
        s->cone_outer = (ALfloat)d[0];
        break;
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION: {
        ALfloat *v = param == AL_POSITION ? s->position : param == AL_VELOCITY ? s->velocity : s->direction;
        for (k = 0; k < 3; k++) {
            v[k] = (ALfloat)d[k];
        }
        break;
    }
    case AL_SOURCE_RELATIVE:
    case AL_LOOPING:
        if (iv[0] != AL_FALSE && iv[0] != AL_TRUE) {
            e = AL_INVALID_VALUE;
        } else if (param == AL_SOURCE_RELATIVE) {
            s->relative = (ALboolean)iv[0];
        } else {
            s->looping = (ALboolean)iv[0];
            al_source_touch(s, AL_DIRTY_STATE | AL_DIRTY_LOOP);
        }
        break;
    case AL_BUFFER:
        e = al_source_set_buffer(c, s, iv[0]);
        break;
    default:                    /* the offsets */
        e = al_source_set_offset(s, param, d[0]);
        break;
    }
    if (e != AL_NO_ERROR) {
        al_set_error(c, e);
    } else if (param != AL_LOOPING && param != AL_BUFFER && param != AL_SEC_OFFSET &&
               param != AL_SAMPLE_OFFSET && param != AL_BYTE_OFFSET) {
        al_source_touch(s, AL_DIRTY_PARAMS);
    }
    al_leave(c);
}

void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
{
    ALdouble d = value;
    ALint i = 0;
    al_source_set(source, param, AL_F, &d, &i);
}

void AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ALdouble d[3];
    ALint i[3] = { 0, 0, 0 };
    d[0] = value1;
    d[1] = value2;
    d[2] = value3;
    al_source_set(source, param, AL_3F, d, i);
}

void AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
{
    ALdouble d[3];
    ALint i[3] = { 0, 0, 0 };
    int count, k;
    if (!values) {
        al_source_set(source, param, AL_FV, NULL, NULL);
        return;
    }
    al_source_forms(param, &count);
    for (k = 0; k < count; k++) {
        d[k] = values[k];
    }
    al_source_set(source, param, AL_FV, d, i);
}

void AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
    ALdouble d = value;
    al_source_set(source, param, AL_I, &d, &value);
}

void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3)
{
    ALdouble d[3];
    ALint i[3];
    i[0] = value1;
    i[1] = value2;
    i[2] = value3;
    d[0] = value1;
    d[1] = value2;
    d[2] = value3;
    al_source_set(source, param, AL_3I, d, i);
}

void AL_APIENTRY alSourceiv(ALuint source, ALenum param, const ALint *values)
{
    ALdouble d[3];
    ALint i[3];
    int count, k;
    if (!values) {
        al_source_set(source, param, AL_IV, NULL, NULL);
        return;
    }
    al_source_forms(param, &count);
    for (k = 0; k < count; k++) {
        i[k] = values[k];
        d[k] = values[k];
    }
    al_source_set(source, param, AL_IV, d, i);
}

/* A get call: the values into d (3 room), their count, or -1 with the
 * error set. */
static int al_source_get_param(ALCcontext *c, ALuint source, ALenum param, int form, ALdouble *d)
{
    int count;
    ALenum e;
    ALsource *s = al_source_get(c, source);
    if (!s) {
        al_set_error(c, AL_INVALID_NAME);
        return -1;
    }
    e = al_get_check(al_source_forms(param, &count), form);
    if (e != AL_NO_ERROR) {
        al_set_error(c, e);
        return -1;
    }
    switch (param) {
    case AL_PITCH: d[0] = s->pitch; break;
    case AL_GAIN: d[0] = s->gain; break;
    case AL_MIN_GAIN: d[0] = s->min_gain; break;
    case AL_MAX_GAIN: d[0] = s->max_gain; break;
    case AL_CONE_OUTER_GAIN: d[0] = s->cone_outer_gain; break;
    case AL_REFERENCE_DISTANCE: d[0] = s->ref_distance; break;
    case AL_ROLLOFF_FACTOR: d[0] = s->rolloff; break;
    case AL_MAX_DISTANCE: d[0] = s->max_distance; break;
    case AL_CONE_INNER_ANGLE: d[0] = s->cone_inner; break;
    case AL_CONE_OUTER_ANGLE: d[0] = s->cone_outer; break;
    case AL_POSITION:
    case AL_VELOCITY:
    case AL_DIRECTION: {
        const ALfloat *v = param == AL_POSITION ? s->position : param == AL_VELOCITY ? s->velocity : s->direction;
        d[0] = v[0];
        d[1] = v[1];
        d[2] = v[2];
        break;
    }
    case AL_SOURCE_RELATIVE: d[0] = s->relative; break;
    case AL_LOOPING: d[0] = s->looping; break;
    case AL_BUFFER: {
        /* The current entry: the attached buffer of a static source. */
        ALuint k = s->current < s->queued ? s->current : s->queued - 1;
        d[0] = (s->queued && s->queue[k]) ? s->queue[k]->name : 0;
        break;
    }
    case AL_SOURCE_STATE: d[0] = s->state; break;
    case AL_SOURCE_TYPE: d[0] = s->type; break;
    /* 4.3.2: both are "0 if the current and only buffer name is 0". */
    case AL_BUFFERS_QUEUED: d[0] = (s->queued == 1 && !s->queue[0]) ? 0 : s->queued; break;
    case AL_BUFFERS_PROCESSED: d[0] = (s->queued == 1 && !s->queue[0]) ? 0 : al_source_processed(s); break;
    default: d[0] = al_source_offset(s, param); break;
    }
    return count;
}

void AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
{
    ALdouble d[3];
    ALCcontext *c;
    if (!value || !(c = al_enter())) {
        return;
    }
    if (al_source_get_param(c, source, param, AL_F, d) > 0) {
        *value = (ALfloat)d[0];
    }
    al_leave(c);
}

void AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALdouble d[3];
    ALCcontext *c;
    if (!value1 || !value2 || !value3 || !(c = al_enter())) {
        return;
    }
    if (al_source_get_param(c, source, param, AL_3F, d) > 0) {
        *value1 = (ALfloat)d[0];
        *value2 = (ALfloat)d[1];
        *value3 = (ALfloat)d[2];
    }
    al_leave(c);
}

void AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
{
    ALdouble d[3];
    int n, k;
    ALCcontext *c;
    if (!values || !(c = al_enter())) {
        return;
    }
    n = al_source_get_param(c, source, param, AL_FV, d);
    for (k = 0; k < n; k++) {
        values[k] = (ALfloat)d[k];
    }
    al_leave(c);
}

void AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
{
    ALdouble d[3];
    ALCcontext *c;
    if (!value || !(c = al_enter())) {
        return;
    }
    if (al_source_get_param(c, source, param, AL_I, d) > 0) {
        *value = (ALint)d[0];
    }
    al_leave(c);
}

void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALdouble d[3];
    ALCcontext *c;
    if (!value1 || !value2 || !value3 || !(c = al_enter())) {
        return;
    }
    if (al_source_get_param(c, source, param, AL_3I, d) > 0) {
        *value1 = (ALint)d[0];
        *value2 = (ALint)d[1];
        *value3 = (ALint)d[2];
    }
    al_leave(c);
}

void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
{
    ALdouble d[3];
    int n, k;
    ALCcontext *c;
    if (!values || !(c = al_enter())) {
        return;
    }
    n = al_source_get_param(c, source, param, AL_IV, d);
    for (k = 0; k < n; k++) {
        values[k] = (ALint)d[k];
    }
    al_leave(c);
}

/* ---- States (4.3.6) --------------------------------------------------------------- */

static void al_source_play(ALsource *s)
{
    ALCdevice *d = s->context->device;
    unsigned dirty = AL_DIRTY_STATE;
    if (s->state == AL_PAUSED) {
        s->state = AL_PLAYING;
    } else {
        /* INITIAL, STOPPED or PLAYING: from the start, or from an offset
         * set while inactive. */
        ALenum type = s->pending_type;
        s->current = 0;
        s->pos = s->frac = 0;
        s->pending_type = 0;
        s->state = AL_PLAYING;
        s->mix_fresh = AL_TRUE;
        if (type) {
            ALdouble frames = al_offset_frames(s, type, s->pending_value);
            if (frames >= 0.0 && frames < al_queue_frames(s, s->queued)) {
                al_source_seek(s, frames);
            }
        }
        dirty |= AL_DIRTY_SEEK;
    }
    /* No buffers, or a device that has gone (ALC_EXT_disconnect): nothing
     * to play, so it ends at once. */
    if (!s->queued || !d->connected) {
        al_source_stop_all(s);
        return;
    }
    al_source_touch(s, dirty);
}

static void al_source_pause(ALsource *s)
{
    if (s->state == AL_PLAYING) {
        s->state = AL_PAUSED;
        al_source_touch(s, AL_DIRTY_STATE);
    }
}

static void al_source_stop(ALsource *s)
{
    if (s->state == AL_PLAYING || s->state == AL_PAUSED) {
        al_source_stop_all(s);
    }
    s->pending_type = 0;
}

static void al_source_rewind(ALsource *s)
{
    if (s->state != AL_INITIAL) {
        s->state = AL_INITIAL;
        s->current = 0;
        s->pos = s->frac = 0;
        al_source_touch(s, AL_DIRTY_STATE | AL_DIRTY_SEEK);
    }
    s->pending_type = 0;
}

/* The vector calls work on all the sources at once, under one lock (4.3.6:
 * "synchronized operation"); a bad name, and none changes. */
static void al_sources_do(ALsizei n, const ALuint *sources, void (*fn)(ALsource *))
{
    ALsizei i;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (n < 0 || (n > 0 && !sources)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    for (i = 0; i < n; i++) {
        if (!al_source_get(c, sources[i])) {
            al_set_error(c, AL_INVALID_NAME);
            al_leave(c);
            return;
        }
    }
    for (i = 0; i < n; i++) {
        fn(al_source_get(c, sources[i]));
    }
    al_leave(c);
}

void AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
{
    al_sources_do(n, sources, al_source_play);
}

void AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    al_sources_do(n, sources, al_source_stop);
}

void AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    al_sources_do(n, sources, al_source_rewind);
}

void AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    al_sources_do(n, sources, al_source_pause);
}

void AL_APIENTRY alSourcePlay(ALuint source)
{
    al_sources_do(1, &source, al_source_play);
}

void AL_APIENTRY alSourceStop(ALuint source)
{
    al_sources_do(1, &source, al_source_stop);
}

void AL_APIENTRY alSourceRewind(ALuint source)
{
    al_sources_do(1, &source, al_source_rewind);
}

void AL_APIENTRY alSourcePause(ALuint source)
{
    al_sources_do(1, &source, al_source_pause);
}

/* ---- The queue (4.3.5) ------------------------------------------------------------ */

void AL_APIENTRY alSourceQueueBuffers(ALuint source, ALsizei nb, const ALuint *buffers)
{
    ALsizei i;
    ALsource *s;
    const ALbuffer *f;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    s = al_source_get(c, source);
    if (!s) {
        al_set_error(c, AL_INVALID_NAME);
        al_leave(c);
        return;
    }
    if (nb < 0 || (nb > 0 && !buffers)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    if (nb == 0) {
        al_leave(c);
        return;
    }
    /* 4.3.2, Source Type: a static source takes no queue. */
    if (s->type == AL_STATIC) {
        al_set_error(c, AL_INVALID_OPERATION);
        al_leave(c);
        return;
    }
    f = al_source_format(s);
    for (i = 0; i < nb; i++) {
        const ALbuffer *b;
        if (!buffers[i]) {
            continue;           /* the NULL buffer can always be queued */
        }
        b = al_buffer_get(c, buffers[i]);
        if (!b) {
            al_set_error(c, AL_INVALID_NAME);
            al_leave(c);
            return;
        }
        /* Every buffer the same format and frequency, or none queued. */
        if (!f && b->format) {
            f = b;
        } else if (f && (b->format != f->format || b->frequency != f->frequency)) {
            al_set_error(c, AL_INVALID_VALUE);
            al_leave(c);
            return;
        }
    }
    if (s->queued + (ALuint)nb > s->room) {
        ALuint room = s->room ? s->room : 4;
        ALbuffer **q;
        while (room < s->queued + (ALuint)nb) {
            room *= 2;
        }
        q = realloc(s->queue, room * sizeof *q);
        if (!q) {
            al_set_error(c, AL_OUT_OF_MEMORY);
            al_leave(c);
            return;
        }
        s->queue = q;
        s->room = room;
    }
    for (i = 0; i < nb; i++) {
        ALbuffer *b = buffers[i] ? al_buffer_get(c, buffers[i]) : NULL;
        if (b) {
            b->refs++;
        }
        s->queue[s->queued++] = b;
    }
    if (s->state == AL_STOPPED) {
        s->current = s->queued;     /* stopped: every entry processed */
    }
    s->type = AL_STREAMING;
    al_source_touch(s, AL_DIRTY_QUEUE | AL_DIRTY_PARAMS);
    al_leave(c);
}

void AL_APIENTRY alSourceUnqueueBuffers(ALuint source, ALsizei nb, ALuint *buffers)
{
    ALsizei i;
    ALsource *s;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    s = al_source_get(c, source);
    if (!s) {
        al_set_error(c, AL_INVALID_NAME);
        al_leave(c);
        return;
    }
    if (nb < 0 || (nb > 0 && !buffers)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    if (nb == 0) {
        al_leave(c);
        return;
    }
    /* 4.3.5: only processed entries, and no more than there are. */
    if ((ALuint)nb > al_source_processed(s)) {
        al_set_error(c, AL_INVALID_VALUE);
        al_leave(c);
        return;
    }
    for (i = 0; i < nb; i++) {
        ALbuffer *b = s->queue[i];
        buffers[i] = b ? b->name : 0;
        if (b) {
            b->refs--;
        }
    }
    memmove(s->queue, s->queue + nb, (s->queued - (ALuint)nb) * sizeof *s->queue);
    s->queued -= (ALuint)nb;
    s->dequeued += (ALuint)nb;
    s->current = s->current > (ALuint)nb ? s->current - (ALuint)nb : 0;
    if (s->state == AL_STOPPED) {
        s->current = s->queued;
    }
    if (!s->queued) {
        s->type = AL_UNDETERMINED;  /* as an empty queue from AL_BUFFER 0 is */
    }
    al_source_touch(s, AL_DIRTY_QUEUE);
    al_leave(c);
}
