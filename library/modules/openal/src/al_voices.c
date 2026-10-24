/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Voices, OpenAL.module's back end for ACAHI's Host mix: the board mixes.
 * Each playing source plays on one of the board's voices, which reads the
 * source's buffers straight from the module's memory (Design-OpenAL,
 * section 3):
 *
 *   OpenAL                      the board (through AHI, al_vout_*)
 *   a buffer                    a segment of the all-of-memory sound for
 *                               its format: where its frames are, how many
 *   a playing source            a voice: its frequency, volume and pan
 *   AL_LOOPING                  the queue planned round and round
 *   one buffer, not looping     silence planned after it: the voice stops
 *   queued buffers              each planned after the one before, without
 *                               AHISF_IMM; the event at each end (AHI's
 *                               SoundFunc) moves the source on
 *   offsets                     the board's position for the voice (AHI's
 *                               AHIET_CHANNELINFO)
 *   pause                       a frequency of 0, which holds the voice
 *
 * The plan. Each voice has a ring of up to AL_VPLAN segments: the one
 * playing (head) and those after it. At each end the interrupt side
 * (al_voices_event, at interrupt time: no lock, no FPU) moves head on and
 * gives the board the segment after the new head, so a stream plays with no
 * gap whatever the 68k is doing. Everything else is the task side's, under
 * al_state_lock: the events taken into each source's timeline at the start
 * of each AL call (al_voices_sync) and by the output's task
 * (al_voices_service), the plan topped up, and a stop when a queue that
 * doesn't loop has played to its end.
 *
 * The 68k's work is for each change, never for each sample: a source's two
 * gains and pitch (al_spatial.c, as for Mix) become the voice's volume, pan
 * and frequency (al_voices_settings: volume = left + right, pan = right /
 * (left + right), which the board turns back into left and right exactly),
 * sent only when they differ from what the board has. Between
 * alcSuspendContext and alcProcessContext only plays, pauses, stops,
 * offsets and queues go to the board; the gains and pitches go in one pass
 * at alcProcessContext.
 *
 * More sources than voices: the loudest play on voices. The others are
 * virtual: their time runs on with the board's clock (al_source_advance),
 * silent, and a virtual source takes a voice at its current offset when one
 * comes free, or from a source playing at under half its gain (a paused
 * source's voice first). */
#include "al_internal.h"

#include <stdlib.h>
#include <string.h>

#define AL_VOICES_WANT 16       /* the board has 32: ahi.device's four units of 4 can share it */
#define AL_VOICES_LEAST 4
#define AL_VPLAN 16             /* segments planned on a voice, the one playing included */
#define AL_VMASK (AL_VPLAN - 1)
#define AL_VSTEAL 2.0f          /* a virtual source takes the voice of one under half its gain */

struct al_vseg {
    ALuint entry;               /* the queue entry, counted from the first ever queued (ALsource.dequeued) */
    ALuint base;                /* the frame of the entry's first frame in its sound */
    ALuint first;               /* the entry's first frame this segment plays */
    ALuint frames;              /* how many it plays */
    unsigned sound;
};

struct al_voice {
    ALsource *source;           /* NULL: free */
    struct al_vseg seg[AL_VPLAN];
    /* Free-running: seg[head & AL_VMASK] plays while head != tail; the
     * interrupt side moves head, the task side tail (under al_vout_lock). */
    volatile ALuint head, tail;
    volatile ALuint ends;       /* events */
    volatile ALuint info_frame; /* the board's position at the last channel info */
    volatile ALuint info_ends;  /* ends then */
    volatile ALuint info_ok;
    ALuint next_entry;          /* the planner's next entry (as seg.entry counts) */
    ALboolean planned_end;      /* the plan reaches the end of a queue that doesn't loop */
    ALboolean sent;             /* vol, pan and freq below are the board's */
    ALint vol, pan;
    ALuint freq;
};

struct al_voices {
    ALuint n;                   /* the voices held */
    volatile ALuint pending;    /* an event or channel info since the task side last looked */
    struct al_voice v[AL_VOICES_MAX];
};

/* ---- The board's settings for a source --------------------------------------------- */

static ALint al_fixed(double x)
{
    if (!(x > 0.0)) {
        return 0;
    }
    if (x >= 4.0) {
        return 0x40000;         /* the board's largest volume */
    }
    return (ALint)(x * 65536.0 + 0.5);
}

void al_voices_settings(const struct al_params *p, ALint channels, ALint *vol, ALint *pan)
{
    double l = p->left, r = p->right, sum = l + r;
    if (channels == 2) {
        /* No 3D for stereo (5.3.4): the gain to both, the pan a balance. */
        *vol = al_fixed(p->gain);
        *pan = 0x8000;
        return;
    }
    /* The board gives the left vol * (1 - pan) and the right vol * pan. */
    if (!(sum > 0.0)) {
        *vol = 0;
        *pan = 0x8000;
        return;
    }
    *vol = al_fixed(sum);
    *pan = (ALint)(r / sum * 65536.0 + 0.5);
    if (*pan > 0x10000) {
        *pan = 0x10000;
    }
}

/* The source's frequency on the board: its buffers' times its pitch. */
static ALuint al_vfreq(const ALsource *s)
{
    const ALbuffer *f = al_source_format(s);
    double p = s->params.pitch, r;
    if (!f) {
        return 0;
    }
    if (!(p > 0.0)) {
        p = 0.0;
    }
    if (p > 255.0) {
        p = 255.0;              /* as Mix's step is clamped */
    }
    r = (double)f->frequency * p + 0.5;
    return r < 1.0 ? 1u : (ALuint)r;
}

/* ---- The interrupt side ---------------------------------------------------------- */

/* The segment after head as the board's next, or silence. */
static void al_vsend_next(ALCdevice *d, ALuint k, struct al_voice *v)
{
    ALuint h = v->head;
    if (h + 1 != v->tail) {
        const struct al_vseg *g = &v->seg[(h + 1) & AL_VMASK];
        al_vout_play(d, k, g->sound, g->base + g->first, g->frames, 0);
    } else {
        al_vout_play(d, k, AL_VOUT_NOSOUND, 0, 0, 0);
    }
}

void al_voices_event(ALCdevice *d, ALuint k)
{
    struct al_voices *vs = d->voices;
    struct al_voice *v;
    if (!vs || k >= vs->n) {
        return;
    }
    v = &vs->v[k];
    v->ends++;
    /* The board has started what it had next (seg[head + 1], or silence);
     * now it is told what comes after that. An end on a voice with nothing
     * planned is one from before it was silenced: nothing moves. */
    if (v->head != v->tail) {
        v->head++;
        if (v->head != v->tail) {
            al_vsend_next(d, k, v);
        }
    }
    vs->pending = 1;
}

void al_voices_info(ALCdevice *d, ALuint k, ALuint frame)
{
    struct al_voices *vs = d->voices;
    struct al_voice *v;
    if (!vs || k >= vs->n) {
        return;
    }
    v = &vs->v[k];
    v->info_frame = frame;
    v->info_ends = v->ends;
    v->info_ok = 1;
    vs->pending = 1;
}

/* ---- The plan (the task side) ------------------------------------------------------- */

static ALboolean al_vhas_frames(const ALsource *s)
{
    ALuint i;
    for (i = 0; i < s->queued; i++) {
        if (s->queue[i] && s->queue[i]->frames) {
            return AL_TRUE;
        }
    }
    return AL_FALSE;
}

static void al_vseg_set(ALCdevice *d, struct al_vseg *g, ALuint entry, const ALbuffer *b, ALuint first)
{
    g->entry = entry;
    g->sound = al_vout_sound(d, b->bits, b->channels);
    g->base = al_vout_frame(d, b->data, b->frames, b->bits, b->channels);
    g->first = first;
    g->frames = b->frames - first;
}

/* Plan the entries after the last planned, while there is room: round the
 * queue when it loops. send: the voice plays, so a segment planned right
 * after the one playing (where silence was next) goes to the board. Under
 * al_vout_lock. */
static void al_vplan_more(ALCdevice *d, ALuint k, struct al_voice *v, int send)
{
    ALsource *s = v->source;
    ALuint wraps = 0;           /* round the queue with nothing planned */
    while (v->tail - v->head < AL_VPLAN && !v->planned_end) {
        ALuint idx = v->next_entry - s->dequeued;
        const ALbuffer *b;
        if (idx >= s->queued) {
            if (s->looping && al_vhas_frames(s) && wraps++ < 2) {
                v->next_entry = s->dequeued;
                continue;
            }
            v->planned_end = AL_TRUE;
            break;
        }
        b = s->queue[idx];
        v->next_entry++;
        if (!b || !b->frames) {
            continue;           /* the NULL buffer, or one with no data: nothing to play */
        }
        al_vseg_set(d, &v->seg[v->tail & AL_VMASK], v->next_entry - 1, b, 0);
        v->tail++;
        wraps = 0;
        if (send && v->tail - v->head == 2) {
            al_vsend_next(d, k, v);
        }
    }
}

/* What the board has for the source: its frequency (0 while paused),
 * volume and pan, each sent only when it changed. */
static void al_vsettings(ALCdevice *d, ALuint k, struct al_voice *v)
{
    const ALsource *s = v->source;
    ALint vol, pan;
    ALuint freq = s->state == AL_PLAYING ? al_vfreq(s) : 0;
    al_voices_settings(&s->params, al_source_channels(s), &vol, &pan);
    if (!v->sent || freq != v->freq) {
        al_vout_freq(d, k, freq);
        v->freq = freq;
    }
    if (!v->sent || vol != v->vol || pan != v->pan) {
        al_vout_vol(d, k, vol, pan);
        v->vol = vol;
        v->pan = pan;
    }
    v->sent = AL_TRUE;
}

/* The voice goes silent and free. */
static void al_vfree(ALCdevice *d, ALuint k)
{
    struct al_voices *vs = d->voices;
    struct al_voice *v = &vs->v[k];
    al_vout_lock(d);
    al_vout_play(d, k, AL_VOUT_NOSOUND, 0, 0, 1);
    v->tail = v->head;
    al_vout_unlock(d);
    if (v->source) {
        v->source->voice = -1;
        v->source = NULL;
    }
}

/* Source s plays on voice k from its timeline (current entry, pos), at
 * once: what the voice played before stops. */
static void al_vstart(ALCdevice *d, ALuint k, ALsource *s)
{
    struct al_voices *vs = d->voices;
    struct al_voice *v = &vs->v[k];
    const ALbuffer *b = NULL;
    ALuint idx, first = 0, n;
    if (v->source && v->source != s) {
        v->source->voice = -1;
    }
    v->source = s;
    s->voice = (ALint)k;
    /* The first entry with frames from where the source is. */
    for (idx = s->current, n = 0; n <= s->queued; n++, idx++) {
        if (idx >= s->queued) {
            if (!s->looping) {
                break;
            }
            idx = 0;
        }
        b = s->queue[idx];
        first = (idx == s->current && n == 0) ? s->pos : 0;
        if (b && first < b->frames) {
            break;
        }
        b = NULL;
    }
    if (!b) {
        al_vfree(d, k);
        al_source_stop_all(s);
        return;
    }
    if (idx != s->current) {
        s->current = idx;
        s->pos = s->frac = 0;
    }
    al_vout_lock(d);
    v->tail = v->head;
    v->info_ok = 0;             /* the last channel info was what played before */
    al_vseg_set(d, &v->seg[v->tail & AL_VMASK], s->dequeued + idx, b, first);
    v->tail++;
    v->next_entry = s->dequeued + idx + 1;
    v->planned_end = AL_FALSE;
    al_vplan_more(d, k, v, 0);
    v->sent = AL_FALSE;
    al_vsettings(d, k, v);
    al_vout_play(d, k, v->seg[v->head & AL_VMASK].sound, v->seg[v->head & AL_VMASK].base + first,
                 v->seg[v->head & AL_VMASK].frames, 1);
    al_vsend_next(d, k, v);
    al_vout_unlock(d);
}

/* The source's looping or queue changed: everything after the segment
 * playing is planned again, and the board told what comes next. */
static void al_vreplan(ALCdevice *d, ALuint k)
{
    struct al_voice *v = &((struct al_voices *)d->voices)->v[k];
    al_vout_lock(d);
    if (v->head != v->tail) {
        v->tail = v->head + 1;
        v->next_entry = v->seg[v->head & AL_VMASK].entry + 1;
        v->planned_end = AL_FALSE;
        al_vplan_more(d, k, v, 0);
        al_vsend_next(d, k, v);
    }
    al_vout_unlock(d);
}

/* Buffers queued on a source that doesn't loop: the plan goes on past what
 * was its end. A voice that ran out just now starts again with them. */
static void al_vextend(ALCdevice *d, ALuint k)
{
    struct al_voice *v = &((struct al_voices *)d->voices)->v[k];
    ALsource *s = v->source;
    if (!v->planned_end) {
        return;
    }
    al_vout_lock(d);
    v->planned_end = AL_FALSE;
    if (v->head != v->tail) {
        al_vplan_more(d, k, v, 1);
        al_vout_unlock(d);
        return;
    }
    al_vout_unlock(d);
    s->current = v->next_entry - s->dequeued;
    s->pos = s->frac = 0;
    al_vstart(d, k, s);
}

/* Where a voice's source is: the entry of the segment playing, and the
 * frame from the last channel info when no end has come since. */
static void al_vposition(struct al_voice *v)
{
    ALsource *s = v->source;
    const struct al_vseg *g = &v->seg[v->head & AL_VMASK];
    ALuint pos = g->first;
    if (v->info_ok && v->info_ends == v->ends) {
        ALuint f = v->info_frame;
        if (f >= g->base + g->first && f - g->base < g->first + g->frames) {
            pos = f - g->base;
        }
    }
    s->current = g->entry - s->dequeued;
    s->pos = pos;
    s->frac = 0;
}

/* ---- Taking in what played ----------------------------------------------------------- */

static ALuint al_vclock(ALCdevice *d)
{
    return al_vout_clock(d);
}

/* Each voice's events into its source's timeline: the entry playing and
 * where in it; a queue that doesn't loop played to its end stops the
 * source; a plan that ran dry before the queue did starts again where it
 * is. */
static void al_vtake(ALCdevice *d)
{
    struct al_voices *vs = d->voices;
    ALuint k;
    for (k = 0; k < vs->n; k++) {
        struct al_voice *v = &vs->v[k];
        ALsource *s = v->source;
        if (!s) {
            continue;
        }
        if (v->head == v->tail) {
            if (v->planned_end) {
                v->source = NULL;
                s->voice = -1;
                al_source_stop_all(s);
            } else {
                s->current = v->next_entry - s->dequeued;
                s->pos = s->frac = 0;
                al_vstart(d, k, s);
            }
            continue;
        }
        al_vposition(v);
    }
}

/* Every virtual source's time runs on to the board's clock. The number
 * still playing virtually. */
static ALuint al_vadvance(ALCdevice *d)
{
    ALuint now = al_vclock(d), n = 0;
    ALCcontext *c;
    ALsource *s;
    for (c = d->contexts; c; c = c->next) {
        for (s = c->sources; s; s = s->next) {
            if (s->voice < 0 && s->state == AL_PLAYING) {
                ALuint delta = now - s->vclock;
                if (delta) {
                    al_source_advance(s, delta, d->frequency);
                    s->dirty &= ~(unsigned)(AL_DIRTY_STATE | AL_DIRTY_SEEK);
                }
                s->vclock = now;
                n += s->state == AL_PLAYING;
            }
        }
    }
    return n;
}

/* Each playing voice's plan topped up. */
static void al_vrefill(ALCdevice *d)
{
    struct al_voices *vs = d->voices;
    ALuint k;
    for (k = 0; k < vs->n; k++) {
        struct al_voice *v = &vs->v[k];
        if (v->source && !v->planned_end && v->tail - v->head < AL_VPLAN) {
            al_vout_lock(d);
            if (v->head != v->tail) {
                al_vplan_more(d, k, v, 1);
            }
            al_vout_unlock(d);
        }
    }
}

/* Voices for virtual sources, the loudest first: a free voice, or one
 * whose source is paused, or plays at under 1/AL_VSTEAL of its gain (which
 * goes on virtually from where it is). */
static void al_vassign(ALCdevice *d)
{
    struct al_voices *vs = d->voices;
    ALuint guard;
    for (guard = 0; guard < 2 * AL_VOICES_MAX + 2; guard++) {
        ALsource *best = NULL, *s;
        ALCcontext *c;
        ALuint k, free_k = vs->n, low_k = vs->n;
        ALfloat low = 0.0f;
        for (c = d->contexts; c; c = c->next) {
            for (s = c->sources; s; s = s->next) {
                if (s->voice < 0 && s->state == AL_PLAYING && (!best || s->params.gain > best->params.gain)) {
                    best = s;
                }
            }
        }
        if (!best) {
            return;
        }
        for (k = 0; k < vs->n; k++) {
            const ALsource *o = vs->v[k].source;
            ALfloat g;
            if (!o) {
                free_k = k;
                break;
            }
            g = o->state == AL_PLAYING ? o->params.gain : -1.0f;
            if (low_k == vs->n || g < low) {
                low = g;
                low_k = k;
            }
        }
        if (free_k < vs->n) {
            al_vstart(d, free_k, best);
            continue;
        }
        if (low_k < vs->n && (low < 0.0f || best->params.gain > AL_VSTEAL * low)) {
            ALsource *o = vs->v[low_k].source;
            if (o->state == AL_PLAYING) {
                al_vposition(&vs->v[low_k]);
            }
            o->voice = -1;
            o->vclock = al_vclock(d);
            vs->v[low_k].source = NULL;
            al_vstart(d, low_k, best);
            continue;
        }
        return;
    }
}

/* Everything the output did since the last look. Under al_state_lock. */
static void al_vlook(ALCdevice *d)
{
    struct al_voices *vs = d->voices;
    vs->pending = 0;
    al_vtake(d);
    if (al_vadvance(d)) {
        al_vassign(d);
    }
    al_vrefill(d);
}

/* ---- The back end ------------------------------------------------------------------ */

static ALCboolean al_voices_open(ALCdevice *d)
{
    struct al_voices *vs = calloc(1, sizeof *vs);
    ALuint n;
    if (!vs) {
        return ALC_FALSE;
    }
    d->voices = vs;
    for (n = AL_VOICES_WANT; n >= AL_VOICES_LEAST; n /= 2) {
        vs->n = n;
        if (al_vout_open(d, n)) {
            return ALC_TRUE;
        }
    }
    d->voices = NULL;
    free(vs);
    return ALC_FALSE;
}

static void al_voices_close(ALCdevice *d)
{
    al_vout_close(d);
    free(d->voices);
    d->voices = NULL;
}

static ALCboolean al_voices_start(ALCdevice *d)
{
    return al_vout_start(d);
}

static void al_voices_stop(ALCdevice *d)
{
    struct al_voices *vs = d->voices;
    ALuint k;
    AL_LOCK();
    for (k = 0; k < vs->n; k++) {
        al_vfree(d, k);
    }
    AL_UNLOCK();
    al_vout_stop(d);
}

/* After an AL call that changed something: plays, pauses, stops, offsets
 * and queues at once; gains and pitches unless the context is suspended
 * (alcProcessContext sends them all, as al_context_update would work
 * them out). */
static void al_voices_update(ALCcontext *c)
{
    ALCdevice *d = c->device;
    struct al_voices *vs = d->voices;
    int all = !c->suspended && c->dirty;
    ALuint virt = 0;
    ALsource *s;
    if (!vs) {
        return;
    }
    for (s = c->sources; s; s = s->next) {
        unsigned dirty = s->dirty;
        int params = !c->suspended && (all || (dirty & AL_DIRTY_PARAMS));
        if (params) {
            al_source_params(c, s, al_source_channels(s), &s->params);
        }
        s->dirty = c->suspended ? (dirty & AL_DIRTY_PARAMS) : 0;
        dirty &= ~(unsigned)AL_DIRTY_PARAMS;
        if (s->voice >= 0) {
            ALuint k = (ALuint)s->voice;
            if (s->state != AL_PLAYING && s->state != AL_PAUSED) {
                al_vfree(d, k);
            } else if (dirty & AL_DIRTY_SEEK) {
                al_vstart(d, k, s);
            } else {
                if ((dirty & AL_DIRTY_LOOP) || ((dirty & AL_DIRTY_QUEUE) && s->looping)) {
                    al_vreplan(d, k);
                } else if (dirty & AL_DIRTY_QUEUE) {
                    al_vextend(d, k);
                }
                if (s->voice >= 0 && (dirty || params)) {
                    al_vsettings(d, k, &vs->v[k]);
                }
            }
        } else if (s->state == AL_PLAYING) {
            if (dirty & (AL_DIRTY_STATE | AL_DIRTY_SEEK)) {
                s->vclock = al_vclock(d);   /* its time starts now, voice or not */
            }
            virt++;
        }
    }
    if (!c->suspended) {
        c->dirty = AL_FALSE;
    }
    if (virt) {
        al_vassign(d);
    }
}

static void al_voices_sync(ALCcontext *c)
{
    struct al_voices *vs = c->device->voices;
    if (vs && vs->pending) {
        al_vlook(c->device);
    }
}

static void al_voices_forget(ALsource *s)
{
    if (s->voice >= 0) {
        al_vfree(s->context->device, (ALuint)s->voice);
    }
}

void al_voices_service(ALCdevice *d)
{
    AL_LOCK();
    if (d->voices) {
        al_vlook(d);
    }
    AL_UNLOCK();
}

const struct al_backend al_backend_voices = {
    "AHI Voices",
    al_vout_available,
    al_voices_open,
    al_voices_close,
    al_voices_start,
    al_voices_stop,
    al_voices_update,
    al_voices_sync,
    al_voices_forget,
    AL_BACKEND_FIXED_RATE,
};
