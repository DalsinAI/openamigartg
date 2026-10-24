/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's 3D on a PC (tests/openal/run_host.sh): the six distance
 * models (and AL_NONE), cones, Doppler, the gain steps and the pan, each
 * against a value this file works out in doubles straight from the OpenAL
 * 1.1 specification's formulas (3.4, 3.5, 3.5.2, 4.3.2); then the same
 * scenes as mixed, and the formats as stored (8-bit, AL_EXT_FLOAT32,
 * AL_EXT_MCFORMATS's fold-down), suspend and process, and gain ramps.
 * Exit 0 when every check passes. */
#include "check.h"
#include "host_out.h"

#include <float.h>

static ALCdevice *dev;
static ALCcontext *ctx;

/* ---- References, from the specification ---------------------------------------------- */

/* 3.4.1 to 3.4.6, and "if the formula can not be evaluated then the
 * source will not be attenuated" (3.4). */
static double ref_distance_gain(ALenum model, double d, double ref, double roll, double max)
{
    double den;
    switch (model) {
    case AL_INVERSE_DISTANCE:
    case AL_INVERSE_DISTANCE_CLAMPED:
        if (model == AL_INVERSE_DISTANCE_CLAMPED) {
            d = fmax(d, ref);
            d = fmin(d, max);
        }
        den = ref + roll * (d - ref);
        return den == 0.0 ? 1.0 : ref / den;
    case AL_LINEAR_DISTANCE:
    case AL_LINEAR_DISTANCE_CLAMPED:
        if (model == AL_LINEAR_DISTANCE_CLAMPED) {
            d = fmax(d, ref);
        }
        d = fmin(d, max);
        if (max - ref == 0.0) {
            return 1.0;
        }
        return 1.0 - roll * (d - ref) / (max - ref);
    case AL_EXPONENT_DISTANCE:
    case AL_EXPONENT_DISTANCE_CLAMPED:
        if (model == AL_EXPONENT_DISTANCE_CLAMPED) {
            d = fmax(d, ref);
            d = fmin(d, max);
        }
        if (ref == 0.0) {
            return 1.0;
        }
        return pow(d / ref, -roll);
    default:
        return 1.0;
    }
}

/* 4.3.2, Direction and Cone: angle in degrees off the direction. */
static double ref_cone(double angle, double inner, double outer, double outer_gain)
{
    if (angle <= inner / 2.0) {
        return 1.0;
    }
    if (angle >= outer / 2.0) {
        return outer_gain;
    }
    return 1.0 + (outer_gain - 1.0) * (angle - inner / 2.0) / (outer / 2.0 - inner / 2.0);
}

/* 3.5.2, with AL_DOPPLER_VELOCITY: the ratio f'/f, or HUGE_VAL when the
 * source comes at the speed of sound. sl is the source-to-listener vector. */
static double ref_doppler(const double *sl, const double *lv, const double *sv, double ss, double df, double dv)
{
    double mag = sqrt(sl[0] * sl[0] + sl[1] * sl[1] + sl[2] * sl[2]);
    double vls, vss, den;
    if (df == 0.0 || mag == 0.0) {
        return 1.0;
    }
    vls = (sl[0] * lv[0] + sl[1] * lv[1] + sl[2] * lv[2]) / mag;
    vss = (sl[0] * sv[0] + sl[1] * sv[1] + sl[2] * sv[2]) / mag;
    vss = fmin(vss, ss * dv / df);
    vls = fmin(vls, ss * dv / df);
    den = ss * dv - df * vss;
    if (den <= 0.0) {
        return HUGE_VAL;
    }
    return (ss * dv - df * vls) / den;
}

/* ---- The core's answer for a source ------------------------------------------------------ */

static struct al_params params_of(ALuint name)
{
    ALsource *s;
    AL_LOCK();
    al_context_update(ctx);
    s = al_source_get(ctx, name);
    AL_UNLOCK();
    return s->params;
}

/* ---- Distance models (3.4) ------------------------------------------------------------------ */

static void test_distance_models(void)
{
    static const ALenum models[] = { AL_NONE, AL_INVERSE_DISTANCE, AL_INVERSE_DISTANCE_CLAMPED,
                                     AL_LINEAR_DISTANCE, AL_LINEAR_DISTANCE_CLAMPED,
                                     AL_EXPONENT_DISTANCE, AL_EXPONENT_DISTANCE_CLAMPED };
    /* reference distance, rolloff, max distance: the defaults, ordinary
     * ones, max = ref (linear can't be evaluated), ref = 0, rolloff 0,
     * max below ref, a steep rolloff. */
    static const float sets[][3] = {
        { 1.0f, 1.0f, FLT_MAX }, { 2.0f, 0.5f, 20.0f }, { 1.0f, 2.0f, 10.0f }, { 5.0f, 1.0f, 5.0f },
        { 0.0f, 1.0f, 100.0f }, { 3.0f, 0.0f, 50.0f }, { 10.0f, 1.0f, 2.0f }, { 1.0f, 10.0f, 1000.0f },
    };
    static const float dists[] = { 0.0f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 5.0f, 9.99f, 10.0f, 50.0f, 999.0f, 1.0e6f };
    static const float dir[3] = { 0.48f, -0.6f, 0.64f };   /* unit length */
    static const float lpos[3] = { 1.0f, -2.0f, 3.0f };
    ALuint s;
    size_t m, k, j;
    int compared = 0;

    alGenSources(1, &s);
    alListenerfv(AL_POSITION, lpos);
    for (m = 0; m < sizeof models / sizeof models[0]; m++) {
        alDistanceModel(models[m]);
        for (k = 0; k < sizeof sets / sizeof sets[0]; k++) {
            alSourcef(s, AL_REFERENCE_DISTANCE, sets[k][0]);
            alSourcef(s, AL_ROLLOFF_FACTOR, sets[k][1]);
            alSourcef(s, AL_MAX_DISTANCE, sets[k][2]);
            for (j = 0; j < sizeof dists / sizeof dists[0]; j++) {
                float p[3];
                double dx, dy, dz, d, want;
                struct al_params got;
                p[0] = lpos[0] + dir[0] * dists[j];
                p[1] = lpos[1] + dir[1] * dists[j];
                p[2] = lpos[2] + dir[2] * dists[j];
                alSourcefv(s, AL_POSITION, p);
                dx = (double)p[0] - lpos[0];
                dy = (double)p[1] - lpos[1];
                dz = (double)p[2] - lpos[2];
                d = sqrt(dx * dx + dy * dy + dz * dz);
                want = ref_distance_gain(models[m], d, sets[k][0], sets[k][1], sets[k][2]);
                got = params_of(s);
                if (isinf(want)) {
                    CHECK(got.distance_gain >= 3.0e38f);
                } else {
                    CHECK_NEAR(got.distance_gain, want, 2e-5);
                }
                compared++;
            }
        }
    }
    CHECK(compared == 7 * 8 * 12);
    CHECK_AL(AL_NO_ERROR);
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alDeleteSources(1, &s);
    printf("distance models: %d distances compared\n", compared);
}

/* ---- The gain steps (3.5) -------------------------------------------------------------------- */

static void test_gain_steps(void)
{
    ALuint s;
    struct al_params p;
    double att;
    alGenSources(1, &s);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -4.0f);      /* inverse clamped: 1 / 4 */
    alSourcef(s, AL_GAIN, 0.8f);
    p = params_of(s);
    CHECK_NEAR(p.gain, 0.25 * 0.8f, 1e-6);
    /* Step 4: clamped by AL_MAX_GAIN ... */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -0.5f);      /* within the reference distance: 1 */
    alSourcef(s, AL_GAIN, 2.0f);
    alSourcef(s, AL_MAX_GAIN, 0.9f);
    p = params_of(s);
    CHECK_NEAR(p.gain, 0.9f, 1e-6);
    /* ... and AL_MIN_GAIN, after the distance; step 5: the listener's gain after. */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -1000.0f);
    alSourcef(s, AL_GAIN, 1.0f);
    alSourcef(s, AL_MIN_GAIN, 0.3f);
    alListenerf(AL_GAIN, 0.5f);
    p = params_of(s);
    CHECK_NEAR(p.gain, 0.3f * 0.5, 1e-6);
    /* A listener gain above 1 is applied after the clamp. */
    alListenerf(AL_GAIN, 1.5f);
    alSourcef(s, AL_MIN_GAIN, 0.0f);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -2.0f);
    p = params_of(s);
    att = ref_distance_gain(AL_INVERSE_DISTANCE_CLAMPED, 2.0, 1.0, 1.0, FLT_MAX);
    CHECK_NEAR(p.gain, att * 1.5, 1e-6);
    /* AL_MAX_GAIN 0 mutes it (4.3.2); AL_GAIN 0 too. */
    alSourcef(s, AL_MAX_GAIN, 0.0f);
    p = params_of(s);
    CHECK(p.gain == 0.0f && p.left == 0.0f && p.right == 0.0f);
    alSourcef(s, AL_MAX_GAIN, 1.0f);
    alSourcef(s, AL_GAIN, 0.0f);
    alDistanceModel(AL_EXPONENT_DISTANCE);              /* at the listener: an infinite gain ... */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
    p = params_of(s);
    CHECK(p.gain == 0.0f);                              /* ... times 0 */
    alSourcef(s, AL_GAIN, 1.0f);
    p = params_of(s);
    CHECK_NEAR(p.gain, 1.0 * 1.5, 1e-6);                /* ... clamped by AL_MAX_GAIN */
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alListenerf(AL_GAIN, 1.0f);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
}

/* ---- Cones (4.3.2) -------------------------------------------------------------------------- */

static void test_cones(void)
{
    static const double angles[] = { 0.0, 10.0, 29.9, 30.0, 30.1, 45.0, 59.9, 60.0, 60.1, 75.0, 90.0, 135.0, 179.0, 180.0 };
    /* inner, outer, outer gain */
    static const float cones[][3] = {
        { 60.0f, 120.0f, 0.25f }, { 0.0f, 360.0f, 0.0f }, { 90.0f, 90.0f, 0.5f },
        { 360.0f, 360.0f, 0.0f }, { 120.0f, 60.0f, 0.1f }, { 10.0f, 200.0f, 1.0f },
    };
    ALuint s;
    size_t k, j;
    int compared = 0;
    alGenSources(1, &s);
    alDistanceModel(AL_NONE);
    /* The source ahead of the listener; the listener is along +z from it. */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -10.0f);
    for (k = 0; k < sizeof cones / sizeof cones[0]; k++) {
        alSourcef(s, AL_CONE_INNER_ANGLE, cones[k][0]);
        alSourcef(s, AL_CONE_OUTER_ANGLE, cones[k][1]);
        alSourcef(s, AL_CONE_OUTER_GAIN, cones[k][2]);
        for (j = 0; j < sizeof angles / sizeof angles[0]; j++) {
            double a = angles[j] * 3.14159265358979323846 / 180.0, angle, want;
            float dir[3];
            struct al_params p;
            dir[0] = (float)(3.0 * sin(a));
            dir[1] = 0.0f;
            dir[2] = (float)(3.0 * cos(a));
            alSourcefv(s, AL_DIRECTION, dir);
            /* The angle as the floats give it, to the line from the source to the listener (0, 0, 10). */
            angle = acos(dir[2] / sqrt((double)dir[0] * dir[0] + (double)dir[2] * dir[2])) * 180.0 / 3.14159265358979323846;
            want = ref_cone(angle, cones[k][0], cones[k][1], cones[k][2]);
            p = params_of(s);
            CHECK_NEAR(p.cone_gain, want, 1e-5);
            CHECK_NEAR(p.gain, want, 1e-5);
            compared++;
        }
    }
    /* No direction: not directional, whatever the cone. */
    alSource3f(s, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(s, AL_CONE_INNER_ANGLE, 0.0f);
    alSourcef(s, AL_CONE_OUTER_ANGLE, 0.0f);
    alSourcef(s, AL_CONE_OUTER_GAIN, 0.0f);
    CHECK(params_of(s).cone_gain == 1.0f);
    /* A relative source's direction is in the listener's frame: turn the
     * listener, and a relative cone turns with it. */
    alSource3f(s, AL_DIRECTION, 0.0f, 0.0f, 1.0f);
    alSourcef(s, AL_CONE_INNER_ANGLE, 60.0f);
    alSourcef(s, AL_CONE_OUTER_ANGLE, 120.0f);
    alSourcef(s, AL_CONE_OUTER_GAIN, 0.25f);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    {
        float orient[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
        alListenerfv(AL_ORIENTATION, orient);
        CHECK_NEAR(params_of(s).cone_gain, 1.0, 1e-6);
        alSourcei(s, AL_SOURCE_RELATIVE, AL_FALSE);   /* the same in world terms: the listener is straight down the direction */
        CHECK_NEAR(params_of(s).cone_gain, 1.0, 1e-6);
        alSource3f(s, AL_DIRECTION, 1.0f, 0.0f, 0.0f);
        CHECK_NEAR(params_of(s).cone_gain, ref_cone(90.0, 60.0, 120.0, 0.25), 1e-6);
        orient[0] = 0.0f;
        orient[2] = -1.0f;
        alListenerfv(AL_ORIENTATION, orient);
    }
    alSourcei(s, AL_SOURCE_RELATIVE, AL_FALSE);
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
    printf("cones: %d angles compared\n", compared);
}

/* ---- Doppler (3.5.2) ------------------------------------------------------------------------ */

static void test_doppler(void)
{
    static const float svs[][3] = { { 0, 0, 0 }, { 0, 0, 10 }, { 0, 0, 100 }, { 0, 0, 300 }, { 0, 0, 343.3f },
                                    { 0, 0, 500 }, { 0, 0, -100 }, { 30, 40, 20 }, { -50, 0, -400 } };
    static const float lvs[][3] = { { 0, 0, 0 }, { 0, 0, 50 }, { 0, 0, -50 }, { 10, -20, 600 } };
    /* speed of sound, Doppler factor, Doppler velocity */
    static const float globals[][3] = { { 343.3f, 1.0f, 1.0f }, { 343.3f, 0.0f, 1.0f }, { 343.3f, 0.5f, 1.0f },
                                        { 100.0f, 2.0f, 1.0f }, { 343.3f, 1.0f, 2.0f } };
    static const float spos[3] = { 2.0f, -1.0f, -10.0f };
    ALuint s;
    size_t g, i, j;
    int compared = 0;
    alGenSources(1, &s);
    alSourcefv(s, AL_POSITION, spos);
    alSourcef(s, AL_PITCH, 1.5f);
    for (g = 0; g < sizeof globals / sizeof globals[0]; g++) {
        alSpeedOfSound(globals[g][0]);
        alDopplerFactor(globals[g][1]);
        alDopplerVelocity(globals[g][2]);
        for (i = 0; i < sizeof svs / sizeof svs[0]; i++) {
            for (j = 0; j < sizeof lvs / sizeof lvs[0]; j++) {
                double sl[3], sv[3], lv[3], want;
                struct al_params p;
                int k;
                alSourcefv(s, AL_VELOCITY, svs[i]);
                alListenerfv(AL_VELOCITY, lvs[j]);
                for (k = 0; k < 3; k++) {
                    sl[k] = 0.0 - spos[k];
                    sv[k] = svs[i][k];
                    lv[k] = lvs[j][k];
                }
                want = ref_doppler(sl, lv, sv, globals[g][0], globals[g][1], globals[g][2]);
                p = params_of(s);
                if (isinf(want)) {
                    CHECK(p.doppler >= 1.0e6f);
                } else {
                    CHECK_NEAR(p.doppler, want, 2e-5);
                    CHECK_NEAR(p.pitch, 1.5 * want, 2e-5);
                }
                compared++;
            }
        }
    }
    alSpeedOfSound(343.3f);
    alDopplerFactor(1.0f);
    alDopplerVelocity(1.0f);
    /* A relative source moves with the listener: no shift. */
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(s, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, -100.0f);
    CHECK_NEAR(params_of(s).doppler, 1.0, 1e-6);
    /* Its own velocity is relative to the listener's: toward it at 50. */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -10.0f);
    alSource3f(s, AL_VELOCITY, 0.0f, 0.0f, 50.0f);
    {
        double sl[3] = { 0, 0, 10 }, lv[3] = { 0, 0, -100 }, sv[3] = { 0, 0, -50 };
        CHECK_NEAR(params_of(s).doppler, ref_doppler(sl, lv, sv, 343.3, 1.0, 1.0), 2e-5);
    }
    alSourcei(s, AL_SOURCE_RELATIVE, AL_FALSE);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    /* The source at the listener: no line to project on, no shift. */
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(s, AL_VELOCITY, 0.0f, 0.0f, 200.0f);
    CHECK(params_of(s).doppler == 1.0f);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
    printf("doppler: %d cases compared\n", compared);
}

/* ---- The pan --------------------------------------------------------------------------------- */

static void check_pan(ALuint s, double lateral, double gain)
{
    struct al_params p = params_of(s);
    CHECK_NEAR(p.lateral, lateral, 1e-6);
    CHECK_NEAR(p.left, gain * sqrt((1.0 - lateral) / 2.0), 1e-5);
    CHECK_NEAR(p.right, gain * sqrt((1.0 + lateral) / 2.0), 1e-5);
    /* Equal power. */
    CHECK_NEAR((double)p.left * p.left + (double)p.right * p.right, gain * gain, 1e-5);
}

static void test_pan(void)
{
    ALuint s;
    float orient[6];
    alGenSources(1, &s);
    alDistanceModel(AL_NONE);
    alSource3f(s, AL_POSITION, 1.0f, 0.0f, 0.0f);
    check_pan(s, 1.0, 1.0);
    alSource3f(s, AL_POSITION, -3.0f, 0.0f, 0.0f);
    check_pan(s, -1.0, 1.0);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, -1.0f);
    check_pan(s, 0.0, 1.0);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 5.0f);       /* behind: the centre too */
    check_pan(s, 0.0, 1.0);
    alSource3f(s, AL_POSITION, 1.0f, 0.0f, -1.0f);
    check_pan(s, 1.0 / sqrt(2.0), 1.0);
    alSource3f(s, AL_POSITION, -2.0f, 2.0f, 1.0f);
    check_pan(s, -2.0 / 3.0, 1.0);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);       /* at the listener */
    check_pan(s, 0.0, 1.0);
    alSourcef(s, AL_GAIN, 0.5f);
    alSource3f(s, AL_POSITION, 3.0f, 0.0f, -4.0f);
    check_pan(s, 0.6, 0.5);
    alSourcef(s, AL_GAIN, 1.0f);
    /* The listener turned to face +x: +z is its right. */
    orient[0] = 1.0f; orient[1] = 0.0f; orient[2] = 0.0f;
    orient[3] = 0.0f; orient[4] = 1.0f; orient[5] = 0.0f;
    alListenerfv(AL_ORIENTATION, orient);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 5.0f);
    check_pan(s, 1.0, 1.0);
    alSource3f(s, AL_POSITION, 5.0f, 0.0f, 0.0f);
    check_pan(s, 0.0, 1.0);
    /* ... and moved: positions are from it. */
    alListener3f(AL_POSITION, 10.0f, 0.0f, 10.0f);
    alSource3f(s, AL_POSITION, 10.0f, 0.0f, 7.0f);
    check_pan(s, -1.0, 1.0);
    /* A relative source is in the listener's frame: (1, 0, 0) is its right. */
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(s, AL_POSITION, 1.0f, 0.0f, 0.0f);
    check_pan(s, 1.0, 1.0);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_FALSE);
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    /* "at" and "up" needn't be unit length or at right angles (4.2.1). */
    orient[0] = 0.0f; orient[1] = 0.0f; orient[2] = -2.0f;
    orient[3] = 0.0f; orient[4] = 3.0f; orient[5] = 1.0f;
    alListenerfv(AL_ORIENTATION, orient);
    alSource3f(s, AL_POSITION, 2.0f, 0.0f, 0.0f);
    check_pan(s, 1.0, 1.0);
    /* In line: undefined (4.2.1); the module falls back to the default frame. */
    orient[3] = 0.0f; orient[4] = 0.0f; orient[5] = 1.0f;
    alListenerfv(AL_ORIENTATION, orient);
    check_pan(s, 1.0, 1.0);
    orient[2] = -1.0f; orient[4] = 1.0f; orient[5] = 0.0f;
    alListenerfv(AL_ORIENTATION, orient);
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
}

/* ---- Two channels: no 3D (5.3.4) --------------------------------------------------------------- */

static void test_stereo_source(void)
{
    ALuint s, b;
    ALshort pcm[8];
    struct al_params p;
    memset(pcm, 0, sizeof pcm);
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_STEREO16, pcm, sizeof pcm, 22050);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSource3f(s, AL_POSITION, 100.0f, 0.0f, 0.0f);
    alSource3f(s, AL_VELOCITY, 0.0f, 0.0f, 300.0f);
    alSource3f(s, AL_DIRECTION, 1.0f, 0.0f, 0.0f);
    alSourcef(s, AL_GAIN, 0.6f);
    alSourcef(s, AL_PITCH, 0.75f);
    alListenerf(AL_GAIN, 0.5f);
    p = params_of(s);
    CHECK_NEAR(p.left, 0.6 * 0.5, 1e-6);
    CHECK_NEAR(p.right, 0.6 * 0.5, 1e-6);
    CHECK(p.lateral == 0.0f && p.doppler == 1.0f && p.pitch == 0.75f);
    alSourcef(s, AL_MAX_GAIN, 0.2f);
    CHECK_NEAR(params_of(s).left, 0.2 * 0.5, 1e-6);
    alListenerf(AL_GAIN, 1.0f);
    alSourcei(s, AL_BUFFER, 0);
    alDeleteBuffers(1, &b);
    alDeleteSources(1, &s);
    CHECK_AL(AL_NO_ERROR);
}

/* ---- As mixed -------------------------------------------------------------------------------- */

static ALshort mixbuf[2 * 4096];

static ALint q14(double g)
{
    return g >= 32767.0 / 16384.0 ? 32767 : (ALint)(g * 16384.0 + 0.5);
}

/* n frames mixed into mixbuf. */
static void render(ALuint n)
{
    al_mix_render(dev, mixbuf, n);
}

/* A source playing buf at the device's rate, at the listener (centre). */
static ALuint play_buffer(ALuint b, int relative_x)
{
    ALuint s;
    alGenSources(1, &s);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(s, AL_POSITION, (float)relative_x, 0.0f, 0.0f);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcePlay(s);
    return s;
}

static void stop_and_delete(ALuint s, ALuint b)
{
    alSourceStop(s);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
}

static int all_frames(ALuint n, ALint l, ALint r)
{
    ALuint k;
    for (k = 0; k < n; k++) {
        if (mixbuf[2 * k] != l || mixbuf[2 * k + 1] != r) {
            printf("  frame %u: %d %d, expected %d %d\n", k, mixbuf[2 * k], mixbuf[2 * k + 1], l, r);
            return 0;
        }
    }
    return 1;
}

/* AL_EXT_MCFORMATS's fold-down as al_buffer.c documents it, in doubles:
 * each side its front at 1, the centre and its own rear and side channels
 * at 0.7071, the rear centre at 0.5, LFE left out; over the weights' sum. */
static void ref_fold(const double *x, int layout, double *l, double *r)
{
    static const double c = 0.70710678;
    double wl[8] = { 0 }, wr[8] = { 0 }, sl = 0, sr = 0;
    int n = 0, i;
    switch (layout) {
    case 4:                     /* QUAD: FL FR RL RR */
        n = 4; wl[0] = 1; wr[1] = 1; wl[2] = c; wr[3] = c;
        break;
    case 2:                     /* REAR: RL RR */
        n = 2; wl[0] = 1; wr[1] = 1;
        break;
    case 6:                     /* 5.1: FL FR FC LFE RL RR */
        n = 6; wl[0] = 1; wr[1] = 1; wl[2] = wr[2] = c; wl[4] = c; wr[5] = c;
        break;
    case 7:                     /* 6.1: FL FR FC LFE RC SL SR */
        n = 7; wl[0] = 1; wr[1] = 1; wl[2] = wr[2] = c; wl[4] = wr[4] = 0.5; wl[5] = c; wr[6] = c;
        break;
    default:                    /* 7.1: FL FR FC LFE RL RR SL SR */
        n = 8; wl[0] = 1; wr[1] = 1; wl[2] = wr[2] = c; wl[4] = c; wr[5] = c; wl[6] = c; wr[7] = c;
        break;
    }
    *l = *r = 0;
    for (i = 0; i < n; i++) {
        sl += wl[i];
        sr += wr[i];
    }
    for (i = 0; i < n; i++) {
        *l += wl[i] / sl * x[i];
        *r += wr[i] / sr * x[i];
    }
}

static ALint ref_s16(double v)
{
    double s = v * 32767.0;
    if (s >= 32767.0) return 32767;
    if (s <= -32767.0) return -32767;
    return (ALint)(s < 0 ? s - 0.5 : s + 0.5);
}

static void test_mixed(void)
{
    ALuint b, s;
    ALint gc = q14(sqrt(0.5));
    alDistanceModel(AL_NONE);

    /* Mono 16 at the centre: 0.7071 each side, as Q14. */
    {
        ALshort pcm[256];
        int k;
        for (k = 0; k < 256; k++) {
            pcm[k] = 16384;
        }
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, (ALsizei)dev->frequency);
        s = play_buffer(b, 0);
        render(512);
        CHECK(all_frames(512, (16384 * gc) >> 14, (16384 * gc) >> 14));
        /* To the right: all of it right. A gain change ramps over 64 frames. */
        alSource3f(s, AL_POSITION, 1.0f, 0.0f, 0.0f);
        render(512);
        CHECK(mixbuf[0] == (16384 * gc) >> 14 && mixbuf[1] == (16384 * gc) >> 14);
        CHECK(mixbuf[2 * 63] < mixbuf[0] && mixbuf[2 * 63] > 0 && mixbuf[2 * 63 + 1] > mixbuf[1] && mixbuf[2 * 63 + 1] < 16384);
        {
            int k2, mono = 1;
            for (k2 = 1; k2 < 64; k2++) {
                mono &= mixbuf[2 * k2] <= mixbuf[2 * k2 - 2] && mixbuf[2 * k2 + 1] >= mixbuf[2 * k2 - 1];
            }
            CHECK(mono);
        }
        CHECK(mixbuf[2 * 64] == 0 && mixbuf[2 * 64 + 1] == 16384);
        CHECK(mixbuf[2 * 511] == 0 && mixbuf[2 * 511 + 1] == 16384);
        /* Suspended, the change waits for alcProcessContext (6.2.5, and the
         * design's batching); the sound goes on. */
        alcSuspendContext(ctx);
        alSource3f(s, AL_POSITION, -1.0f, 0.0f, 0.0f);
        alSourcef(s, AL_GAIN, 0.5f);
        render(256);
        CHECK(all_frames(256, 0, 16384));
        alcProcessContext(ctx);
        render(256);
        CHECK(mixbuf[2 * 100] == (16384 * q14(0.5)) >> 14 && mixbuf[2 * 100 + 1] == 0);
        /* AL_GAIN 0: silent. */
        alSourcef(s, AL_GAIN, 0.0f);
        render(256);
        render(256);
        CHECK(all_frames(256, 0, 0));
        stop_and_delete(s, b);
        render(64);
        CHECK(all_frames(64, 0, 0));
    }

    /* 8 bit is unsigned in, signed as stored: 200 is +72, times 256. */
    {
        ALubyte u8[64];
        memset(u8, 200, sizeof u8);
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_MONO8, u8, sizeof u8, (ALsizei)dev->frequency);
        s = play_buffer(b, 0);
        render(128);
        CHECK(all_frames(128, (72 * 256 * gc) >> 14, (72 * 256 * gc) >> 14));
        stop_and_delete(s, b);
        for (int k = 0; k < 64; k += 2) {
            u8[k] = 255;
            u8[k + 1] = 0;
        }
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_STEREO8, u8, sizeof u8, (ALsizei)dev->frequency);
        s = play_buffer(b, 0);
        render(128);
        CHECK(all_frames(128, 127 * 256, -128 * 256));
        stop_and_delete(s, b);
    }

    /* AL_EXT_FLOAT32: -1 to 1 onto 16 bit, clamped; NaN is silence. */
    {
        float fl[8] = { 0.5f, -0.25f, 1.0f, -1.0f, 1.5f, -3.0f, NAN, 0.0f };
        static const ALint want[8] = { 16384, -8192, 32767, -32767, 32767, -32767, 0, 0 };
        int k;
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_STEREO_FLOAT32, fl, sizeof fl, (ALsizei)dev->frequency);
        s = play_buffer(b, 0);
        render(8);
        for (k = 0; k < 8; k++) {
            CHECK(mixbuf[k] == want[k]);
        }
        stop_and_delete(s, b);
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_MONO_FLOAT32, fl, 4 * sizeof(float), (ALsizei)dev->frequency);
        s = play_buffer(b, 1);
        render(4);
        for (k = 0; k < 4; k++) {
            CHECK(mixbuf[2 * k] == 0 && mixbuf[2 * k + 1] == want[k]);
        }
        stop_and_delete(s, b);
    }

    /* AL_EXT_MCFORMATS: folded to stereo, each kind and size. */
    {
        static const ALenum f16[] = { AL_FORMAT_REAR16, AL_FORMAT_QUAD16, AL_FORMAT_51CHN16, AL_FORMAT_61CHN16, AL_FORMAT_71CHN16 };
        static const ALenum f8[] = { AL_FORMAT_REAR8, AL_FORMAT_QUAD8, AL_FORMAT_51CHN8, AL_FORMAT_61CHN8, AL_FORMAT_71CHN8 };
        static const ALenum f32[] = { AL_FORMAT_REAR32, AL_FORMAT_QUAD32, AL_FORMAT_51CHN32, AL_FORMAT_61CHN32, AL_FORMAT_71CHN32 };
        static const int chans[] = { 2, 4, 6, 7, 8 };
        static const ALshort v16[8] = { 1000, -2000, 3000, 30000, -4000, 5000, 32767, -32768 };
        static const ALubyte v8[8] = { 10, 250, 128, 0, 200, 60, 255, 1 };
        static const float v32[8] = { 0.1f, -0.2f, 0.3f, 0.9f, -0.4f, 0.5f, 1.0f, -1.0f };
        size_t i;
        int k;
        for (i = 0; i < 5; i++) {
            int n = chans[i];
            double x[8], l, r;
            ALshort d16[8 * 4];
            ALubyte d8[8 * 4];
            float d32[8 * 4];
            for (k = 0; k < 4 * n; k++) {
                d16[k] = v16[k % n];
                d8[k] = v8[k % n];
                d32[k] = v32[k % n];
            }
            /* 16 */
            for (k = 0; k < n; k++) x[k] = v16[k] / 32768.0;
            ref_fold(x, n, &l, &r);
            alGenBuffers(1, &b);
            alBufferData(b, f16[i], d16, (ALsizei)(4 * n * sizeof(ALshort)), (ALsizei)dev->frequency);
            CHECK_AL(AL_NO_ERROR);
            s = play_buffer(b, 0);
            render(4);
            CHECK(abs(mixbuf[0] - ref_s16(l)) <= 1 && abs(mixbuf[1] - ref_s16(r)) <= 1);
            CHECK(mixbuf[6] == mixbuf[0] && mixbuf[7] == mixbuf[1]);
            stop_and_delete(s, b);
            /* 8 */
            for (k = 0; k < n; k++) x[k] = ((int)v8[k] - 128) / 128.0;
            ref_fold(x, n, &l, &r);
            alGenBuffers(1, &b);
            alBufferData(b, f8[i], d8, 4 * n, (ALsizei)dev->frequency);
            s = play_buffer(b, 0);
            render(4);
            CHECK(abs(mixbuf[0] - ref_s16(l)) <= 1 && abs(mixbuf[1] - ref_s16(r)) <= 1);
            stop_and_delete(s, b);
            /* 32: float */
            for (k = 0; k < n; k++) x[k] = v32[k];
            ref_fold(x, n, &l, &r);
            alGenBuffers(1, &b);
            alBufferData(b, f32[i], d32, (ALsizei)(4 * n * sizeof(float)), (ALsizei)dev->frequency);
            s = play_buffer(b, 0);
            render(4);
            CHECK(abs(mixbuf[0] - ref_s16(l)) <= 1 && abs(mixbuf[1] - ref_s16(r)) <= 1);
            stop_and_delete(s, b);
        }
        /* A full-scale signal in every channel doesn't clip. */
        {
            ALshort full[8];
            double x[8], l, r;
            for (k = 0; k < 8; k++) {
                full[k] = 32767;
                x[k] = 32767 / 32768.0;
            }
            ref_fold(x, 8, &l, &r);
            alGenBuffers(1, &b);
            alBufferData(b, AL_FORMAT_71CHN16, full, sizeof full, (ALsizei)dev->frequency);
            s = play_buffer(b, 0);
            render(1);
            CHECK(abs(mixbuf[0] - ref_s16(l)) <= 1 && mixbuf[0] < 32767 && mixbuf[0] > 32700);
            stop_and_delete(s, b);
        }
    }

    /* Two contexts on one device: both are mixed. */
    {
        ALCcontext *other = alcCreateContext(dev, NULL);
        ALshort pcm[64];
        ALuint b2, s2;
        int k;
        for (k = 0; k < 64; k++) {
            pcm[k] = 1000;
        }
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, (ALsizei)dev->frequency);
        s = play_buffer(b, 1);
        alcMakeContextCurrent(other);
        alDistanceModel(AL_NONE);
        alGenBuffers(1, &b2);       /* buffers are the device's: either context's name works */
        alBufferData(b2, AL_FORMAT_MONO16, pcm, sizeof pcm, (ALsizei)dev->frequency);
        s2 = play_buffer(b, -1);
        render(64);
        CHECK(all_frames(64, 1000, 1000));
        alSourceStop(s2);
        alDeleteSources(1, &s2);
        alDeleteBuffers(1, &b2);
        alcMakeContextCurrent(ctx);
        alcDestroyContext(other);
        stop_and_delete(s, b);
        CHECK_ALC(dev, ALC_NO_ERROR);
    }
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    CHECK_AL(AL_NO_ERROR);
}

int main(void)
{
    ALCint attrs[] = { ALC_FREQUENCY, 44100, ALC_REFRESH, 50, 0 };
    al_core_init();
    dev = alcOpenDevice(NULL);
    ctx = alcCreateContext(dev, attrs);
    alcMakeContextCurrent(ctx);
    CHECK(dev && ctx && dev->frequency == 44100);
    test_distance_models();
    test_gain_steps();
    test_cones();
    test_doppler();
    test_pan();
    test_stereo_source();
    test_mixed();
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK(alcCloseDevice(dev) == ALC_TRUE);
    al_core_shutdown();
    return check_done("test_al_3d");
}
