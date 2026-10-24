/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Voices' output for the tests (library/modules/openal/src/al_ahi_voices.c
 * is the Amiga's real one, through AHI). The board here is ACAHI's own
 * code: tests/openal/acahi holds the runtime's acahi.c, byte for byte
 * (its README says from which commit). This file plays the part of AHI and
 * acaudio.audio's Host mix between OpenAL and the board:
 *   - al_vout_open takes voices 0 to n-1, loads AHI's all-of-memory sounds
 *     for the four formats (address 0, length 0xFFFFFFFF) and sets the
 *     timer to 50 ticks a second, as al_ahi_voices.c asks AHI to;
 *   - SetSound, SetFreq and SetVol are the voice registers, as
 *     acaudio.audio writes them (with AHISF_IMM, a voice's end not yet told
 *     is dropped, as the broker in acaudio.audio drops it);
 *   - the board plays host_voices_chunk frames at a time, and then its
 *     interrupt is served as acaudio.audio serves it: the voices' events
 *     (AHI's SoundFunc: al_voices_event), the timer's ticks (PlayerFunc:
 *     the clock) and, after them, the channel info (al_voices_info).
 * Guest RAM: the board reads 32-bit big-endian addresses. Each segment the
 * module plans gets a copy of its frames in the board's order (16-bit
 * samples big-endian, as on the 68k) at an address of its own, so the
 * board reads them as it would read the module's buffers on an Amiga. */
#include "host_voices.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "acahi.h"

int host_voices_fitted;
ALuint host_voices_free = ACAHI_VOICES;
ALuint host_voices_chunk = 64;
int host_voices_service_auto = 1;
unsigned long host_voices_sounds, host_voices_vols, host_voices_freqs, host_voices_refused;
int host_voices_lock_depth;

#define HV_TICK 50                              /* PlayerFunc's rate, as al_ahi_voices.c asks */
#define HV_TICK_FRAMES (ACAHI_OUT_RATE / HV_TICK)
#define HV_RAM_AT 0x00100000u

struct hv_region {
    const void *host;           /* the module's frames */
    uint8_t *copy;              /* in the board's order */
    uint32_t addr, len;
};

struct host_voices {
    acahi board;
    ALCdevice *device;
    ALuint n;
    uint32_t last_ticks;
    ALuint ticks;
    int running, woken;
    struct hv_region *regions;
    size_t nregions, room;
    uint32_t next_addr;
    int32_t *mix;
    ALuint mix_frames;
};

static struct host_voices *HV(ALCdevice *d)
{
    return d->out;
}

static uint8_t *hv_ram(void *opaque, uint32_t addr, uint32_t len)
{
    struct host_voices *h = opaque;
    size_t i;
    for (i = h->nregions; i-- > 0;) {
        struct hv_region *r = &h->regions[i];
        if (addr >= r->addr && (uint64_t)addr + len <= (uint64_t)r->addr + r->len) {
            return r->copy + (addr - r->addr);
        }
    }
    return NULL;
}

static void wr(struct host_voices *h, uint32_t off, uint32_t v)
{
    acahi_write(&h->board, off, v, 4);
}

static uint32_t rd(struct host_voices *h, uint32_t off)
{
    return acahi_read(&h->board, off, 4);
}

#define VREG(k, r) (ACAHI_REG_VOICE0 + (k) * ACAHI_VOICE_STRIDE + (r))

ALCboolean al_vout_available(void)
{
    return host_voices_fitted ? ALC_TRUE : ALC_FALSE;
}

ALCboolean al_vout_open(ALCdevice *d, ALuint n)
{
    struct host_voices *h;
    ALuint k;
    if (!host_voices_fitted || n > host_voices_free || n > ACAHI_VOICES) {
        return ALC_FALSE;
    }
    h = calloc(1, sizeof *h);
    if (!h) {
        return ALC_FALSE;
    }
    if (acahi_init(&h->board, hv_ram, h)) {
        free(h);
        return ALC_FALSE;
    }
    h->board.host = 1;
    h->device = d;
    h->n = n;
    h->next_addr = HV_RAM_AT;
    d->out = h;
    /* AHI's all-of-memory sounds: M8S, M16S, S8S, S16S in slots 0 to 3. */
    for (k = 0; k < 4; k++) {
        wr(h, ACAHI_REG_SND_ID, k);
        wr(h, ACAHI_REG_SND_TYPE, k);
        wr(h, ACAHI_REG_SND_ADDR, 0);
        wr(h, ACAHI_REG_SND_LEN, 0xFFFFFFFFu);
        wr(h, ACAHI_REG_SND_CMD, ACAHI_SND_LOAD);
    }
    /* The master volume al_ahi_voices.c sets (n / 2), times acaudio's
     * 2 / n: unity. */
    wr(h, ACAHI_REG_MASTER, 0x10000);
    for (k = 0; k < n; k++) {
        wr(h, VREG(k, ACAHI_V_SOUND), ACAHI_NOSOUND);
        wr(h, VREG(k, ACAHI_V_OFFSET), 0);
        wr(h, VREG(k, ACAHI_V_LENGTH), 0);
        wr(h, VREG(k, ACAHI_V_FREQ), 0);
        wr(h, VREG(k, ACAHI_V_VOL), 0);
        wr(h, VREG(k, ACAHI_V_PAN), 0x8000);
        wr(h, VREG(k, ACAHI_V_CMD), ACAHI_VC_SOUND | ACAHI_VC_FREQ | ACAHI_VC_VOL | ACAHI_VC_IMM);
    }
    wr(h, ACAHI_REG_TIMER_FREQ, (uint32_t)HV_TICK << 16);
    wr(h, ACAHI_REG_INTENA, ACAHI_INT_VOICE | ACAHI_INT_TIMER);
    wr(h, ACAHI_REG_CONTROL, ACAHI_CTL_VOICES);
    d->frequency = ACAHI_OUT_RATE;
    d->refresh = HV_TICK;
    return ALC_TRUE;
}

void al_vout_close(ALCdevice *d)
{
    struct host_voices *h = HV(d);
    size_t i;
    if (!h) {
        return;
    }
    for (i = 0; i < h->nregions; i++) {
        free(h->regions[i].copy);
    }
    free(h->regions);
    free(h->mix);
    acahi_free(&h->board);
    free(h);
    d->out = NULL;
}

ALCboolean al_vout_start(ALCdevice *d)
{
    HV(d)->running = 1;
    return ALC_TRUE;
}

void al_vout_stop(ALCdevice *d)
{
    HV(d)->running = 0;
}

unsigned al_vout_sound(ALCdevice *d, ALint bits, ALint channels)
{
    (void)d;
    return (bits == 16 ? 1u : 0u) + (channels == 2 ? 2u : 0u);
}

ALuint al_vout_frame(ALCdevice *d, const void *data, ALuint frames, ALint bits, ALint channels)
{
    struct host_voices *h = HV(d);
    uint32_t fb = (uint32_t)(bits / 8 * channels), len = frames * fb, i;
    struct hv_region *r;
    size_t k;
    uint8_t *copy;
    /* The same frames planned again (a loop, a stream's buffer queued
     * again with the same data): the copy already made. */
    for (k = h->nregions; k-- > 0 && k + 64 >= h->nregions;) {
        r = &h->regions[k];
        if (r->host == data && r->len == len) {
            int same = 1;
            if (bits == 16) {
                const ALshort *s = data;
                for (i = 0; i < len / 2 && same; i++) {
                    same = r->copy[2 * i] == (uint8_t)((uint16_t)s[i] >> 8) && r->copy[2 * i + 1] == (uint8_t)s[i];
                }
            } else {
                same = !memcmp(r->copy, data, len);
            }
            if (same) {
                return r->addr / fb;
            }
        }
    }
    if (h->nregions == h->room) {
        size_t room = h->room ? h->room * 2 : 64;
        struct hv_region *nr = realloc(h->regions, room * sizeof *nr);
        if (!nr) {
            abort();
        }
        h->regions = nr;
        h->room = room;
    }
    copy = malloc(len ? len : 1);
    if (!copy) {
        abort();
    }
    if (bits == 16) {
        const ALshort *s = data;
        for (i = 0; i < len / 2; i++) {
            copy[2 * i] = (uint8_t)((uint16_t)s[i] >> 8);
            copy[2 * i + 1] = (uint8_t)s[i];
        }
    } else {
        memcpy(copy, data, len);
    }
    r = &h->regions[h->nregions++];
    r->host = data;
    r->copy = copy;
    r->addr = h->next_addr;
    r->len = len;
    h->next_addr += (len + 15u) & ~15u;
    return r->addr / fb;
}

void al_vout_play(ALCdevice *d, ALuint k, unsigned sound, ALuint offset, ALuint frames, int imm)
{
    struct host_voices *h = HV(d);
    host_voices_sounds++;
    if (imm) {
        wr(h, ACAHI_REG_VOICE_EVENTS, 1u << k);     /* an end not yet told: dropped */
    }
    wr(h, VREG(k, ACAHI_V_SOUND), sound == AL_VOUT_NOSOUND ? ACAHI_NOSOUND : sound);
    wr(h, VREG(k, ACAHI_V_OFFSET), offset);
    wr(h, VREG(k, ACAHI_V_LENGTH), frames);
    wr(h, VREG(k, ACAHI_V_CMD), ACAHI_VC_SOUND | (imm ? ACAHI_VC_IMM : 0));
    if (rd(h, ACAHI_REG_SND_CMD) != ACAHI_RES_OK) {
        host_voices_refused++;
    }
}

void al_vout_freq(ALCdevice *d, ALuint k, ALuint freq)
{
    struct host_voices *h = HV(d);
    host_voices_freqs++;
    wr(h, VREG(k, ACAHI_V_FREQ), freq);
    wr(h, VREG(k, ACAHI_V_CMD), ACAHI_VC_FREQ | ACAHI_VC_IMM);
}

void al_vout_vol(ALCdevice *d, ALuint k, ALint vol, ALint pan)
{
    struct host_voices *h = HV(d);
    host_voices_vols++;
    wr(h, VREG(k, ACAHI_V_VOL), (uint32_t)vol);
    wr(h, VREG(k, ACAHI_V_PAN), (uint32_t)pan);
    wr(h, VREG(k, ACAHI_V_CMD), ACAHI_VC_VOL | ACAHI_VC_IMM);
}

void al_vout_lock(ALCdevice *d)
{
    (void)d;
    host_voices_lock_depth++;
}

void al_vout_unlock(ALCdevice *d)
{
    (void)d;
    host_voices_lock_depth--;
}

ALuint al_vout_clock(ALCdevice *d)
{
    return HV(d)->ticks * HV_TICK_FRAMES;
}

void al_vout_wake(ALCdevice *d)
{
    HV(d)->woken = 1;
}

/* The board's interrupt, served as acaudio.audio serves it. */
static void hv_interrupt(ALCdevice *d)
{
    struct host_voices *h = HV(d);
    uint32_t bits = rd(h, ACAHI_REG_INTREQ) & rd(h, ACAHI_REG_INTENA), k;
    if (!bits) {
        return;
    }
    wr(h, ACAHI_REG_INTACK, bits);
    if (bits & ACAHI_INT_VOICE) {
        uint32_t ev = rd(h, ACAHI_REG_VOICE_EVENTS);
        wr(h, ACAHI_REG_VOICE_EVENTS, ev);
        for (k = 0; k < h->n; k++) {
            if (ev & (1u << k)) {
                al_voices_event(d, k);              /* SoundFunc */
                al_vout_wake(d);
            }
        }
    }
    if (bits & ACAHI_INT_TIMER) {
        uint32_t t = rd(h, ACAHI_REG_TIMER_TICKS), n = t - h->last_ticks;
        h->last_ticks = t;
        if (n) {
            h->ticks += n;                          /* PlayerFunc */
            al_vout_wake(d);
            for (k = 0; k < h->n; k++) {            /* the channel info after it */
                al_voices_info(d, k, rd(h, VREG(k, ACAHI_V_POS)));
            }
        }
    }
    if (h->woken && host_voices_service_auto && h->running) {
        h->woken = 0;
        al_voices_service(d);
    }
}

void host_voices_render(ALCdevice *d, ALshort *out, ALuint frames)
{
    struct host_voices *h = HV(d);
    ALuint chunk = host_voices_chunk ? host_voices_chunk : 64, i;
    if (h->mix_frames < chunk) {
        free(h->mix);
        h->mix = malloc((size_t)chunk * 2 * sizeof *h->mix);
        if (!h->mix) {
            abort();
        }
        h->mix_frames = chunk;
    }
    while (frames) {
        ALuint n = frames < chunk ? frames : chunk;
        memset(h->mix, 0, (size_t)n * 2 * sizeof *h->mix);
        acahi_render(&h->board, h->mix, n);
        for (i = 0; i < 2 * n; i++) {
            int32_t v = h->mix[i];
            out[i] = (ALshort)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        out += 2 * n;
        frames -= n;
        hv_interrupt(d);
    }
}

struct acahi *host_voices_board(ALCdevice *d)
{
    return &HV(d)->board;
}

ALuint host_voices_pos(ALCdevice *d, ALuint k)
{
    return rd(HV(d), VREG(k, ACAHI_V_POS));
}

ALuint host_voices_buffer_frame(ALCdevice *d, ALuint k)
{
    struct host_voices *h = HV(d);
    uint32_t snd = h->board.v[k].cur.snd, type = snd < ACAHI_SOUNDS ? h->board.sounds[snd].type : 0;
    uint32_t fb = type == 0 ? 1 : type == 3 ? 4 : 2, at = host_voices_pos(d, k) * fb;
    size_t i;
    for (i = h->nregions; i-- > 0;) {
        if (at >= h->regions[i].addr && at < h->regions[i].addr + h->regions[i].len) {
            return (at - h->regions[i].addr) / fb;
        }
    }
    return 0xFFFFFFFFu;
}

int host_voices_on(ALCdevice *d, ALuint k)
{
    return (int)(rd(HV(d), VREG(k, ACAHI_V_CMD)) & 1u);
}

ALuint host_voices_held(ALCdevice *d)
{
    return HV(d)->n;
}
