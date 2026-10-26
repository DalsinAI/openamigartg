/* Original procedural score. Copyright (c) 2026 Dalsin Limited. MIT. */
#ifndef OPENTENSOR_SCORE_H
#define OPENTENSOR_SCORE_H
#include <stdint.h>
#include <string.h>
#include <math.h>

/* A minor / F major / C major / G major, 128 BPM. Lookup oscillators keep
 * transcendental functions out of the AHI buffer pump. No external samples. */
struct OtScore {
    uint64_t cursor;
    uint32_t phase[6], noise;
    int32_t filter, noise_prev;
    int16_t echo[2][8192];
    unsigned echo_pos;
};
static int16_t ot_score_sine[1024];
static uint32_t ot_score_pitch[48];
static int ot_score_tables_ready;

static void ot_score_init(struct OtScore *s, uint64_t cursor)
{
    unsigned i;
    if (!ot_score_tables_ready) {
        for (i = 0; i < 1024; ++i)
            ot_score_sine[i] = (int16_t)(sin(i * 6.283185307179586 / 1024.0) * 32767.0);
        for (i = 0; i < 48; ++i)
            ot_score_pitch[i] = (uint32_t)(65.406391325 * pow(2.0, i / 12.0) *
                                         4294967296.0 / 44100.0);
        ot_score_tables_ready = 1;
    }
    memset(s, 0, sizeof(*s));
    s->cursor = cursor;
    s->noise = 0x13579bdfu;
}

static int32_t ot_score_wave(uint32_t phase)
{
    return ot_score_sine[phase >> 22];
}

static int16_t ot_score_limit(int32_t x)
{
    return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

static void ot_score_render(struct OtScore *s, int16_t *dst, unsigned frames)
{
    static const unsigned roots[4] = {21, 17, 12, 19};
    static const unsigned melody[16] = {12, 7, 10, 7, 14, 12, 7, 3,
                                        12, 15, 14, 7, 10, 7, 3, 7};
    const unsigned step_samples = 5168;
    uint64_t step = s->cursor / step_samples;
    unsigned within = (unsigned)(s->cursor % step_samples), i;
    unsigned seconds = (unsigned)(s->cursor / 44100) % 164;
    unsigned second_remaining = 44100 - (unsigned)(s->cursor % 44100);
    for (i = 0; i < frames; ++i, ++s->cursor) {
        unsigned bar = (unsigned)(step / 16), tick = (unsigned)step & 15;
        unsigned root = roots[(bar / 2) & 3];
        unsigned minor = ((bar / 2) & 3) == 0;
        unsigned age = tick % 4 * step_samples + within;
        unsigned snare_age = tick % 8 * step_samples + within;
        int32_t env = (int32_t)(step_samples - within);
        int32_t lead, bass, pad, kick = 0, snare = 0, hat = 0, left, right;
        int32_t noise, attack = within < 220 ? (int32_t)within : 220;
        int32_t gain = seconds < 10 ? (int32_t)(seconds * 100 + (44100 - second_remaining) / 441) : 1000;
        if (seconds >= 152) gain = (164 - seconds) * 1000 / 12;
        s->phase[0] += ot_score_pitch[root + melody[tick]];
        s->phase[1] += ot_score_pitch[root - 12];
        s->phase[2] += ot_score_pitch[root];
        s->phase[3] += ot_score_pitch[root + (minor ? 3 : 4)] + 7000;
        s->phase[4] += ot_score_pitch[root + 7] - 5000;
        lead = (ot_score_wave(s->phase[0]) + ot_score_wave(s->phase[0] * 2u) / 4) / 9;
        lead = lead * env / (int32_t)step_samples * attack / 220;
        bass = (ot_score_wave(s->phase[1]) + ot_score_wave(s->phase[1] * 2u) / 3) / 10;
        pad = (ot_score_wave(s->phase[2]) + ot_score_wave(s->phase[3]) +
               ot_score_wave(s->phase[4])) / 32;
        s->filter += (pad - s->filter) / 12;
        s->noise ^= s->noise << 13; s->noise ^= s->noise >> 17; s->noise ^= s->noise << 5;
        noise = (int32_t)(s->noise >> 16) - 32768;
        if (age < 8000) {
            s->phase[5] += 4600000u + (8000u - age) * 2000u;
            kick = ot_score_wave(s->phase[5]) / 5 * (int32_t)(8000 - age) / 8000;
        }
        if (snare_age >= 4 * step_samples && snare_age < 4 * step_samples + 6000)
            snare = (noise / 11 + ot_score_wave(s->phase[1] * 3u) / 25) *
                    (int32_t)(4 * step_samples + 6000 - snare_age) / 6000;
        if (within < ((tick & 1) ? 1900u : 650u))
            hat = (noise - s->noise_prev) / 40 * (int32_t)(1900 - within) / 1900;
        s->noise_prev = noise;
        if (seconds < 10 || (seconds >= 64 && seconds < 78)) {
            kick /= 3; snare = 0; hat /= 3; lead /= 2;
        }
        left = bass + kick + snare + hat + s->filter + lead;
        right = bass + kick + snare - hat / 2 + s->filter + lead * 3 / 4;
        /* Cross-fed dotted-eighth echo; feedback remains below unity. */
        {
            unsigned read = (s->echo_pos + 8192 - 7752) & 8191;
            int32_t el = s->echo[0][read], er = s->echo[1][read];
            s->echo[0][s->echo_pos] = ot_score_limit(lead / 2 + er / 3);
            s->echo[1][s->echo_pos] = ot_score_limit(lead / 3 + el / 3);
            s->echo_pos = (s->echo_pos + 1) & 8191;
            left += el; right += er;
        }
        dst[i * 2] = ot_score_limit(left * gain / 1000);
        dst[i * 2 + 1] = ot_score_limit(right * gain / 1000);
        if (--second_remaining == 0) { second_remaining = 44100; seconds = (seconds + 1) % 164; }
        if (++within == step_samples) { within = 0; ++step; }
    }
}
#endif
