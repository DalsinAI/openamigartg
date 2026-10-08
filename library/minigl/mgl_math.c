/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's arithmetic. The 68040's FPU adds, multiplies, divides and
 * takes square roots; sines, exponentials and the conversions it does not
 * have trap to software that is slow or missing, so these are written out
 * with what it has. Accurate to a few units in the last place of a float,
 * which is all OpenGL asks. */
#include "mgl_internal.h"

LONG mgl_f2l(float f)
{
#if defined(__m68k__) && !defined(MGL_HOST)
    LONG r;
    __asm__("fmove.l %1,%0" : "=d"(r) : "f"(f));   /* rounded to the nearest (the FPU's own mode) */
    return r;
#else
    return (LONG)(f < 0.0f ? f - 0.5f : f + 0.5f);
#endif
}

ULONG mgl_fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

float mgl_floor(float x)
{
    LONG i;
    if (x >= 8388608.0f || x <= -8388608.0f) return x;     /* already whole */
    i = mgl_f2l(x);
    if ((float)i > x) i--;
    return (float)i;
}

float mgl_sqrt(float x)
{
    if (!(x > 0.0f)) return 0.0f;
#if defined(__m68k__) && !defined(MGL_HOST)
    {
        float r;
        __asm__("fsqrt.x %1,%0" : "=f"(r) : "f"(x));
        return r;
    }
#else
    return __builtin_sqrtf(x);
#endif
}

float mgl_rsqrt(float x)
{
    float s = mgl_sqrt(x);
    return s > 0.0f ? 1.0f / s : 0.0f;
}

/* sin on [-pi/4, pi/4] and cos on the same, by their series (to x^11 and x^10). */
static float sin_k(float x)
{
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f
             + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}

static float cos_k(float x)
{
    float x2 = x * x;
    return 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f
             + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f)))));
}

/* x as k quarter turns plus a remainder in [-pi/4, pi/4]. */
static float reduce(float x, LONG *k)
{
    double q = (double)x * (2.0 / 3.14159265358979323846);
    LONG n = mgl_f2l((float)q);
    *k = n;
    return (float)((double)x - (double)n * 1.57079632679489661923);
}

float mgl_sin(float x)
{
    LONG k;
    float r = reduce(x, &k);
    switch (k & 3) {
    case 0: return sin_k(r);
    case 1: return cos_k(r);
    case 2: return -sin_k(r);
    default: return -cos_k(r);
    }
}

float mgl_cos(float x)
{
    LONG k;
    float r = reduce(x, &k);
    switch (k & 3) {
    case 0: return cos_k(r);
    case 1: return -sin_k(r);
    case 2: return -cos_k(r);
    default: return sin_k(r);
    }
}

float mgl_tan(float x)
{
    float c = mgl_cos(x);
    return c != 0.0f ? mgl_sin(x) / c : 1e30f;
}

/* 2^x: the whole part in the exponent, the rest by a polynomial on [-0.5, 0.5]. */
static float exp2_f(float x)
{
    LONG n;
    float f, p;
    union { float f; ULONG u; } s;
    if (x > 126.0f) return 1e38f;
    if (x < -126.0f) return 0.0f;
    n = mgl_f2l(x);
    f = x - (float)n;                           /* -0.5 .. 0.5 */
    /* 2^f = e^(f ln 2), its series to the seventh power */
    f *= 0.69314718f;
    p = 1.0f + f * (1.0f + f * (0.5f + f * (1.0f / 6.0f + f * (1.0f / 24.0f + f * (1.0f / 120.0f
        + f * (1.0f / 720.0f + f * (1.0f / 5040.0f)))))));
    s.u = (ULONG)(n + 127) << 23;
    return p * s.f;
}

float mgl_exp(float x)
{
    return exp2_f(x * 1.44269504f);
}

/* log2 x: the exponent, and the mantissa m in [sqrt(1/2), sqrt(2)) by
 * log(m) = 2 atanh((m - 1) / (m + 1)). */
float mgl_log2(float x)
{
    union { float f; ULONG u; } v;
    LONG e;
    float m, t, t2, l;
    if (!(x > 0.0f)) return -1e30f;
    v.f = x;
    e = (LONG)((v.u >> 23) & 255) - 127;
    v.u = (v.u & 0x007FFFFFUL) | 0x3F800000UL;  /* 1 .. 2 */
    m = v.f;
    if (m > 1.41421356f) { m *= 0.5f; e++; }
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    l = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f + t2 * (1.0f / 9.0f)))));
    return (float)e + l * 1.44269504f;
}

float mgl_pow(float x, float y)
{
    if (y == 0.0f) return 1.0f;
    if (!(x > 0.0f)) return 0.0f;
    if (y == 1.0f) return x;
    return exp2_f(y * mgl_log2(x));
}

static ULONG ch(float v)
{
    LONG c = mgl_f2l(v * 255.0f);
    return c < 0 ? 0 : c > 255 ? 255 : (ULONG)c;
}

ULONG mgl_argb(const float *c)
{
    return (ch(c[3]) << 24) | (ch(c[0]) << 16) | (ch(c[1]) << 8) | ch(c[2]);
}
