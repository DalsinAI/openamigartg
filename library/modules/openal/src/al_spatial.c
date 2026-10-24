/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's 3D: a source's gains and pitch from its properties and
 * the listener (OpenAL 1.1 sections 3.4, 3.5 and 4.3.2), worked out when
 * something they depend on changes, never for each sample. Every back end
 * uses it: Mix applies the two gains to its own mix; Voices (P2) turns them
 * into the board's volume and pan.
 *
 *   - Position: a source's position is taken into the listener's frame
 *     (x right, y up, -z ahead, from AL_ORIENTATION's "at" and "up"). A
 *     source with AL_SOURCE_RELATIVE is given in that frame already: its
 *     position, velocity, direction and cone are "relative to the listener"
 *     (4.3.2), and since a direction doesn't move with a position, that
 *     means the listener's orientation as well as its position.
 *   - Gain: the steps of 3.5 in order: the distance model (3.4, with "no
 *     attenuation" where a formula can't be evaluated), times AL_GAIN,
 *     times the cone, clamped to [AL_MIN_GAIN, AL_MAX_GAIN], times the
 *     listener's AL_GAIN.
 *   - Cone: the angle between AL_DIRECTION and the line from the source to
 *     the listener, against half of each cone's angle: AL_GAIN inside the
 *     inner cone, AL_GAIN times AL_CONE_OUTER_GAIN outside the outer, and
 *     linear in the angle between (4.3.2, "Direction and Cone").
 *   - Pan: equal power from the side the source is on, left = gain *
 *     sqrt((1 - x) / 2) and right = gain * sqrt((1 + x) / 2), x being the
 *     sideways part of the direction to the source (-1 left, 1 right).
 *   - Doppler: 3.5.2's formula with AL_DOPPLER_VELOCITY (DV, 1.0 unless a
 *     1.0 program sets it), along the line from the source to the listener.
 *     A relative source's velocity is relative to the listener's. The ratio
 *     multiplies AL_PITCH.
 *   - Buffers with two channels are played without 3D (5.3.4): AL_GAIN,
 *     clamped, times the listener's, to both channels, and AL_PITCH. */
#include "al_internal.h"

#include <math.h>

#define AL_DOPPLER_MAX 1.0e6    /* a source coming at the speed of sound */

static double dot3(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double len3(const double *a)
{
    return sqrt(dot3(a, a));
}

/* 3.4.1 to 3.4.6. */
double al_distance_gain(ALenum model, double d, double ref, double roll, double max)
{
    double den;
    switch (model) {
    case AL_INVERSE_DISTANCE_CLAMPED:
        if (d < ref) d = ref;
        if (d > max) d = max;
        /* fall through */
    case AL_INVERSE_DISTANCE:
        den = ref + roll * (d - ref);
        return den == 0.0 ? 1.0 : ref / den;
    case AL_LINEAR_DISTANCE_CLAMPED:
        if (d < ref) d = ref;
        /* fall through */
    case AL_LINEAR_DISTANCE:
        if (d > max) d = max;
        return max == ref ? 1.0 : 1.0 - roll * (d - ref) / (max - ref);
    case AL_EXPONENT_DISTANCE_CLAMPED:
        if (d < ref) d = ref;
        if (d > max) d = max;
        /* fall through */
    case AL_EXPONENT_DISTANCE:
        if (ref == 0.0) {
            return 1.0;
        }
        if (d == 0.0) {
            return roll > 0.0 ? HUGE_VAL : 1.0;
        }
        return pow(d / ref, -roll);
    default:                    /* AL_NONE */
        return 1.0;
    }
}

/* A vector into the listener's frame. */
static void al_to_listener(const double *r, const double *u, const double *f, const ALfloat *v, double *out)
{
    double w[3];
    w[0] = v[0];
    w[1] = v[1];
    w[2] = v[2];
    out[0] = dot3(w, r);
    out[1] = dot3(w, u);
    out[2] = -dot3(w, f);
}

static double al_clamp_gain(const ALsource *s, double g)
{
    if (g < s->min_gain) g = s->min_gain;
    if (g > s->max_gain) g = s->max_gain;
    return g;
}

void al_source_params(const ALCcontext *c, const ALsource *s, ALint channels, struct al_params *p)
{
    double f[3], up[3], r[3], u[3], n;
    double pos[3], sv[3], lv[3], dir[3], d, gain, dist = 1.0, cone = 1.0, lateral = 0.0, doppler = 1.0;
    int k;

    if (channels != 1) {
        gain = al_clamp_gain(s, s->gain) * c->listener_gain;
        p->left = p->right = p->gain = (ALfloat)gain;
        p->lateral = 0.0f;
        p->distance_gain = p->cone_gain = p->doppler = 1.0f;
        p->pitch = s->pitch;
        return;
    }

    /* The listener's frame (4.2.1): at and up need not be unit length, nor
     * at right angles; when they are in line, the default frame. */
    for (k = 0; k < 3; k++) {
        f[k] = c->listener_at[k];
        up[k] = c->listener_up[k];
    }
    r[0] = f[1] * up[2] - f[2] * up[1];
    r[1] = f[2] * up[0] - f[0] * up[2];
    r[2] = f[0] * up[1] - f[1] * up[0];
    n = len3(f);
    if (len3(r) > 0.0 && n > 0.0) {
        double rl = len3(r);
        for (k = 0; k < 3; k++) {
            f[k] /= n;
            r[k] /= rl;
        }
    } else {
        f[0] = 0.0; f[1] = 0.0; f[2] = -1.0;
        r[0] = 1.0; r[1] = 0.0; r[2] = 0.0;
    }
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];

    al_to_listener(r, u, f, c->listener_velocity, lv);
    if (s->relative) {
        for (k = 0; k < 3; k++) {
            pos[k] = s->position[k];
            sv[k] = s->velocity[k] + lv[k];
            dir[k] = s->direction[k];
        }
    } else {
        ALfloat rel[3];
        for (k = 0; k < 3; k++) {
            rel[k] = s->position[k] - c->listener_position[k];
        }
        al_to_listener(r, u, f, rel, pos);
        al_to_listener(r, u, f, s->velocity, sv);
        al_to_listener(r, u, f, s->direction, dir);
    }
    d = len3(pos);

    /* 3.5 step 1. */
    dist = al_distance_gain(c->distance_model, d, s->ref_distance, s->rolloff, s->max_distance);
    gain = dist * s->gain;

    /* Step 3: the cone. */
    n = len3(dir);
    if (n > 0.0 && d > 0.0) {
        double cosang = -dot3(dir, pos) / (n * d), angle;
        double inner = s->cone_inner * 0.5, outer = s->cone_outer * 0.5;
        if (cosang > 1.0) cosang = 1.0;
        if (cosang < -1.0) cosang = -1.0;
        angle = acos(cosang) * (180.0 / 3.14159265358979323846);
        if (angle <= inner) {
            cone = 1.0;
        } else if (angle >= outer) {
            cone = s->cone_outer_gain;
        } else {
            cone = 1.0 + (s->cone_outer_gain - 1.0) * (angle - inner) / (outer - inner);
        }
    }
    gain *= cone;
    if (!(gain == gain)) {
        gain = 0.0;             /* an infinite distance gain times a zero AL_GAIN or cone: muted */
    }

    /* Steps 4 and 5. */
    gain = al_clamp_gain(s, gain) * c->listener_gain;

    if (d > 0.0) {
        lateral = pos[0] / d;
        if (lateral > 1.0) lateral = 1.0;
        if (lateral < -1.0) lateral = -1.0;
    }

    /* 3.5.2, along SL, the line from the source to the listener. */
    if (c->doppler_factor > 0.0f && d > 0.0) {
        double ss = (double)c->speed_of_sound * c->doppler_velocity, df = c->doppler_factor;
        double vls = -dot3(pos, lv) / d, vss = -dot3(pos, sv) / d, limit = ss / df, num, den;
        if (vss > limit) vss = limit;
        if (vls > limit) vls = limit;
        num = ss - df * vls;
        den = ss - df * vss;
        doppler = den > 0.0 ? num / den : AL_DOPPLER_MAX;
        if (doppler > AL_DOPPLER_MAX) doppler = AL_DOPPLER_MAX;
    }

    p->gain = (ALfloat)gain;
    p->lateral = (ALfloat)lateral;
    p->left = (ALfloat)(gain * sqrt((1.0 - lateral) * 0.5));
    p->right = (ALfloat)(gain * sqrt((1.0 + lateral) * 0.5));
    p->distance_gain = dist > 3.0e38 ? 3.0e38f : (ALfloat)dist;
    p->cone_gain = (ALfloat)cone;
    p->doppler = (ALfloat)doppler;
    p->pitch = (ALfloat)(s->pitch * doppler);
}

void al_context_update(ALCcontext *c)
{
    ALsource *s;
    for (s = c->sources; s; s = s->next) {
        if (c->dirty || (s->dirty & AL_DIRTY_PARAMS)) {
            al_source_params(c, s, al_source_channels(s), &s->params);
            s->dirty &= ~AL_DIRTY_PARAMS;
        }
    }
    c->dirty = AL_FALSE;
}
