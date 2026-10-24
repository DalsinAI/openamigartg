/*
 * ACAHI, AmigaChrome's sound board for AHI (protocol acahi-v1,
 * vendor/amigachrome-guest/common/protocol/acahi.h; reference
 * docs/autodocs/acahi.doc). We, 6 Oct 2026: "go on ACAHI sound
 * host", with AHI's channels mixed on the host from the start.
 *
 * Two sources, summed into the instance's second ring at 44.1 kHz:
 *
 * - The stream: what AHI has mixed itself (the 68k's mixer, the modes
 *   without "Host mix"). Each buffer is copied in when it is submitted,
 *   converted to 16-bit stereo, and played at its own rate, linearly
 *   resampled. A buffer is DONE when its last frame has played: AHI's
 *   driver mixes the next one then, so the music keeps the audio clock's
 *   time, not the CPU's (the review's "one clock": in hyper mode the CPU
 *   runs far ahead of real time).
 *
 * - The voices: AHI's channels, mixed here (the "Host mix" modes). AHI
 *   loads its sounds (LoadSound: where, how long, what type) and the driver
 *   passes each SetSound, SetFreq and SetVol on. The voices read the
 *   sounds from guest RAM as they play, so AHI's dynamic sounds (a
 *   player's double buffers, ahi.device's requests) are heard as the guest
 *   writes them. AHI's rules: a sound plays again until another is set;
 *   one set without AHISF_IMM waits for the playing one to end, as do the
 *   frequency and volume set without it; the end of every sound is an
 *   event (AHI's SoundFunc). The timer gives AHI's PlayerFunc its ticks on
 *   the same clock.
 *
 * Nothing here knows the machine: guest RAM comes through a callback, and
 * the machine calls acahi_render as its audio clock moves (a1200.c,
 * ahi_sync).
 */
#include "acahi.h"
#include <stdlib.h>
#include <string.h>

#define ONE ((uint64_t)1 << 32)

static int stream_type_ok(uint32_t t) { return t == 1 || t == 3 || t == 8 || t == 10; }
static int sound_type_ok(uint32_t t) { return t == 0 || t == 1 || t == 2 || t == 3 || t == 8 || t == 10; }
/* Bytes a frame of AHI's sample type t. */
static uint32_t frame_bytes(uint32_t t) {
    switch (t) {
    case 0: return 1;                  /* M8S */
    case 1: case 2: return 2;          /* M16S, S8S */
    case 3: case 8: return 4;          /* S16S, M32S */
    case 10: return 8;                 /* S32S */
    }
    return 0;
}
static inline int16_t be16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static inline int16_t be32hi(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }   /* a 32-bit sample's top 16 bits */
/* One frame of type t at p, as 16-bit left and right. */
static inline void frame_at(uint32_t t, const uint8_t *p, int32_t *l, int32_t *r) {
    switch (t) {
    case 0: *l = *r = (int8_t)p[0] * 256; break;
    case 1: *l = *r = be16(p); break;
    case 2: *l = (int8_t)p[0] * 256; *r = (int8_t)p[1] * 256; break;
    case 3: *l = be16(p); *r = be16(p + 2); break;
    case 8: *l = *r = be32hi(p); break;
    case 10: *l = be32hi(p); *r = be32hi(p + 4); break;
    default: *l = *r = 0;
    }
}

int acahi_init(acahi *a, acahi_ram_fn ram, void *opaque) {
    memset(a, 0, sizeof *a);
    a->fifo = calloc((size_t)ACAHI_FIFO * 2, sizeof(int16_t));
    if (!a->fifo) return -1;
    a->ram = ram;
    a->ram_opaque = opaque;
    a->levels = a->levels_own;
    a->levels_own[0] = ACAHI_LEVELS_DEFAULT | ACAHI_LEVELS_SET;
    acahi_reset(a);
    return 0;
}

void acahi_share_levels(acahi *a, uint8_t *ring_header) {
    volatile uint32_t *w = (volatile uint32_t *)(ring_header + 24);
    if (!(w[0] & ACAHI_LEVELS_SET)) w[0] = a->levels[0];
    a->levels = w;
}

static uint32_t clamp_pct(uint32_t p) { return p > 100 ? 100 : p; }

static void levels_write(acahi *a, uint32_t v) {
    uint32_t w = clamp_pct(ACAHI_LEVEL_VOLUME(v)) | clamp_pct(ACAHI_LEVEL_PAULA(v)) << 8 | clamp_pct(ACAHI_LEVEL_AHI(v)) << 16
                 | (v & ACAHI_LEVEL_MUTE);
    a->levels[0] = w | ACAHI_LEVELS_SET;
    a->levels[1] = a->levels[1] + 1;
}

void acahi_free(acahi *a) {
    free(a->fifo);
    a->fifo = NULL;
}

void acahi_reset(acahi *a) {
    a->control = 0;
    a->rate = ACAHI_OUT_RATE;
    a->format = 3;                     /* S16S */
    a->pb_addr = a->pb_frames = a->pb_seq = a->pb_result = a->pb_done = 0;
    a->intreq = a->intena = 0;
    a->underruns = a->errors = 0;
    a->frame = 0;
    a->fifo_w = a->fifo_r = a->frac = 0;
    a->q_n = 0;
    a->dry = 1;
    a->timer_freq = a->timer_ticks = 0;
    a->timer_acc = 0;
    a->master = 0x10000;
    a->voice_events = 0;
    a->snd_id = a->snd_type = a->snd_addr = a->snd_len = a->snd_result = 0;
    memset(a->sounds, 0, sizeof a->sounds);
    memset(a->v, 0, sizeof a->v);
    for (uint32_t i = 0; i < ACAHI_VOICES; i++) {
        a->v[i].s_sound = ACAHI_NOSOUND;
        a->v[i].s_vol = 0x10000;
        a->v[i].s_pan = 0x8000;
        a->v[i].next.vol = a->v[i].cur.vol = 0x10000;
        a->v[i].next.pan = a->v[i].cur.pan = 0x8000;
    }
}

int acahi_busy(const acahi *a) {
    return (a->control & (ACAHI_CTL_STREAM | ACAHI_CTL_VOICES)) || a->timer_freq;
}

/* ---- the stream ------------------------------------------------------------------ */

static uint32_t stream_queued(const acahi *a) { return (uint32_t)(a->fifo_w - a->fifo_r); }

static void stream_submit(acahi *a) {
    uint32_t t = a->format, fb = frame_bytes(t), n = a->pb_frames;
    if (!stream_type_ok(t) || a->rate < ACAHI_MIN_RATE || a->rate > ACAHI_MAX_RATE) { a->pb_result = ACAHI_PB_BADFMT; a->errors++; return; }
    if (n == 0 || n > ACAHI_MAX_FRAMES) { a->pb_result = ACAHI_PB_BADLEN; a->errors++; return; }
    if (a->q_n >= ACAHI_QUEUE || stream_queued(a) + n > ACAHI_FIFO) { a->pb_result = ACAHI_PB_FULL; a->errors++; return; }
    const uint8_t *p = a->ram ? a->ram(a->ram_opaque, a->pb_addr, n * fb) : NULL;
    if (!p) { a->pb_result = ACAHI_PB_BADADDR; a->errors++; return; }
    for (uint32_t i = 0; i < n; i++, p += fb) {
        int32_t l, r;
        frame_at(t, p, &l, &r);
        int16_t *d = &a->fifo[((a->fifo_w + i) % ACAHI_FIFO) * 2];
        d[0] = (int16_t)l; d[1] = (int16_t)r;
    }
    a->fifo_w += n;
    a->q[a->q_n].seq = a->pb_seq;
    a->q[a->q_n].end = a->fifo_w;
    a->q_n++;
    a->pb_result = ACAHI_PB_OK;
    a->dry = 0;
}

/* Buffers whose last frame has played are DONE. */
static void stream_retire(acahi *a) {
    uint32_t k = 0;
    while (k < a->q_n && a->fifo_r >= a->q[k].end) {
        a->pb_done = a->q[k].seq;
        k++;
    }
    if (!k) return;
    memmove(a->q, a->q + k, (a->q_n - k) * sizeof a->q[0]);
    a->q_n -= k;
    a->intreq |= ACAHI_INT_DONE;
}

static void stream_render(acahi *a, int32_t *mix, uint32_t n) {
    uint64_t step = ((uint64_t)a->rate << 32) / ACAHI_OUT_RATE;
    for (uint32_t i = 0; i < n; i++) {
        if (a->fifo_r >= a->fifo_w) {
            if (!a->dry) { a->dry = 1; a->underruns++; a->intreq |= ACAHI_INT_UNDERRUN; }
            a->frac = 0;
            continue;
        }
        const int16_t *s0 = &a->fifo[(a->fifo_r % ACAHI_FIFO) * 2];
        const int16_t *s1 = a->fifo_r + 1 < a->fifo_w ? &a->fifo[((a->fifo_r + 1) % ACAHI_FIFO) * 2] : s0;
        int32_t f = (int32_t)(a->frac >> 17);                 /* 0..32767: the product stays in 32 bits */
        mix[i * 2] += s0[0] + (((s1[0] - s0[0]) * f) >> 15);
        mix[i * 2 + 1] += s0[1] + (((s1[1] - s0[1]) * f) >> 15);
        a->frac += step;
        a->fifo_r += a->frac >> 32;
        a->frac &= ONE - 1;
        if (a->fifo_r > a->fifo_w) a->fifo_r = a->fifo_w;
        if (a->q_n && a->fifo_r >= a->q[0].end) stream_retire(a);
    }
}

/* ---- the voices ------------------------------------------------------------------ */

static uint64_t voice_step(uint32_t freq) {
    if (freq == 0xffffffffu) return ONE;
    return ((uint64_t)freq << 32) / ACAHI_OUT_RATE;
}

/* cur starts: find its frames in host memory. 0 if they are not all RAM. */
static int voice_bind(acahi *a, acahi_voice *v) {
    const acahi_sound *s = &a->sounds[v->cur.snd];
    uint32_t fb = frame_bytes(s->type);
    uint32_t lo = v->cur.dir > 0 ? v->cur.start : v->cur.start - (v->cur.frames - 1);
    uint64_t at = (uint64_t)s->addr + (uint64_t)lo * fb, len = (uint64_t)v->cur.frames * fb;
    const uint8_t *p = a->ram && at + len <= 0x100000000ull ? a->ram(a->ram_opaque, (uint32_t)at, (uint32_t)len) : NULL;
    if (!p) { v->data = NULL; return 0; }
    v->data = p + (size_t)(v->cur.start - lo) * fb;
    return 1;
}

static void voice_start(acahi *a, acahi_voice *v, const acahi_seg *s, uint64_t pos) {
    v->cur = *s;
    v->pos = pos;
    v->step = voice_step(s->freq);
    v->on = s->snd < ACAHI_SOUNDS && a->sounds[s->snd].frames && voice_bind(a, v);
    if (!v->on && s->snd < ACAHI_SOUNDS) a->errors++;
}

/* The staged sound as a segment: 0 and *silent when it is AHI_NOSOUND,
 * ACAHI_RES_* otherwise. */
static uint32_t staged_segment(const acahi *a, const acahi_voice *v, acahi_seg *s, int *silent) {
    *silent = v->s_sound == ACAHI_NOSOUND;
    if (*silent) return ACAHI_RES_OK;
    if (v->s_sound >= ACAHI_SOUNDS || !a->sounds[v->s_sound].frames) return ACAHI_RES_BADSLOT;
    uint32_t len = a->sounds[v->s_sound].frames, off = v->s_offset;
    if (off >= len) return ACAHI_RES_BADRANGE;
    s->snd = v->s_sound;
    s->start = off;
    if (v->s_length == 0) { s->frames = len - off; s->dir = 1; }
    else if (v->s_length > 0) {
        if ((uint64_t)off + (uint32_t)v->s_length > len) return ACAHI_RES_BADRANGE;
        s->frames = (uint32_t)v->s_length; s->dir = 1;
    } else {
        uint32_t k = (uint32_t)-(int64_t)v->s_length;
        if (k > off + 1) return ACAHI_RES_BADRANGE;
        s->frames = k; s->dir = -1;
    }
    return ACAHI_RES_OK;
}

static int32_t clamp_vol(int32_t x) { return x > 0x40000 ? 0x40000 : x < -0x40000 ? -0x40000 : x; }
static int32_t clamp_pan(int32_t x) {
    if (x < 0) x = x == INT32_MIN ? INT32_MAX : -x;   /* AHI's surround: a board without it plays the absolute pan */
    return x > 0x10000 ? 0x10000 : x;
}

static void voice_cmd(acahi *a, uint32_t n, uint32_t cmd) {
    acahi_voice *v = &a->v[n];
    int imm = (cmd & ACAHI_VC_IMM) || !v->on;    /* a silent voice takes everything at once */
    acahi_seg s = v->next;
    int silent = 0;
    if (cmd & ACAHI_VC_SOUND) {                  /* checked first: a refused command changes nothing */
        uint32_t r = staged_segment(a, v, &s, &silent);
        if (r != ACAHI_RES_OK) { a->snd_result = r; a->errors++; return; }
    }
    if (cmd & ACAHI_VC_FREQ) {
        v->next.freq = v->s_freq;
        if (imm) { v->cur.freq = v->s_freq; v->step = voice_step(v->s_freq); }
    }
    if (cmd & ACAHI_VC_VOL) {
        v->next.vol = clamp_vol(v->s_vol); v->next.pan = clamp_pan(v->s_pan);
        if (imm) { v->cur.vol = v->next.vol; v->cur.pan = v->next.pan; }
    }
    if (cmd & ACAHI_VC_SOUND) {
        if (silent) {
            v->next_on = 0;
            if (cmd & ACAHI_VC_IMM) v->on = 0;
        } else {
            v->next.snd = s.snd; v->next.start = s.start; v->next.frames = s.frames; v->next.dir = s.dir;
            v->next_on = 1;
            if (imm) voice_start(a, v, &v->next, 0);   /* with the frequency and volume set last */
        }
    }
    a->snd_result = ACAHI_RES_OK;
}

/* The voice's current segment ended: the next one starts (or silence), and
 * AHI hears of it. */
static void voice_end(acahi *a, uint32_t n, uint64_t carry) {
    acahi_voice *v = &a->v[n];
    a->voice_events |= 1u << n;
    a->intreq |= ACAHI_INT_VOICE;
    if (!v->next_on) { v->on = 0; return; }
    voice_start(a, v, &v->next, carry);
}

static void voices_render(acahi *a, int32_t *mix, uint32_t n) {
    int32_t master = (int32_t)a->master;
    for (uint32_t k = 0; k < ACAHI_VOICES; k++) {
        acahi_voice *v = &a->v[k];
        if (!v->step) continue;                                     /* frequency 0: AHI's pause, silent, keeping its place */
        for (uint32_t i = 0; i < n && v->on; i++) {
            const acahi_sound *s = &a->sounds[v->cur.snd];
            uint32_t fb = frame_bytes(s->type);
            uint32_t at = (uint32_t)(v->pos >> 32);
            const uint8_t *p0 = v->data + (int64_t)v->cur.dir * at * fb;
            const uint8_t *p1 = at + 1 < v->cur.frames ? p0 + v->cur.dir * (int32_t)fb : p0;
            int32_t l0, r0, l1, r1;
            frame_at(s->type, p0, &l0, &r0);
            frame_at(s->type, p1, &l1, &r1);
            int32_t f = (int32_t)((v->pos >> 17) & 0x7fff);
            int32_t l = l0 + (((l1 - l0) * f) >> 15), r = r0 + (((r1 - r0) * f) >> 15);
            int32_t gl, gr, pan = v->cur.pan;
            if (s->type == 2 || s->type == 3 || s->type == 10) {   /* stereo: the pan is a balance */
                gl = (int32_t)(((int64_t)v->cur.vol * (pan <= 0x8000 ? 0x10000 : 2 * (0x10000 - pan))) >> 16);
                gr = (int32_t)(((int64_t)v->cur.vol * (pan >= 0x8000 ? 0x10000 : 2 * pan)) >> 16);
            } else {
                gl = (int32_t)(((int64_t)v->cur.vol * (0x10000 - pan)) >> 16);
                gr = (int32_t)(((int64_t)v->cur.vol * pan) >> 16);
            }
            mix[i * 2] += (int32_t)(((int64_t)l * gl >> 16) * master >> 16);
            mix[i * 2 + 1] += (int32_t)(((int64_t)r * gr >> 16) * master >> 16);
            v->pos += v->step;
            while (v->on && (v->pos >> 32) >= v->cur.frames)        /* its last frame has played: the next starts, AHI hears now */
                voice_end(a, k, v->pos - ((uint64_t)v->cur.frames << 32));
        }
    }
}

/* ---- the timer --------------------------------------------------------------------- */

static uint64_t timer_period(uint32_t tf) {
    uint64_t p = ((uint64_t)ACAHI_OUT_RATE << 48) / tf;   /* 32.32 output frames a tick */
    return p < ((uint64_t)11 << 32) ? (uint64_t)11 << 32 : p;   /* at most 4 kHz */
}

static void timer_render(acahi *a, uint32_t n) {
    uint64_t p = timer_period(a->timer_freq);
    a->timer_acc += (uint64_t)n << 32;
    while (a->timer_acc >= p) {
        a->timer_acc -= p;
        a->timer_ticks++;
        a->intreq |= ACAHI_INT_TIMER;
    }
}

void acahi_render(acahi *a, int32_t *mix, uint32_t n) {
    if (a->control & ACAHI_CTL_STREAM) stream_render(a, mix, n);
    if (a->control & ACAHI_CTL_VOICES) voices_render(a, mix, n);
    if (a->timer_freq) timer_render(a, n);
    a->frame += n;
}

/* ---- registers ------------------------------------------------------------------- */

static uint32_t reg(acahi *a, uint32_t off) {
    if (off >= ACAHI_REG_VOICE0 && off < ACAHI_REG_VOICE0 + ACAHI_VOICES * ACAHI_VOICE_STRIDE) {
        acahi_voice *v = &a->v[(off - ACAHI_REG_VOICE0) / ACAHI_VOICE_STRIDE];
        switch ((off - ACAHI_REG_VOICE0) % ACAHI_VOICE_STRIDE) {
        case ACAHI_V_SOUND: return v->s_sound;
        case ACAHI_V_OFFSET: return v->s_offset;
        case ACAHI_V_LENGTH: return (uint32_t)v->s_length;
        case ACAHI_V_FREQ: return v->s_freq;
        case ACAHI_V_VOL: return (uint32_t)v->s_vol;
        case ACAHI_V_PAN: return (uint32_t)v->s_pan;
        case ACAHI_V_CMD: return v->on ? 1 : 0;
        case ACAHI_V_POS: {
            if (!v->on) return 0;
            uint32_t at = (uint32_t)(v->pos >> 32);
            return v->cur.dir > 0 ? v->cur.start + at : v->cur.start - at;
        }
        }
        return 0;
    }
    switch (off) {
    case ACAHI_REG_ID: return ACAHI_SIGNATURE;
    case ACAHI_REG_VERSION: return ACAHI_VERSION;
    case ACAHI_REG_CAPS: return ACAHI_CAP_PLAYBACK | ACAHI_CAP_S16 | ACAHI_CAP_STEREO | ACAHI_CAP_IRQ | ACAHI_CAP_QUEUE
                                | ACAHI_CAP_LATENCY | ACAHI_CAP_S32 | ACAHI_CAP_VOICES | ACAHI_CAP_TIMER | ACAHI_CAP_LEVELS;
    case ACAHI_REG_STATUS: return (a->host ? ACAHI_ST_HOST : 0) | (a->control & ACAHI_CTL_STREAM ? ACAHI_ST_STREAM : 0)
                                  | (a->control & ACAHI_CTL_VOICES ? ACAHI_ST_VOICES : 0);
    case ACAHI_REG_CONTROL: return a->control;
    case ACAHI_REG_RATE: return a->rate;
    case ACAHI_REG_FORMAT: return a->format;
    case ACAHI_REG_OUTRATE: return ACAHI_OUT_RATE;
    case ACAHI_REG_PB_ADDR: return a->pb_addr;
    case ACAHI_REG_PB_FRAMES: return a->pb_frames;
    case ACAHI_REG_PB_SEQ: return a->pb_seq;
    case ACAHI_REG_PB_SUBMIT: return a->pb_result;
    case ACAHI_REG_PB_DONE: return a->pb_done;
    case ACAHI_REG_PB_QUEUED: return stream_queued(a);
    case ACAHI_REG_PB_FREE: return ACAHI_QUEUE - a->q_n;
    case ACAHI_REG_LATENCY: return (uint32_t)((uint64_t)stream_queued(a) * ACAHI_OUT_RATE / (a->rate ? a->rate : ACAHI_OUT_RATE));
    case ACAHI_REG_INTREQ: return a->intreq;
    case ACAHI_REG_INTENA: return a->intena;
    case ACAHI_REG_UNDERRUNS: return a->underruns;
    case ACAHI_REG_ERRORS: return a->errors;
    case ACAHI_REG_FRAME: return (uint32_t)a->frame;
    case ACAHI_REG_TIMER_FREQ: return a->timer_freq;
    case ACAHI_REG_TIMER_TICKS: return a->timer_ticks;
    case ACAHI_REG_VOICES: return ACAHI_VOICES;
    case ACAHI_REG_VOICE_EVENTS: return a->voice_events;
    case ACAHI_REG_MASTER: return a->master;
    case ACAHI_REG_SND_ID: return a->snd_id;
    case ACAHI_REG_SND_TYPE: return a->snd_type;
    case ACAHI_REG_SND_ADDR: return a->snd_addr;
    case ACAHI_REG_SND_LEN: return a->snd_len;
    case ACAHI_REG_SND_CMD: return a->snd_result;
    case ACAHI_REG_LEVELS: return a->levels[0] & ~ACAHI_LEVELS_SET;
    case ACAHI_REG_LEVEL_SEQ: return a->levels[1];
    }
    return 0;
}

uint32_t acahi_read(acahi *a, uint32_t off, int size) {
    if (size == 4 && !(off & 3)) return reg(a, off);
    uint32_t r = 0;
    for (int i = 0; i < size; i++) { uint32_t at = off + (uint32_t)i; r = r << 8 | (reg(a, at & ~3u) >> ((3 - (at & 3)) * 8) & 0xff); }
    return r;
}

static void sound_cmd(acahi *a, uint32_t cmd) {
    uint32_t id = a->snd_id;
    if (id >= ACAHI_SOUNDS) { a->snd_result = ACAHI_RES_BADSLOT; a->errors++; return; }
    if (cmd == ACAHI_SND_UNLOAD) {
        a->sounds[id].frames = 0;
        for (uint32_t k = 0; k < ACAHI_VOICES; k++) {                   /* nothing plays a sound that has gone */
            if (a->v[k].on && a->v[k].cur.snd == id) a->v[k].on = 0;
            if (a->v[k].next_on && a->v[k].next.snd == id) a->v[k].next_on = 0;
        }
        a->snd_result = ACAHI_RES_OK;
        return;
    }
    if (cmd != ACAHI_SND_LOAD) return;
    if (!sound_type_ok(a->snd_type)) { a->snd_result = ACAHI_RES_BADTYPE; a->errors++; return; }
    uint64_t bytes = (uint64_t)a->snd_len * frame_bytes(a->snd_type);
    int whole = !a->snd_addr && a->snd_len == 0xffffffffu;      /* AHI's address 0, length ~0: the offset is the address, checked as it plays */
    if (!whole && (!a->snd_len || bytes > 0xffffffffu || !a->ram || !a->ram(a->ram_opaque, a->snd_addr, (uint32_t)bytes))) {
        a->snd_result = ACAHI_RES_BADADDR; a->errors++; return;
    }
    a->sounds[id] = (acahi_sound){ a->snd_addr, a->snd_len, a->snd_type };
    a->snd_result = ACAHI_RES_OK;
}

void acahi_write(acahi *a, uint32_t off, uint32_t v, int size) {
    if (size != 4 || (off & 3)) return;                 /* registers are longwords */
    if (off >= ACAHI_REG_VOICE0 && off < ACAHI_REG_VOICE0 + ACAHI_VOICES * ACAHI_VOICE_STRIDE) {
        uint32_t n = (off - ACAHI_REG_VOICE0) / ACAHI_VOICE_STRIDE;
        acahi_voice *vc = &a->v[n];
        switch ((off - ACAHI_REG_VOICE0) % ACAHI_VOICE_STRIDE) {
        case ACAHI_V_SOUND: vc->s_sound = v; break;
        case ACAHI_V_OFFSET: vc->s_offset = v; break;
        case ACAHI_V_LENGTH: vc->s_length = (int32_t)v; break;
        case ACAHI_V_FREQ: vc->s_freq = v; break;
        case ACAHI_V_VOL: vc->s_vol = (int32_t)v; break;
        case ACAHI_V_PAN: vc->s_pan = (int32_t)v; break;
        case ACAHI_V_CMD: voice_cmd(a, n, v); break;
        }
        return;
    }
    switch (off) {
    case ACAHI_REG_CONTROL:
        if (v & ACAHI_CTL_RESET) { int host = a->host; uint32_t base = a->base; acahi_reset(a); a->host = host; a->base = base; break; }
        if ((v & ACAHI_CTL_STREAM) && !(a->control & ACAHI_CTL_STREAM)) a->dry = a->fifo_r >= a->fifo_w;   /* no underrun before the first frame */
        a->control = v & (ACAHI_CTL_STREAM | ACAHI_CTL_VOICES);
        break;
    case ACAHI_REG_RATE: a->rate = v; break;
    case ACAHI_REG_FORMAT: a->format = v; break;
    case ACAHI_REG_PB_ADDR: a->pb_addr = v; break;
    case ACAHI_REG_PB_FRAMES: a->pb_frames = v; break;
    case ACAHI_REG_PB_SEQ: a->pb_seq = v; break;
    case ACAHI_REG_PB_SUBMIT: stream_submit(a); break;
    case ACAHI_REG_INTENA: a->intena = v & 0xf; break;
    case ACAHI_REG_INTACK: a->intreq &= ~v; break;
    case ACAHI_REG_TIMER_FREQ: a->timer_freq = v; a->timer_ticks = 0; a->timer_acc = 0; break;
    case ACAHI_REG_VOICE_EVENTS: a->voice_events &= ~v; break;
    case ACAHI_REG_MASTER: a->master = v > 0x100000 ? 0x100000 : v; break;
    case ACAHI_REG_SND_ID: a->snd_id = v; break;
    case ACAHI_REG_SND_TYPE: a->snd_type = v; break;
    case ACAHI_REG_SND_ADDR: a->snd_addr = v; break;
    case ACAHI_REG_SND_LEN: a->snd_len = v; break;
    case ACAHI_REG_SND_CMD: sound_cmd(a, v); break;
    case ACAHI_REG_LEVELS: levels_write(a, v); break;
    }
}
