/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's 3D core (stream v1.2): see ogpu_3d.h. The Team wrote it from the
 * Warp3D and OpenGL 1.x specifications; software Warp3Ds (Wazp3D, AROS's
 * Warp3D) were behaviour references only, and none of their code is here.
 *
 * How a triangle is drawn:
 *  - Positions become 28.4 fixed point. Rows and spans come from exact
 *    integer edge stepping (quotient and remainder), with pixel centres at
 *    +0.5 and the top-left rule, so two triangles sharing an edge neither
 *    overlap nor leave a gap, whichever order their vertices come in.
 *  - Every other value (colour, specular, fog, Z, texture coordinates, the
 *    perspective divisor) is a plane: its value at one reference pixel and
 *    its steps across and down, in 32-bit fixed point. Sums are modulo 2^32,
 *    so a value is exact wherever it fits, even when the bounding box around
 *    a sliver would overflow.
 *  - Perspective-correct texturing divides every 8 pixels and steps linearly
 *    between; colours and Z are linear in the screen, as 3D hardware does.
 *  - Each pixel then goes through the fixed-function pipeline in OpenGL's
 *    order: stipple, texture, chroma test, environment, specular, fog, alpha
 *    test, stencil, Z, blend or logic op, colour mask, write.
 *
 * Speed on the 68k. The inner loops keep to what a 68040 and the AC090 both
 * do quickly: byte access for colour channels, immediate shifts of 8 or
 * less, swap for 16, word multiplies, and one 64/32 divide per 8 pixels
 * under perspective. Shifts by a register count, 32-bit multiplies and
 * bitfield instructions stay out of the loops; the AC090 interprets them
 * (about 70 ns per bit of a shift's count). OGPU_PLAIN_C=1 builds the plain
 * C forms instead, for when that changes. 68k builds pass -mnobitfield.
 *
 * Every name here starts o3_, so the file can share one unit with
 * ogpu_core.c, as the Cradle's runtime builds the core.
 */
#include "ogpu_3d.h"

typedef int o3_s32;                /* exactly 32 bits on every target the core builds for */
typedef unsigned int o3_u32;
typedef long long o3_s64;
typedef unsigned short o3_u16;

#if defined(__mc68000__) && (defined(__mc68020__) || defined(__mc68040__))
#define M68K 1                  /* 64/32 divides and unaligned word access */
#endif
#ifndef OGPU_PLAIN_C
#define OGPU_PLAIN_C 0
#endif
#if defined(M68K) && !OGPU_PLAIN_C
#define FEW_SHIFTS 1            /* keep register-count shifts out of the work */
#endif
/* Per-pixel helpers: a call per pixel costs more than the work on the JIT. */
#if defined(__GNUC__)
#define PIXEL static inline __attribute__((always_inline))
#define SPAN static __attribute__((noinline))   /* one call a row; and its code reads on its own */
#else
#define PIXEL static
#define SPAN static
#endif

/* ---- small helpers ---------------------------------------------------------------- */

/* A big-endian word: the 68k reads it as it is. */
#if defined(M68K)
typedef o3_u32 __attribute__((may_alias)) o3_u32a;
static o3_u32 o3_rd32(const ogpu_u8 *p) { return *(const o3_u32a *)p; }
static void o3_wr32(ogpu_u8 *p, o3_u32 v) { *(o3_u32a *)p = v; }
#else
static o3_u32 o3_rd32(const ogpu_u8 *p) {
    return ((o3_u32)p[0] << 24) | ((o3_u32)p[1] << 16) | ((o3_u32)p[2] << 8) | (o3_u32)p[3];
}
static void o3_wr32(ogpu_u8 *p, o3_u32 v) {
    p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
}
#endif
static int o3_hi_s(o3_u32 w) { return (int)(short)(w >> 16); }
static int o3_lo_s(o3_u32 w) { return (int)(short)(w & 0xFFFFu); }
static int o3_hi_u(o3_u32 w) { return (int)(w >> 16); }
static int o3_lo_u(o3_u32 w) { return (int)(w & 0xFFFFu); }

PIXEL int o3_clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* The low words of a and b multiplied: mulu.w, which GCC does not always
 * choose by itself. */
PIXEL o3_u32 o3_muluw(o3_u32 a, o3_u32 b) {
#if defined(M68K) && !OGPU_PLAIN_C
    __asm__("mulu.w %1,%0" : "+d"(a) : "d"(b) : "cc");
    return a;
#else
    return (a & 0xFFFFu) * (b & 0xFFFFu);
#endif
}

/* x * y / 255, rounded, for x and y in 0..255: one word multiply. */
PIXEL int o3_m255(int x, int y) {
    o3_u32 t = o3_muluw((o3_u32)x, (o3_u32)y) + 128;
    return (int)((t + (t >> 8)) >> 8);
}

/* Shifts by a count known only at run time. */
static o3_u32 o3_shr_u(o3_u32 x, int n) {
#ifdef FEW_SHIFTS
    while (n >= 8) { x >>= 8; n -= 8; }
    while (n > 0) { x >>= 1; n--; }
    return x;
#else
    return n >= 32 ? 0 : x >> n;
#endif
}
static o3_s32 o3_shr_s(o3_s32 x, int n) {
#ifdef FEW_SHIFTS
    while (n >= 8) { x >>= 8; n -= 8; }
    while (n > 0) { x >>= 1; n--; }
    return x;
#else
    return n >= 32 ? (x < 0 ? -1 : 0) : x >> n;
#endif
}

/* g * d modulo 2^32, for |d| < 65536: two word multiplies. */
PIXEL o3_u32 o3_mulw(o3_u32 g, int d) {
    o3_u32 ud = (o3_u32)(d < 0 ? -d : d), lo, hi, r;
    lo = o3_muluw(g, ud);
    hi = o3_muluw(g >> 16, ud);
    r = lo + (hi << 16);
    return d < 0 ? (o3_u32)0 - r : r;
}

static o3_s32 o3_sat32(o3_s64 q) {
    return q > 0x7FFFFFFFLL ? 0x7FFFFFFF : q < -0x7FFFFFFFLL ? -0x7FFFFFFF : (o3_s32)q;
}

/* n / d (d > 0), saturating. The 68020 to 68040 divide 64 by 32 bits in one
 * instruction; elsewhere C does it. */
static o3_s32 o3_div64(o3_s64 n, o3_s32 d) {
#ifdef M68K
    {
        o3_s32 hi = (o3_s32)(n >> 32), lo = (o3_s32)n;
        char ov;
        __asm__("divs.l %3,%1:%0\n\tsvs %2" : "+d"(lo), "+d"(hi), "=d"(ov) : "d"(d) : "cc");
        if (!ov) return lo;
    }
#endif
    return o3_sat32(n / d);
}

/* v * 2^28 / q (q > 0), saturating. */
PIXEL o3_s32 o3_div_shl28(o3_s32 v, o3_s32 q) {
#ifdef M68K
    {
        o3_s32 hi = v >> 4;
        o3_u32 lo = (o3_u32)v;
        char ov;
        lo = ((lo >> 4) | (lo << 28)) & 0xF0000000u;        /* a rotate: the low 4 bits on top */
        __asm__("divs.l %3,%1:%0\n\tsvs %2" : "+d"(lo), "+d"(hi), "=d"(ov) : "d"(q) : "cc");
        if (!ov) return (o3_s32)lo;
        return v < 0 ? -0x7FFFFFFF : 0x7FFFFFFF;
    }
#else
    return o3_sat32((o3_s64)v * 268435456 / q);
#endif
}

/* a * b / 2^28. */
static o3_s32 o3_mul_shr28(o3_s32 a, o3_s32 b) {
    o3_s64 p = (o3_s64)a * b;
    o3_u32 hi = (o3_u32)(p >> 32), lo = (o3_u32)p;
    lo = (lo << 4) | (lo >> 28);
    return (o3_s32)((hi << 4) | (lo & 15));
}

/* An IEEE754 single (as its bits) times 2^shift, as an integer, saturating
 * at +-lim. Integers only, so the core needs no FPU. */
static o3_s32 o3_f2fix(o3_u32 bits, int shift, o3_s32 lim) {
    int e = (int)((bits >> 23) & 255), s;
    o3_u32 m, r;
    if (e == 0) return 0;                                   /* zero, denormals */
    if (e == 255) return (bits >> 31) ? -lim : lim;          /* infinities, NaNs */
    m = (bits & 0x7FFFFFu) | 0x800000u;                      /* value = m * 2^(e - 150) */
    s = e - 150 + shift;
    if (s >= 0) {
        if (s > 7 || o3_shr_u((o3_u32)lim, s) < m) r = (o3_u32)lim;
        else { r = m; while (s--) r <<= 1; }
    } else if (-s > 24) {
        r = 0;
    } else {
        r = o3_shr_u(m, -s - 1);                               /* rounded: half a unit, then the last bit */
        r = (r + 1) >> 1;
        if (r > (o3_u32)lim) r = (o3_u32)lim;
    }
    return (bits >> 31) ? -(o3_s32)r : (o3_s32)r;
}

/* log2(x) in 8.8 fixed point, x > 0; the fraction from the next 8 bits. */
static int o3_log2_88(o3_s64 x) {
    unsigned long long y = (unsigned long long)x;
    o3_u32 v;
    int b = 63;
    if (!y) return 0;
    while (!(y >> 56)) { y <<= 8; b -= 8; }                 /* whole bytes, then bits */
    v = (o3_u32)(y >> 32);
    while (!(v & 0x80000000u)) { v <<= 1; b--; }
    return b * 256 + (int)((v >> 23) & 255);
}

/* e^-x for x in 16.16, as 16.16 (1.0 = 65536). Through 2^-y, y = x log2 e. */
static o3_u32 o3_exp_neg16(o3_u32 x) {
    static const unsigned short p2[17] = {      /* 2^-(i/16), 16.16 */
        65535, 62757, 60097, 57549, 55109, 52773, 50535, 48393, 46341,
        44376, 42495, 40693, 38968, 37316, 35734, 34219, 32768 };
    o3_u32 y, n, f, i, a, b, r;
    if (x > (32u << 16)) return 0;
    y = (o3_u32)(((o3_s64)x * 94548) >> 16);                      /* log2 e = 1.4427 */
    n = y >> 16; f = y & 0xFFFF;
    if (n >= 16) return 0;
    i = f >> 12; a = p2[i]; b = p2[i + 1];
    r = a - (((a - b) * (f & 0xFFF)) >> 12);
    return o3_shr_u(r, (int)n);
}

static int o3_bpp_of(int format) {
    switch (format) {
    case OGPU_FMT_CLUT8: case OGPU_FMT_INDEX8: case OGPU_FMT_A8: case OGPU_FMT_S8: return 1;
    case OGPU_FMT_RGB565: case OGPU_FMT_Z16: return 2;
    case OGPU_FMT_ARGB32: case OGPU_FMT_Z32: return 4;
    }
    return 0;
}

/* Colours as four channels: c[0] red, c[1] green, c[2] blue, c[3] alpha. */
static void o3_unpack(o3_u32 v, int *c) {
    o3_u32 hi = v >> 16, top = (v << 8) | (v >> 24);          /* a swap and a rotate */
    c[3] = (int)(top & 255); c[0] = (int)(hi & 255); c[1] = (int)((v >> 8) & 255); c[2] = (int)(v & 255);
}

/* An RGB565 pixel (high byte first) as channels, with immediate shifts only. */
PIXEL void o3_from565(const ogpu_u8 *p, int *c) {
    int hi = p[0], lo = p[1], r5 = hi >> 3, g6 = ((hi & 7) << 3) | (lo >> 5), b5 = lo & 31;
    c[0] = (r5 << 3) | (r5 >> 2);
    c[1] = (g6 << 2) | (g6 >> 4);
    c[2] = (b5 << 3) | (b5 >> 2);
    c[3] = 255;
}

/* ---- textures ---------------------------------------------------------------------- */

/* One mip level as a triangle samples it. */
struct o3_level {
    const ogpu_u8 *base;
    o3_u32 bpr;
    int w, h, wmask, hmask;             /* masks for power-of-two sizes, else 0 */
    int bpp, format, wrap_s, wrap_t;
    const ogpu_u8 *table;
    int border[4];
};

static void o3_level_of(const struct ogpu3d_tex *tx, int lv, struct o3_level *L) {
    int w = (int)o3_shr_u((o3_u32)tx->w, lv), h = (int)o3_shr_u((o3_u32)tx->h, lv);
    L->w = w < 1 ? 1 : w; L->h = h < 1 ? 1 : h;
    L->wmask = (L->w & (L->w - 1)) ? 0 : L->w - 1;
    L->hmask = (L->h & (L->h - 1)) ? 0 : L->h - 1;
    L->base = tx->level[lv]; L->bpr = (o3_u32)tx->bpr[lv];
    L->bpp = tx->bpp; L->format = tx->format;
    L->wrap_s = tx->wrap_s; L->wrap_t = tx->wrap_t;
    L->table = tx->table;
    o3_unpack(tx->border, L->border);
}

/* Coordinate c (texels) wrapped into 0..size-1; -1 = the border. */
PIXEL int o3_wrapc(int c, int size, int mask, int mode) {
    if (mode == OGPU_WRAP_REPEAT) {
        if (mask || size == 1) return c & mask;
        c %= size;
        return c < 0 ? c + size : c;
    }
    if (mode == OGPU_WRAP_BORDER && (c < 0 || c >= size)) return -1;
    return c < 0 ? 0 : c >= size ? size - 1 : c;
}

PIXEL void o3_texel(const struct o3_level *L, int x, int y, int *c) {
    const ogpu_u8 *p;
    x = o3_wrapc(x, L->w, L->wmask, L->wrap_s);
    y = o3_wrapc(y, L->h, L->hmask, L->wrap_t);
    if (x < 0 || y < 0) { c[0] = L->border[0]; c[1] = L->border[1]; c[2] = L->border[2]; c[3] = L->border[3]; return; }
    p = L->base + o3_mulw(L->bpr, y);
    switch (L->format) {
    case OGPU_FMT_ARGB32:
        p += x << 2;
        c[3] = p[0]; c[0] = p[1]; c[1] = p[2]; c[2] = p[3];
        return;
    case OGPU_FMT_RGB565:
        o3_from565(p + (x << 1), c);
        return;
    case OGPU_FMT_A8:
        c[0] = c[1] = c[2] = 255; c[3] = p[x];
        return;
    default:                                            /* INDEX8, CLUT8 */
        if (L->table) {
            const ogpu_u8 *q = L->table + (p[x] << 2);
            c[3] = q[0]; c[0] = q[1]; c[1] = q[2]; c[2] = q[3];
        } else {
            c[0] = c[1] = c[2] = p[x]; c[3] = 255;
        }
    }
}

/* The level at U, V (texels at this level, 24.8), nearest or bilinear. */
PIXEL void o3_sample(const struct o3_level *L, o3_s32 U, o3_s32 V, int bilinear, int *c) {
    if (!bilinear) { o3_texel(L, (int)(U >> 8), (int)(V >> 8), c); return; }
    {
        int t00[4], t10[4], t01[4], t11[4], k, x, y;
        o3_u32 fx, fy;
        U -= 128; V -= 128;                                 /* o3_texel centres */
        x = (int)(U >> 8); y = (int)(V >> 8);
        fx = (o3_u32)U & 255; fy = (o3_u32)V & 255;
        o3_texel(L, x, y, t00); o3_texel(L, x + 1, y, t10);
        o3_texel(L, x, y + 1, t01); o3_texel(L, x + 1, y + 1, t11);
        for (k = 0; k < 4; k++) {
            o3_u32 top = o3_muluw((o3_u32)t00[k], 256 - fx) + o3_muluw((o3_u32)t10[k], fx);
            o3_u32 bot = o3_muluw((o3_u32)t01[k], 256 - fx) + o3_muluw((o3_u32)t11[k], fx);
            top = (top + 128) >> 8; bot = (bot + 128) >> 8;
            c[k] = (int)((o3_muluw(top, 256 - fy) + o3_muluw(bot, fy) + 128) >> 8);
        }
    }
}

/* ---- the state ops ------------------------------------------------------------------ */

static void o3_build_fog(struct ogpu3d *t, o3_u32 start_bits, o3_u32 end_bits, o3_u32 density_bits) {
    o3_s32 start, end, dens;
    int i;
    if (t->fogkey[0] == (o3_u32)t->fogmode && t->fogkey[1] == start_bits && t->fogkey[2] == end_bits
        && t->fogkey[3] == density_bits) return;              /* the table is still right */
    t->fogkey[0] = (o3_u32)t->fogmode; t->fogkey[1] = start_bits; t->fogkey[2] = end_bits; t->fogkey[3] = density_bits;
    start = o3_f2fix(start_bits, 16, 0x3FFFFFFF); end = o3_f2fix(end_bits, 16, 0x3FFFFFFF);
    dens = o3_f2fix(density_bits, 16, 0x3FFFFFFF);
    for (i = 0; i < OGPU3D_FOGTAB; i++) {
        o3_s32 w = (o3_s32)((i * 65536 + (OGPU3D_FOGTAB - 1) / 2) / (OGPU3D_FOGTAB - 1));   /* 0..1, 16.16 */
        o3_s32 d = 65536 - w;                                    /* the distance from the front plane */
        o3_s32 f;                                                /* 0..255, 255 = no fog */
        switch (t->fogmode) {
        case OGPU_FOG_LINEAR:
            if (start == end) f = w >= start ? 255 : 0;
            else {
                o3_s32 num = (w - end) >> 2, den = (start - end) >> 2;   /* keeps num * 255 in range */
                if (!den) den = start > end ? 1 : -1;
                f = (o3_s32)(((o3_s64)num * 255) / den);
            }
            break;
        case OGPU_FOG_EXP:
            f = (o3_s32)((o3_exp_neg16((o3_u32)(((o3_s64)dens * d) >> 16)) * 255 + 32768) >> 16);
            break;
        case OGPU_FOG_EXP2: {
            o3_s64 x = ((o3_s64)dens * d) >> 16;
            f = x > (64 << 16) ? 0 : (o3_s32)((o3_exp_neg16((o3_u32)((x * x) >> 16)) * 255 + 32768) >> 16);
            break;
        }
        default: f = 255;
        }
        t->fogtab[i] = (ogpu_u8)o3_clamp255(f);
    }
}

static int o3_op_render3d(struct ogpu3d *t, const ogpu_u8 *a) {
    struct ogpu_core *c = t->core;
    o3_u32 w;
    t->enables = o3_rd32(a);
    t->zfunc = (int)o3_rd32(a + 4);
    w = o3_rd32(a + 8); t->afunc = (int)(w & 0xFFFF); t->aref = (int)((w >> 16) & 255);
    w = o3_rd32(a + 12); t->bsrc = (int)(w & 0xFFFF); t->bdst = (int)(w >> 16);
    t->bconst = o3_rd32(a + 16);
    t->cmask = o3_rd32(a + 20);
    t->logicop = (int)o3_rd32(a + 24);
    t->fogmode = (int)o3_rd32(a + 28);
    t->fogcolour = o3_rd32(a + 32);
    w = o3_rd32(a + 48); t->sfunc = (int)(w & 255); t->sref = (int)((w >> 8) & 255); t->smask = (int)((w >> 16) & 255);
    w = o3_rd32(a + 52); t->sfail = (int)(w & 255); t->szfail = (int)((w >> 8) & 255);
    t->szpass = (int)((w >> 16) & 255); t->swmask = (int)((w >> 24) & 255);
    t->chroma = (int)o3_rd32(a + 56);
    t->chroma_lo = o3_rd32(a + 60); t->chroma_hi = o3_rd32(a + 64);
    t->current = o3_rd32(a + 68);
    w = o3_rd32(a + 72); t->pen_table = w ? c->map(c->user, w, 32768) : 0;
    w = o3_rd32(a + 76); t->stipple = w ? c->map(c->user, w, 128) : 0;
    w = o3_rd32(a + 80); t->palette = w ? c->map(c->user, w, 1024) : 0;
    if (t->fogmode >= OGPU_FOG_LINEAR && t->fogmode <= OGPU_FOG_EXP2)
        o3_build_fog(t, o3_rd32(a + 36), o3_rd32(a + 40), o3_rd32(a + 44));
    return OGPU_OK;
}

static int o3_op_texenv(struct ogpu3d *t, const ogpu_u8 *a) {
    o3_u32 unit = o3_rd32(a), mode = o3_rd32(a + 4);
    if (unit >= OGPU_TEX_UNITS) return OGPU_ERR_UNSUPPORTED;
    if (mode < OGPU_ENV_REPLACE || mode > OGPU_ENV_OFF) return OGPU_ERR_UNSUPPORTED;
    t->env[unit] = (int)mode;
    t->env_colour[unit] = o3_rd32(a + 8);
    return OGPU_OK;
}

static int o3_op_texture(struct ogpu3d *t, const ogpu_u8 *a, long args) {
    struct ogpu_core *c = t->core;
    o3_u32 unit = o3_rd32(a), address = o3_rd32(a + 4), bpr = o3_rd32(a + 8), wh = o3_rd32(a + 12), fmt = o3_rd32(a + 16);
    o3_u32 table = o3_rd32(a + 20), filter = o3_rd32(a + 28), wrap = o3_rd32(a + 32), levels = o3_rd32(a + 40);
    struct ogpu3d_tex *tx;
    int w = o3_hi_u(wh), h = o3_lo_u(wh), i;
    if (unit >= OGPU_TEX_UNITS) return OGPU_ERR_UNSUPPORTED;
    tx = &t->tex[unit];
    tx->level[0] = 0;
    if (!address) return OGPU_OK;                           /* unbound */
    if (!levels) levels = 1;
    if (levels > OGPU_TEX_LEVELS || (long)(10 + levels) > args) return OGPU_ERR_BADLEN;
    tx->format = (int)(fmt & 255);
    tx->base = (int)((fmt >> 8) & 255);
    tx->bpp = o3_bpp_of(tx->format);
    if (tx->format != OGPU_FMT_ARGB32 && tx->format != OGPU_FMT_RGB565 && tx->format != OGPU_FMT_A8
        && tx->format != OGPU_FMT_INDEX8 && tx->format != OGPU_FMT_CLUT8)
        return OGPU_ERR_UNSUPPORTED;
    if (!w || !h || w > 4096 || h > 4096 || bpr < (o3_u32)(w * tx->bpp) || bpr > 0xFFFFu)
        return OGPU_ERR_UNSUPPORTED;
    tx->w = w; tx->h = h;
    tx->min = (int)(filter & 255); tx->mag = (int)((filter >> 8) & 255);
    tx->wrap_s = (int)(wrap & 255); tx->wrap_t = (int)((wrap >> 8) & 255);
    tx->border = o3_rd32(a + 36);
    tx->table = 0;
    if ((tx->format == OGPU_FMT_INDEX8 || tx->format == OGPU_FMT_CLUT8) && table) {
        tx->table = c->map(c->user, table, 1024);
        if (!tx->table) return OGPU_ERR_NOMAP;
    }
    for (i = 0; i < (int)levels; i++) {
        int lw = (int)o3_shr_u((o3_u32)w, i), lh = (int)o3_shr_u((o3_u32)h, i);
        o3_u32 lbpr, lad;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        lbpr = i ? (o3_u32)(lw * tx->bpp) : bpr;
        lad = i ? o3_rd32(a + 44 + (i - 1) * 4) : address;
        tx->bpr[i] = (long)lbpr;
        tx->level[i] = c->map(c->user, lad, lbpr * (o3_u32)(lh - 1) + (o3_u32)(lw * tx->bpp));
        if (!tx->level[i]) { tx->level[0] = 0; return OGPU_ERR_NOMAP; }
    }
    tx->levels = (int)levels;
    return OGPU_OK;
}

static int o3_op_depth(struct ogpu3d *t, const ogpu_u8 *a) {
    struct ogpu_core *c = t->core;
    o3_u32 kind = o3_rd32(a), address = o3_rd32(a + 4), bpr = o3_rd32(a + 8), wh = o3_rd32(a + 12), fmt = o3_rd32(a + 16);
    struct ogpu3d_buf *b = kind == OGPU_KIND_STENCIL ? &t->stencil : &t->depth;
    int w = o3_hi_u(wh), h = o3_lo_u(wh), bpp = o3_bpp_of((int)fmt);
    if (kind > OGPU_KIND_STENCIL) return OGPU_ERR_UNSUPPORTED;
    b->mem = 0;
    if (!address) return OGPU_OK;
    if (kind == OGPU_KIND_DEPTH ? (fmt != OGPU_FMT_Z16 && fmt != OGPU_FMT_Z32) : fmt != OGPU_FMT_S8)
        return OGPU_ERR_UNSUPPORTED;
    if (!w || !h || bpr < (o3_u32)(w * bpp) || bpr > 0xFFFFu) return OGPU_ERR_UNSUPPORTED;
    b->mem = c->map(c->user, address, bpr * (o3_u32)(h - 1) + (o3_u32)(w * bpp));
    if (!b->mem) return OGPU_ERR_NOMAP;
    b->bpr = (long)bpr; b->w = w; b->h = h; b->format = (int)fmt;
    return OGPU_OK;
}

/* The drawing area: the target, the clip rectangle and (for 3D) the depth
 * and stencil buffers' sizes, intersected. */
static int o3_draw_area(struct ogpu3d *t, int use_bufs, int *x0, int *y0, int *x1, int *y1) {
    struct ogpu_core *c = t->core;
    const struct ogpu_surface *s = &c->slot[c->target];
    *x0 = 0; *y0 = 0; *x1 = s->w; *y1 = s->h;
    if (c->cx1 > c->cx0) {
        if (c->cx0 > *x0) *x0 = c->cx0;
        if (c->cy0 > *y0) *y0 = c->cy0;
        if (c->cx1 < *x1) *x1 = c->cx1;
        if (c->cy1 < *y1) *y1 = c->cy1;
    }
    if (use_bufs) {
        if (t->depth.mem && (t->enables & OGPU_R3D_ZTEST)) {
            if (t->depth.w < *x1) *x1 = t->depth.w;
            if (t->depth.h < *y1) *y1 = t->depth.h;
        }
        if (t->stencil.mem && (t->enables & OGPU_R3D_STENCIL)) {
            if (t->stencil.w < *x1) *x1 = t->stencil.w;
            if (t->stencil.h < *y1) *y1 = t->stencil.h;
        }
    }
    return *x1 > *x0 && *y1 > *y0;
}

static int o3_op_clear3d(struct ogpu3d *t, const ogpu_u8 *a) {
    struct ogpu_core *c = t->core;
    o3_u32 kind = o3_rd32(a), xy = o3_rd32(a + 4), wh = o3_rd32(a + 8), v = o3_rd32(a + 12);
    struct ogpu3d_buf *b = kind == OGPU_KIND_STENCIL ? &t->stencil : &t->depth;
    int x = o3_hi_s(xy), y = o3_lo_s(xy), w = o3_hi_u(wh), h = o3_lo_u(wh), i, j, cx0 = 0, cy0 = 0, cx1, cy1;
    ogpu_u8 b0 = (ogpu_u8)(v >> 24), b1 = (ogpu_u8)(v >> 16), b3 = (ogpu_u8)v;
    if (kind & OGPU_KIND_READBACK) return OGPU_OK;           /* the CPU draws in memory already */
    if (kind > OGPU_KIND_STENCIL) return OGPU_ERR_UNSUPPORTED;
    if (!b->mem) return OGPU_ERR_NOSURFACE;
    cx1 = b->w; cy1 = b->h;
    if (c->cx1 > c->cx0) {                                  /* clears keep inside the clip, as scissored ones do */
        if (c->cx0 > cx0) cx0 = c->cx0;
        if (c->cy0 > cy0) cy0 = c->cy0;
        if (c->cx1 < cx1) cx1 = c->cx1;
        if (c->cy1 < cy1) cy1 = c->cy1;
    }
    if (x < cx0) { w -= cx0 - x; x = cx0; }
    if (y < cy0) { h -= cy0 - y; y = cy0; }
    if (x + w > cx1) w = cx1 - x;
    if (y + h > cy1) h = cy1 - y;
    if (w <= 0 || h <= 0) return OGPU_OK;
    for (j = 0; j < h; j++) {
        ogpu_u8 *p = b->mem + o3_mulw((o3_u32)b->bpr, y + j);
        if (b->format == OGPU_FMT_Z32) {
            p += x << 2;
            for (i = 0; i < w; i++, p += 4) o3_wr32(p, v);
        } else if (b->format == OGPU_FMT_Z16) {
            p += x << 1;
            for (i = 0; i < w; i++, p += 2) { p[0] = b0; p[1] = b1; }
        } else {
            p += x;
            for (i = 0; i < w; i++) p[i] = b3;
        }
    }
    return OGPU_OK;
}

/* ---- per-draw settings ---------------------------------------------------------------- */

/* What one TRIANGLES command draws with: RENDER3D's state for the 3D form,
 * SDL's flags turned into the same terms for the 9-word form. */
struct o3_pipe {
    const struct ogpu_surface *dst;     /* the target */
    o3_u32 en;                             /* OGPU_R3D_ */
    const struct ogpu3d_tex *tx;        /* 0 = untextured */
    int env, base;
    int envc[4];
    int bsrc, bdst;
    int fmt;                            /* the target's */
    int has_z, has_w, has_spec;
    int cur[4], fog[4], kc[4];          /* current, fog and blend colours as channels */
    int mask_all;                       /* the colour mask lets everything through */
    o3_u32 wmask;                          /* else: the raw bits it lets through */
};

/* ---- the fragment pipeline --------------------------------------------------------------- */

PIXEL int o3_cmp(int func, o3_u32 a, o3_u32 b) {     /* W3D_Z_/W3D_A_ numbering: is a (incoming) OP b? */
    switch (func) {
    case 1: return 0;                       /* NEVER */
    case 2: return a < b;                   /* LESS */
    case 3: return a >= b;                  /* GEQUAL */
    case 4: return a <= b;                  /* LEQUAL */
    case 5: return a > b;                   /* GREATER */
    case 6: return a != b;                  /* NOTEQUAL */
    case 7: return a == b;                  /* EQUAL */
    }
    return 1;                               /* ALWAYS */
}

PIXEL int o3_stencil_test(const struct ogpu3d *t, int s) {
    int r = t->sref & t->smask, v = s & t->smask;
    switch (t->sfunc) {                     /* W3D_ST_: draw if ref OP stencil */
    case 1: return 0;
    case 2: return 1;
    case 3: return r < v;
    case 4: return r <= v;
    case 5: return r == v;
    case 6: return r >= v;
    case 7: return r > v;
    case 8: return r != v;
    }
    return 1;
}

PIXEL int o3_stencil_op(const struct ogpu3d *t, int op, int s) {
    int n = s;
    switch (op) {
    case 2: n = 0; break;                                   /* ZERO */
    case 3: n = t->sref; break;                             /* REPLACE */
    case 4: n = s < 255 ? s + 1 : 255; break;               /* INCR */
    case 5: n = s > 0 ? s - 1 : 0; break;                   /* DECR */
    case 6: n = ~s & 255; break;                            /* INVERT */
    case 7: n = (s + 1) & 255; break;                       /* INCR_WRAP */
    case 8: n = (s - 1) & 255; break;                       /* DECR_WRAP */
    }
    return (s & ~t->swmask) | (n & t->swmask);
}

/* A blend o3_factor (W3D_ZERO..W3D_ONE_MINUS_CONSTANT_ALPHA) as four 0..255 values. */
static void o3_factor(int f, const int *s, const int *d, const int *k, int *o) {
    int i, v;
    switch (f) {
    case 1: o[0] = o[1] = o[2] = o[3] = 0; return;
    case 3: for (i = 0; i < 4; i++) o[i] = s[i]; return;
    case 4: for (i = 0; i < 4; i++) o[i] = d[i]; return;
    case 5: for (i = 0; i < 4; i++) o[i] = 255 - s[i]; return;
    case 6: for (i = 0; i < 4; i++) o[i] = 255 - d[i]; return;
    case 7: v = s[3]; break;
    case 8: v = 255 - s[3]; break;
    case 9: v = d[3]; break;
    case 10: v = 255 - d[3]; break;
    case 11: v = s[3] < 255 - d[3] ? s[3] : 255 - d[3]; o[0] = o[1] = o[2] = v; o[3] = 255; return;
    case 12: for (i = 0; i < 4; i++) o[i] = k[i]; return;
    case 13: for (i = 0; i < 4; i++) o[i] = 255 - k[i]; return;
    case 14: v = k[3]; break;
    case 15: v = 255 - k[3]; break;
    default: v = 255;                                       /* ONE */
    }
    o[0] = o[1] = o[2] = o[3] = v;
}

static o3_u32 o3_logic(int op, o3_u32 s, o3_u32 d) {
    switch (op) {
    case 1: return 0;
    case 2: return s & d;
    case 3: return s & ~d;
    case 5: return ~s & d;
    case 6: return d;
    case 7: return s ^ d;
    case 8: return s | d;
    case 9: return ~(s | d);
    case 10: return ~(s ^ d);
    case 11: return ~d;
    case 12: return s | ~d;
    case 13: return ~s;
    case 14: return ~s | d;
    case 15: return ~(s & d);
    case 16: return 0xFFFFFFFFu;
    }
    return s;                                               /* COPY */
}

/* The target pixel at p as channels. */
PIXEL void o3_read_dst(const struct ogpu3d *t, const ogpu_u8 *p, int fmt, int *c) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: c[3] = p[0]; c[0] = p[1]; c[1] = p[2]; c[2] = p[3]; return;
    case OGPU_FMT_RGB565: o3_from565(p, c); return;
    }
    if (t->palette) { const ogpu_u8 *q = t->palette + (p[0] << 2); c[3] = q[0]; c[0] = q[1]; c[1] = q[2]; c[2] = q[3]; }
    else { c[0] = c[1] = c[2] = p[0]; c[3] = 255; }
}

/* Channels as the target's raw pixel (for o3_logic ops and the colour mask). */
static o3_u32 o3_to_raw(const struct ogpu3d *t, int fmt, const int *c) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: return ((o3_u32)c[3] << 24) | ((o3_u32)c[0] << 16) | ((o3_u32)c[1] << 8) | (o3_u32)c[2];
    case OGPU_FMT_RGB565:
        return ((o3_u32)((c[0] & 0xF8) | (c[1] >> 5)) << 8) | (o3_u32)(((c[1] & 0x1C) << 3) | (c[2] >> 3));
    }
    if (t->pen_table) return t->pen_table[((c[0] & 0xF8) << 7) | ((c[1] & 0xF8) << 2) | (c[2] >> 3)];
    return (o3_u32)((c[0] * 77 + c[1] * 151 + c[2] * 28) >> 8);   /* no table: a grey ramp */
}

PIXEL o3_u32 o3_raw_dst(const ogpu_u8 *p, int fmt) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: return o3_rd32(p);
    case OGPU_FMT_RGB565: return ((o3_u32)p[0] << 8) | p[1];
    }
    return p[0];
}

PIXEL void o3_write_raw(ogpu_u8 *p, int fmt, o3_u32 v) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: o3_wr32(p, v); return;
    case OGPU_FMT_RGB565: p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; return;
    }
    p[0] = (ogpu_u8)v;
}

/* Channels straight to the target (no mask, no o3_logic op): byte stores. */
PIXEL void o3_write_px(const struct ogpu3d *t, ogpu_u8 *p, int fmt, const int *c) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: p[0] = (ogpu_u8)c[3]; p[1] = (ogpu_u8)c[0]; p[2] = (ogpu_u8)c[1]; p[3] = (ogpu_u8)c[2]; return;
    case OGPU_FMT_RGB565:
        p[0] = (ogpu_u8)((c[0] & 0xF8) | (c[1] >> 5));
        p[1] = (ogpu_u8)(((c[1] & 0x1C) << 3) | (c[2] >> 3));
        return;
    }
    p[0] = (ogpu_u8)o3_to_raw(t, fmt, c);
}

/* Bits of the target's raw pixel that the colour mask lets through. */
static o3_u32 o3_write_mask(const struct ogpu3d *t, int fmt) {
    o3_u32 m = t->cmask, w = 0;
    if (fmt == OGPU_FMT_CLUT8) return (m >> 8) & 255;
    if (fmt == OGPU_FMT_RGB565) {
        if (m & 1) w |= 0xF800u;
        if (m & 2) w |= 0x07E0u;
        if (m & 4) w |= 0x001Fu;
        return w;
    }
    if (m & 1) w |= 0x00FF0000u;
    if (m & 2) w |= 0x0000FF00u;
    if (m & 4) w |= 0x000000FFu;
    if (m & 8) w |= 0xFF000000u;
    return w;
}

/* The texture o3_environment: fragment f[] and o3_texel T[] into f[]. */
PIXEL void o3_environment(int mode, int base, int *T, const int *cc, int *f) {
    int has_c = 1, has_a = 1;
    switch (base) {
    case OGPU_TEXBASE_RGB: has_a = 0; break;
    case OGPU_TEXBASE_ALPHA: has_c = 0; break;
    case OGPU_TEXBASE_LUMINANCE: T[1] = T[2] = T[0]; has_a = 0; break;
    case OGPU_TEXBASE_LUM_ALPHA: T[1] = T[2] = T[0]; break;
    case OGPU_TEXBASE_INTENSITY: T[1] = T[2] = T[3] = T[0]; break;
    }
    switch (mode) {
    case OGPU_ENV_REPLACE:
        if (has_c) { f[0] = T[0]; f[1] = T[1]; f[2] = T[2]; }
        if (has_a) f[3] = T[3];
        break;
    case OGPU_ENV_DECAL:                                    /* RGB and RGBA only; others keep the fragment */
        if (base == OGPU_TEXBASE_RGB) { f[0] = T[0]; f[1] = T[1]; f[2] = T[2]; }
        else if (base == OGPU_TEXBASE_RGBA) {
            f[0] = o3_m255(f[0], 255 - T[3]) + o3_m255(T[0], T[3]);
            f[1] = o3_m255(f[1], 255 - T[3]) + o3_m255(T[1], T[3]);
            f[2] = o3_m255(f[2], 255 - T[3]) + o3_m255(T[2], T[3]);
        }
        break;
    case OGPU_ENV_MODULATE:
        if (has_c) { f[0] = o3_m255(f[0], T[0]); f[1] = o3_m255(f[1], T[1]); f[2] = o3_m255(f[2], T[2]); }
        if (has_a) f[3] = o3_m255(f[3], T[3]);
        break;
    case OGPU_ENV_BLEND:
        if (has_c) {
            f[0] = o3_m255(f[0], 255 - T[0]) + o3_m255(cc[0], T[0]);
            f[1] = o3_m255(f[1], 255 - T[1]) + o3_m255(cc[1], T[1]);
            f[2] = o3_m255(f[2], 255 - T[2]) + o3_m255(cc[2], T[2]);
        }
        if (base == OGPU_TEXBASE_INTENSITY) f[3] = o3_m255(f[3], 255 - T[3]) + o3_m255(cc[3], T[3]);
        else if (has_a) f[3] = o3_m255(f[3], T[3]);
        break;
    case OGPU_ENV_ADD:
        if (has_c) { f[0] = o3_clamp255(f[0] + T[0]); f[1] = o3_clamp255(f[1] + T[1]); f[2] = o3_clamp255(f[2] + T[2]); }
        if (has_a) f[3] = o3_m255(f[3], T[3]);
        break;
    case OGPU_ENV_SUB:
        if (has_c) { f[0] = o3_clamp255(f[0] - T[0]); f[1] = o3_clamp255(f[1] - T[1]); f[2] = o3_clamp255(f[2] - T[2]); }
        if (has_a) f[3] = o3_m255(f[3], T[3]);
        break;
    }
}

/* ---- triangle setup ------------------------------------------------------------------- */

/* Values interpolated across a triangle. */
enum { AT_R, AT_G, AT_B, AT_A, AT_SR, AT_SG, AT_SB, AT_F, AT_Z, AT_FI, AT_U, AT_V, AT_Q, AT_N };

/* All values stay within +-2^30, so a difference of two fits 32 bits. */
struct o3_vtx {
    o3_s32 x, y;                           /* 28.4 */
    o3_s32 at[AT_N];                       /* colours 8.16; Z 0..2^30; fog index (w * 1024) 16.16 */
    o3_s32 un, vn;                          /* 16.16, 0..1 over the texture */
    o3_u32 wbits;
};

struct o3_edge {
    o3_s32 x, r, dy, q, rs;                /* exactly x + r/dy; per row x += q, r += rs */
};

/* floor(n / d) and the remainder 0..d-1 (d > 0). */
static void o3_fdivmod(o3_s64 n, o3_s32 d, o3_s32 *q, o3_s32 *r) {
    o3_s64 qq, rr;
    if (n >= -0x7FFFFFFFLL && n <= 0x7FFFFFFFLL) {         /* the usual case, a 32-bit divide */
        o3_s32 q32 = (o3_s32)n / d, r32 = (o3_s32)n - q32 * d;
        if (r32 < 0) { q32--; r32 += d; }
        *q = q32; *r = r32;
        return;
    }
    qq = n / d; rr = n - qq * d;
    if (rr < 0) { qq--; rr += d; }
    *q = (o3_s32)qq; *r = (o3_s32)rr;
}

static void o3_edge_init(struct o3_edge *e, const struct o3_vtx *a, const struct o3_vtx *b, int py) {
    o3_s32 dx = b->x - a->x, t = py * 16 + 8 - a->y;
    e->dy = b->y - a->y;
    if (e->dy <= 0) { e->x = a->x; e->r = 0; e->q = 0; e->rs = 0; e->dy = 1; return; }
    o3_fdivmod((o3_s64)t * dx, e->dy, &e->x, &e->r);
    e->x += a->x;
    o3_fdivmod((o3_s64)(dx * 16), e->dy, &e->q, &e->rs);
}

/* The first pixel whose centre is at or right of the edge: left edges start
 * there (inclusive) and right edges end there (exclusive). */
static int o3_edge_px(const struct o3_edge *e) {
    o3_s32 v = e->x + (e->r > 0) - 8;
    return (int)((v + 15) >> 4);
}

static void o3_edge_step(struct o3_edge *e) {
    e->x += e->q;
    e->r += e->rs;
    if (e->r >= e->dy) { e->r -= e->dy; e->x++; }
}

struct o3_tri {
    o3_s32 gx[AT_N], gy[AT_N], row[AT_N];  /* steps across and down; value at column rx of the current row */
    int rx;
    int nact, act[AT_N];                /* the values this triangle carries */
    int persp, bilinear, lfrac;         /* lfrac: blend with the next level, 0..255 */
    struct o3_level L0, L1;
};

/* ---- spans ------------------------------------------------------------------------------- */

SPAN void o3_span(struct ogpu3d *t, const struct o3_pipe *pp, const struct o3_tri *tr, int py, int x0, int x1) {
    const struct ogpu_surface *dst = pp->dst;
    o3_s32 v[AT_N];
    int i, k, fmt = pp->fmt, bpp = fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 1, seg = 0;
    o3_u32 en = pp->en;
    ogpu_u8 *dp = dst->pixels + o3_mulw((o3_u32)dst->bpr, py) + (bpp == 4 ? x0 << 2 : bpp == 2 ? x0 << 1 : x0);
    ogpu_u8 *zp = 0, *sp = 0;
    const ogpu_u8 *stip = 0;
    o3_s32 U = 0, V = 0, dU = 0, dV = 0;
    int z32 = 0, gouraud = (en & OGPU_R3D_GOURAUD) != 0, tex = pp->tx != 0, persp = tr->persp;
    int spec = (en & OGPU_R3D_SPECULAR) && pp->has_spec;
    int fog = (en & OGPU_R3D_FOG) ? t->fogmode : 0;
    int atest = (en & OGPU_R3D_ALPHATEST) != 0, chroma = (en & OGPU_R3D_CHROMA) && t->chroma > 1;
    int logicop = (en & OGPU_R3D_LOGICOP) && t->logicop != 4;
    int blend = (en & OGPU_R3D_BLEND) && !(pp->bsrc == 2 && pp->bdst == 1);
    int zwrite = (en & OGPU_R3D_ZWRITE) != 0;
    int dx = x0 - tr->rx;
    for (k = 0; k < AT_N; k++) v[k] = 0;                    /* values a triangle does not carry read as 0 */
    for (k = 0; k < tr->nact; k++) {
        int a = tr->act[k];
        v[a] = (o3_s32)((o3_u32)tr->row[a] + o3_mulw((o3_u32)tr->gx[a], dx));
    }
    if (!pp->has_w) fog = fog == OGPU_FOG_VERTEX ? fog : 0;
    if (fog == OGPU_FOG_VERTEX && !pp->has_spec) fog = 0;
    if ((en & OGPU_R3D_ZTEST) && t->depth.mem) {
        z32 = t->depth.format == OGPU_FMT_Z32;
        zp = t->depth.mem + o3_mulw((o3_u32)t->depth.bpr, py) + (z32 ? x0 << 2 : x0 << 1);
    }
    if ((en & OGPU_R3D_STENCIL) && t->stencil.mem) sp = t->stencil.mem + o3_mulw((o3_u32)t->stencil.bpr, py) + x0;
    if ((en & OGPU_R3D_STIPPLE) && t->stipple) stip = t->stipple + ((py & 31) << 2);

    for (i = x0; i < x1; i++, dp += bpp) {
        int f[4], sv = 0;
        o3_u32 zval = 0;
        /* texture coordinates: exact every 8 pixels under perspective */
        if (tex) {
            if (persp) {
                if (!seg) {
                    o3_s32 q0 = v[AT_Q] > 0 ? v[AT_Q] : 1, q1, U1, V1;
                    int n = x1 - i < 8 ? x1 - i : 8;
                    o3_s32 su1 = (o3_s32)((o3_u32)v[AT_U] + ((o3_u32)tr->gx[AT_U] << 3));
                    o3_s32 sv1 = (o3_s32)((o3_u32)v[AT_V] + ((o3_u32)tr->gx[AT_V] << 3));
                    q1 = (o3_s32)((o3_u32)v[AT_Q] + ((o3_u32)tr->gx[AT_Q] << 3));
                    if (q1 <= 0) q1 = 1;
                    U = o3_div_shl28(v[AT_U], q0);
                    V = o3_div_shl28(v[AT_V], q0);
                    U1 = o3_div_shl28(su1, q1);
                    V1 = o3_div_shl28(sv1, q1);
                    dU = (U1 - U) >> 3;                     /* across 8 pixels, even when fewer are left */
                    dV = (V1 - V) >> 3;
                    seg = n;
                }
                seg--;
            } else {
                U = v[AT_U]; V = v[AT_V];
            }
        }
        if (stip && !((stip[(i & 31) >> 3] >> (7 - (i & 7))) & 1)) goto next;
        /* colour */
        if (gouraud) {
            f[0] = o3_clamp255((int)(v[AT_R] >> 16)); f[1] = o3_clamp255((int)(v[AT_G] >> 16));
            f[2] = o3_clamp255((int)(v[AT_B] >> 16)); f[3] = o3_clamp255((int)(v[AT_A] >> 16));
        } else {
            f[0] = pp->cur[0]; f[1] = pp->cur[1]; f[2] = pp->cur[2]; f[3] = pp->cur[3];
        }
        if (tex) {
            int T[4];
            o3_sample(&tr->L0, U, V, tr->bilinear, T);
            if (tr->lfrac) {
                int T1[4], j;
                o3_sample(&tr->L1, U >> 1, V >> 1, tr->bilinear, T1);
                for (j = 0; j < 4; j++) T[j] += ((short)(T1[j] - T[j]) * (short)tr->lfrac) >> 8;
            }
            if (chroma) {
                int lo[4], hi[4], inside;
                o3_unpack(t->chroma_lo, lo); o3_unpack(t->chroma_hi, hi);
                inside = T[0] >= lo[0] && T[0] <= hi[0] && T[1] >= lo[1] && T[1] <= hi[1] && T[2] >= lo[2] && T[2] <= hi[2];
                if (inside != (t->chroma == 2)) goto next;  /* INCLUSIVE keeps the range, EXCLUSIVE drops it */
            }
            o3_environment(pp->env, pp->base, T, pp->envc, f);
        }
        if (spec) {
            f[0] = o3_clamp255(f[0] + (int)(v[AT_SR] >> 16));
            f[1] = o3_clamp255(f[1] + (int)(v[AT_SG] >> 16));
            f[2] = o3_clamp255(f[2] + (int)(v[AT_SB] >> 16));
        }
        if (fog) {
            int ff;
            if (fog == OGPU_FOG_VERTEX) ff = o3_clamp255((int)(v[AT_F] >> 16));
            else {
                o3_s32 idx = v[AT_FI] >> 16;
                ff = t->fogtab[idx < 0 ? 0 : idx >= OGPU3D_FOGTAB ? OGPU3D_FOGTAB - 1 : idx];
            }
            if (ff < 255) {
                f[0] = o3_m255(f[0], ff) + o3_m255(pp->fog[0], 255 - ff);
                f[1] = o3_m255(f[1], ff) + o3_m255(pp->fog[1], 255 - ff);
                f[2] = o3_m255(f[2], ff) + o3_m255(pp->fog[2], 255 - ff);
            }
        }
        if (atest && !o3_cmp(t->afunc, (o3_u32)f[3], (o3_u32)t->aref)) goto next;
        if (sp) {
            sv = sp[i - x0];
            if (!o3_stencil_test(t, sv)) { sp[i - x0] = (ogpu_u8)o3_stencil_op(t, t->sfail, sv); goto next; }
        }
        if (zp) {
            o3_s32 z = v[AT_Z];
            o3_u32 zu = z < 0 ? 0 : z > 0x3FFFFFFF ? 0x3FFFFFFFu : (o3_u32)z;   /* 0..2^30 - 1 */
            int pass;
            zu <<= 2;
            if (z32) {
                ogpu_u8 *q = zp + ((i - x0) << 2);
                zval = zu; pass = o3_cmp(t->zfunc, zval, o3_rd32(q));
                if (pass && zwrite) o3_wr32(q, zval);
            } else {
                ogpu_u8 *q = zp + ((i - x0) << 1);
                zval = zu >> 16; pass = o3_cmp(t->zfunc, zval, ((o3_u32)q[0] << 8) | q[1]);
                if (pass && zwrite) { q[0] = (ogpu_u8)(zval >> 8); q[1] = (ogpu_u8)zval; }
            }
            if (sp) sp[i - x0] = (ogpu_u8)o3_stencil_op(t, pass ? t->szpass : t->szfail, sv);
            if (!pass) goto next;
        } else if (sp) sp[i - x0] = (ogpu_u8)o3_stencil_op(t, t->szpass, sv);
        /* blend or o3_logic op, then the colour mask */
        if (logicop) {
            o3_u32 old = o3_raw_dst(dp, fmt), out = o3_logic(t->logicop, o3_to_raw(t, fmt, f), old);
            o3_write_raw(dp, fmt, (out & pp->wmask) | (old & ~pp->wmask));
        } else {
            if (blend) {
                int dc[4], j;
                o3_read_dst(t, dp, fmt, dc);
                if (fmt != OGPU_FMT_ARGB32) dc[3] = 255;
                if (pp->bsrc == 7 && pp->bdst == 8) {           /* the usual: SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
                    int sa = f[3], da = 255 - f[3];
                    for (j = 0; j < 4; j++) f[j] = o3_m255(f[j], sa) + o3_m255(dc[j], da);
                } else if (pp->bsrc == 2 && pp->bdst == 2) {    /* ONE, ONE */
                    for (j = 0; j < 4; j++) f[j] = o3_clamp255(f[j] + dc[j]);
                } else {
                    int fs[4], fd[4];
                    o3_factor(pp->bsrc, f, dc, pp->kc, fs);
                    o3_factor(pp->bdst, f, dc, pp->kc, fd);
                    for (j = 0; j < 4; j++) f[j] = o3_clamp255(o3_m255(f[j], fs[j]) + o3_m255(dc[j], fd[j]));
                }
            }
            if (pp->mask_all) o3_write_px(t, dp, fmt, f);
            else {
                o3_u32 old = o3_raw_dst(dp, fmt);
                o3_write_raw(dp, fmt, (o3_to_raw(t, fmt, f) & pp->wmask) | (old & ~pp->wmask));
            }
        }
        t->pixels++;
    next:
        for (k = 0; k < tr->nact; k++) {
            int a = tr->act[k];
            v[a] = (o3_s32)((o3_u32)v[a] + (o3_u32)tr->gx[a]);
        }
        if (persp) { U += dU; V += dV; }
    }
}

/* The common case, kept in locals and with the Z test first: flat or
 * Gouraud colour, an ARGB32 texture (the Warp3D library converts every
 * texture to it) from one level, repeating at a power-of-two size or
 * clamped, REPLACE or MODULATE, fog, the alpha test, Z16 or Z32, and no
 * blend, alpha blending or adding. o3_draw_triangle picks it when the state
 * allows (o3_fast_ok); the general o3_span above does the rest. The pictures are
 * the same, pixel for pixel: tests/test_3d.c draws every scene both ways. */
SPAN void o3_span_fast(struct ogpu3d *t, const struct o3_pipe *pp, const struct o3_tri *tr, int py, int x0, int x1) {
    const struct ogpu_surface *dst = pp->dst;
    o3_u32 en = pp->en;
    int fmt = pp->fmt, n = x1 - x0, i, dx = x0 - tr->rx;
    int gouraud = (en & OGPU_R3D_GOURAUD) != 0, tex = pp->tx != 0, persp = tr->persp, bil = tr->bilinear;
    int fogm = (en & OGPU_R3D_FOG) ? t->fogmode : 0;
    int blend = !(en & OGPU_R3D_BLEND) || (pp->bsrc == 2 && pp->bdst == 1) ? 0 : pp->bsrc == 7 ? 1 : 2;
    int atest = (en & OGPU_R3D_ALPHATEST) != 0, afunc = t->afunc;
    o3_u32 aref = (o3_u32)t->aref;
    int ztest = (en & OGPU_R3D_ZTEST) && t->depth.mem, z32 = t->depth.format == OGPU_FMT_Z32;
    int zwrite = (en & OGPU_R3D_ZWRITE) != 0, zfunc = t->zfunc;
    int mod = pp->env == OGPU_ENV_MODULATE, rgbonly = pp->base == OGPU_TEXBASE_RGB;
    int cr = pp->cur[0], cg = pp->cur[1], cb = pp->cur[2], ca = pp->cur[3];
    int fr = pp->fog[0], fgc = pp->fog[1], fbc = pp->fog[2];
    const ogpu_u8 *fogtab = t->fogtab, *tb = 0;
    o3_u32 tbpr = 0;
    int wm = 0, hm = 0, wmax = 0, hmax = 0, rep_s = 0, rep_t = 0, seg = 0;
    o3_s32 r = 0, g = 0, b = 0, a = 0, dr = 0, dg = 0, db = 0, da = 0, z = 0, dz = 0, fv = 0, dfv = 0;
    o3_s32 su = 0, sv = 0, q = 0, dsu = 0, dsv = 0, dq = 0, U = 0, V = 0, dU = 0, dV = 0;
    long count = 0;
    ogpu_u8 *dp, *zp = 0;
#define START(k) (o3_s32)((o3_u32)tr->row[k] + o3_mulw((o3_u32)tr->gx[k], dx))
    dp = dst->pixels + o3_mulw((o3_u32)dst->bpr, py) + (fmt == OGPU_FMT_ARGB32 ? x0 << 2 : x0 << 1);
    if (gouraud) {
        r = START(AT_R); g = START(AT_G); b = START(AT_B); a = START(AT_A);
        dr = tr->gx[AT_R]; dg = tr->gx[AT_G]; db = tr->gx[AT_B]; da = tr->gx[AT_A];
    }
    if (ztest) {
        if (tr->nact && (pp->has_z)) { z = START(AT_Z); dz = tr->gx[AT_Z]; }
        zp = t->depth.mem + o3_mulw((o3_u32)t->depth.bpr, py) + (z32 ? x0 << 2 : x0 << 1);
    }
    if (fogm == OGPU_FOG_VERTEX) {
        if (pp->has_spec) { fv = START(AT_F); dfv = tr->gx[AT_F]; } else fogm = 0;
    } else if (fogm) {
        if (pp->has_w) { fv = START(AT_FI); dfv = tr->gx[AT_FI]; } else fogm = 0;
    }
    if (tex) {
        const struct o3_level *L = &tr->L0;
        tb = L->base; tbpr = L->bpr;
        wm = L->wmask; hm = L->hmask; wmax = L->w - 1; hmax = L->h - 1;
        rep_s = L->wrap_s == OGPU_WRAP_REPEAT; rep_t = L->wrap_t == OGPU_WRAP_REPEAT;
        if (persp) {
            su = START(AT_U); sv = START(AT_V); q = START(AT_Q);
            dsu = tr->gx[AT_U]; dsv = tr->gx[AT_V]; dq = tr->gx[AT_Q];
        } else {
            U = START(AT_U); V = START(AT_V); dU = tr->gx[AT_U]; dV = tr->gx[AT_V];
        }
    }
#undef START

#define FETCH(xx, yy, o) do {                                                       \
        int x_ = (xx), y_ = (yy);                                                   \
        const ogpu_u8 *p_;                                                          \
        if (rep_s) x_ &= wm; else x_ = x_ < 0 ? 0 : x_ > wmax ? wmax : x_;          \
        if (rep_t) y_ &= hm; else y_ = y_ < 0 ? 0 : y_ > hmax ? hmax : y_;          \
        p_ = tb + o3_muluw((o3_u32)y_, tbpr) + (x_ << 2);                                 \
        o[3] = p_[0]; o[0] = p_[1]; o[1] = p_[2]; o[2] = p_[3];                     \
    } while (0)

    for (i = 0; i < n; i++, dp += fmt == OGPU_FMT_ARGB32 ? 4 : 2) {
        int f0, f1, f2, f3;
        o3_u32 zval = 0;
        if (persp && !seg) {                    /* exact texture coordinates every 8 pixels */
            o3_s32 q0 = q > 0 ? q : 1, q1 = (o3_s32)((o3_u32)q + ((o3_u32)dq << 3));
            if (q1 <= 0) q1 = 1;
            U = o3_div_shl28(su, q0); V = o3_div_shl28(sv, q0);
            dU = (o3_div_shl28((o3_s32)((o3_u32)su + ((o3_u32)dsu << 3)), q1) - U) >> 3;
            dV = (o3_div_shl28((o3_s32)((o3_u32)sv + ((o3_u32)dsv << 3)), q1) - V) >> 3;
            seg = 8;
        }
        if (ztest) {
            o3_u32 zu = z < 0 ? 0 : z > 0x3FFFFFFF ? 0x3FFFFFFFu : (o3_u32)z, old;
            zu <<= 2;
            if (z32) { zval = zu; old = o3_rd32(zp + (i << 2)); }
            else { zval = zu >> 16; old = ((o3_u32)zp[i << 1] << 8) | zp[(i << 1) + 1]; }
            if (!o3_cmp(zfunc, zval, old)) goto next;
        }
        if (gouraud) {
            f0 = o3_clamp255((int)(r >> 16)); f1 = o3_clamp255((int)(g >> 16));
            f2 = o3_clamp255((int)(b >> 16)); f3 = o3_clamp255((int)(a >> 16));
        } else { f0 = cr; f1 = cg; f2 = cb; f3 = ca; }
        if (tex) {
            int T[4];
            if (!bil) FETCH((int)(U >> 8), (int)(V >> 8), T);
            else {
                int t00[4], t10[4], t01[4], t11[4], k, x, y;
                o3_u32 fx, fy;
                o3_s32 Us = U - 128, Vs = V - 128;
                x = (int)(Us >> 8); y = (int)(Vs >> 8);
                fx = (o3_u32)Us & 255; fy = (o3_u32)Vs & 255;
                FETCH(x, y, t00); FETCH(x + 1, y, t10); FETCH(x, y + 1, t01); FETCH(x + 1, y + 1, t11);
                for (k = 0; k < 4; k++) {
                    o3_u32 top = (o3_muluw((o3_u32)t00[k], 256 - fx) + o3_muluw((o3_u32)t10[k], fx) + 128) >> 8;
                    o3_u32 bot = (o3_muluw((o3_u32)t01[k], 256 - fx) + o3_muluw((o3_u32)t11[k], fx) + 128) >> 8;
                    T[k] = (int)((o3_muluw(top, 256 - fy) + o3_muluw(bot, fy) + 128) >> 8);
                }
            }
            if (mod) {
                f0 = o3_m255(f0, T[0]); f1 = o3_m255(f1, T[1]); f2 = o3_m255(f2, T[2]);
                if (!rgbonly) f3 = o3_m255(f3, T[3]);
            } else {
                f0 = T[0]; f1 = T[1]; f2 = T[2];
                if (!rgbonly) f3 = T[3];
            }
        }
        if (fogm) {
            int ff;
            if (fogm == OGPU_FOG_VERTEX) ff = o3_clamp255((int)(fv >> 16));
            else {
                o3_s32 idx = fv >> 16;
                ff = fogtab[idx < 0 ? 0 : idx >= OGPU3D_FOGTAB ? OGPU3D_FOGTAB - 1 : idx];
            }
            if (ff < 255) {
                int nf = 255 - ff;
                f0 = o3_m255(f0, ff) + o3_m255(fr, nf); f1 = o3_m255(f1, ff) + o3_m255(fgc, nf); f2 = o3_m255(f2, ff) + o3_m255(fbc, nf);
            }
        }
        if (atest && !o3_cmp(afunc, (o3_u32)f3, aref)) goto next;
        if (ztest && zwrite) {
            if (z32) o3_wr32(zp + (i << 2), zval);
            else { zp[i << 1] = (ogpu_u8)(zval >> 8); zp[(i << 1) + 1] = (ogpu_u8)zval; }
        }
        if (blend) {
            int d[4];
            if (fmt == OGPU_FMT_ARGB32) { d[3] = dp[0]; d[0] = dp[1]; d[1] = dp[2]; d[2] = dp[3]; }
            else o3_from565(dp, d);
            if (blend == 1) {
                int sa = f3, na = 255 - f3;
                f0 = o3_m255(f0, sa) + o3_m255(d[0], na); f1 = o3_m255(f1, sa) + o3_m255(d[1], na);
                f2 = o3_m255(f2, sa) + o3_m255(d[2], na); f3 = o3_m255(f3, sa) + o3_m255(d[3], na);
            } else {
                f0 = o3_clamp255(f0 + d[0]); f1 = o3_clamp255(f1 + d[1]); f2 = o3_clamp255(f2 + d[2]); f3 = o3_clamp255(f3 + d[3]);
            }
        }
        if (fmt == OGPU_FMT_ARGB32) { dp[0] = (ogpu_u8)f3; dp[1] = (ogpu_u8)f0; dp[2] = (ogpu_u8)f1; dp[3] = (ogpu_u8)f2; }
        else { dp[0] = (ogpu_u8)((f0 & 0xF8) | (f1 >> 5)); dp[1] = (ogpu_u8)(((f1 & 0x1C) << 3) | (f2 >> 3)); }
        count++;
    next:
        if (gouraud) { r += dr; g += dg; b += db; a += da; }
        z += dz; fv += dfv;
        if (tex) {
            if (persp) { su += dsu; sv += dsv; q += dq; seg--; }
            U += dU; V += dV;
        }
    }
#undef FETCH
    t->pixels += count;
}

/* Whether o3_span_fast draws this triangle exactly as o3_span would. */
static int o3_fast_ok(const struct ogpu3d *t, const struct o3_pipe *pp, const struct o3_tri *tr) {
    o3_u32 en = pp->en;
    if (pp->fmt != OGPU_FMT_ARGB32 && pp->fmt != OGPU_FMT_RGB565) return 0;
    if (en & (OGPU_R3D_STIPPLE | OGPU_R3D_CHROMA | OGPU_R3D_SPECULAR | OGPU_R3D_STENCIL)) return 0;
    if ((en & OGPU_R3D_LOGICOP) && t->logicop != 4) return 0;
    if (!pp->mask_all) return 0;
    if ((en & OGPU_R3D_BLEND) && !(pp->bsrc == 2 && pp->bdst == 1) && !(pp->bsrc == 7 && pp->bdst == 8)
        && !(pp->bsrc == 2 && pp->bdst == 2)) return 0;
    if (pp->tx) {
        const struct o3_level *L = &tr->L0;
        if (L->format != OGPU_FMT_ARGB32 || tr->lfrac) return 0;
        if (pp->env != OGPU_ENV_REPLACE && pp->env != OGPU_ENV_MODULATE) return 0;
        if (pp->base != OGPU_TEXBASE_RGBA && pp->base != OGPU_TEXBASE_RGB) return 0;
        if (L->wrap_s == OGPU_WRAP_REPEAT ? !L->wmask && L->w != 1 : L->wrap_s != OGPU_WRAP_CLAMP) return 0;
        if (L->wrap_t == OGPU_WRAP_REPEAT ? !L->hmask && L->h != 1 : L->wrap_t != OGPU_WRAP_CLAMP) return 0;
    }
    return 1;
}

/* ---- triangles ------------------------------------------------------------------------- */

/* gx, gy for one value: steps per pixel across and down. */
static void o3_gradient(o3_s32 da1, o3_s32 da2, o3_s32 dx1, o3_s32 dy1, o3_s32 dx2, o3_s32 dy2, o3_s64 area, o3_s32 *gx, o3_s32 *gy) {
    o3_s64 nx = (o3_s64)da1 * (dy2 * 16) - (o3_s64)da2 * (dy1 * 16);
    o3_s64 ny = (o3_s64)da2 * (dx1 * 16) - (o3_s64)da1 * (dx2 * 16);
    if (area < 0) { area = -area; nx = -nx; ny = -ny; }
    if (area <= 0x7FFFFFFFLL) { *gx = o3_div64(nx, (o3_s32)area); *gy = o3_div64(ny, (o3_s32)area); }
    else { *gx = o3_sat32(nx / area); *gy = o3_sat32(ny / area); }
}

static void o3_draw_triangle(struct ogpu3d *t, const struct o3_pipe *pp, const struct o3_vtx *in0, const struct o3_vtx *in1,
                          const struct o3_vtx *in2, int ax0, int ay0, int ax1, int ay1) {
    const struct o3_vtx *v0 = in0, *v1 = in1, *v2 = in2, *sw;
    struct o3_vtx tv[3];
    struct o3_tri tr;
    struct o3_edge el, er, *eshort, *elong;
    o3_s64 area;
    o3_s32 dx1, dy1, dx2, dy2;
    int k, py, ystart, ymid, yend, ry, fast;
    o3_u32 active = 0;

    /* sort by y */
    if (v1->y < v0->y) { sw = v0; v0 = v1; v1 = sw; }
    if (v2->y < v1->y) { sw = v1; v1 = v2; v2 = sw; }
    if (v1->y < v0->y) { sw = v0; v0 = v1; v1 = sw; }
    dx1 = v1->x - v0->x; dy1 = v1->y - v0->y; dx2 = v2->x - v0->x; dy2 = v2->y - v0->y;
    area = (o3_s64)dx1 * dy2 - (o3_s64)dx2 * dy1;
    if (!area) return;
    ystart = (v0->y - 8 + 15) >> 4;
    ymid = (v1->y - 8 + 15) >> 4;
    yend = (v2->y - 8 + 15) >> 4;
    if (ystart < ay0) ystart = ay0;
    if (yend > ay1) yend = ay1;
    if (ystart >= yend) return;

    /* which values this triangle carries */
    if (pp->en & OGPU_R3D_GOURAUD) active |= (1u << AT_R) | (1u << AT_G) | (1u << AT_B) | (1u << AT_A);
    if ((pp->en & OGPU_R3D_SPECULAR) && pp->has_spec) active |= (1u << AT_SR) | (1u << AT_SG) | (1u << AT_SB);
    if ((pp->en & OGPU_R3D_FOG) && t->fogmode == OGPU_FOG_VERTEX && pp->has_spec) active |= 1u << AT_F;
    if ((pp->en & OGPU_R3D_FOG) && t->fogmode && t->fogmode != OGPU_FOG_VERTEX && pp->has_w) active |= 1u << AT_FI;
    if ((pp->en & OGPU_R3D_ZTEST) && t->depth.mem && pp->has_z) active |= 1u << AT_Z;
    tr.persp = 0; tr.bilinear = 0; tr.lfrac = 0;

    if (pp->tx) {
        const struct ogpu3d_tex *tx = pp->tx;
        o3_s32 U[3], V[3], lo;
        o3_s64 ta;
        const struct o3_vtx *vv[3];
        int lam, minf, lv = 0;
        vv[0] = v0; vv[1] = v1; vv[2] = v2;
        for (k = 0; k < 3; k++) {
            /* u * width as 24.8 texels: u is clamped to +-128 textures, so this fits 32 bits */
            o3_s32 u = vv[k]->un, w = vv[k]->vn;
            if (u > 0x7FFFFF) u = 0x7FFFFF;
            if (u < -0x7FFFFF) u = -0x7FFFFF;
            if (w > 0x7FFFFF) w = 0x7FFFFF;
            if (w < -0x7FFFFF) w = -0x7FFFFF;
            U[k] = (u >> 8) * tx->w + (((u & 255) * tx->w) >> 8);
            V[k] = (w >> 8) * tx->h + (((w & 255) * tx->h) >> 8);
        }
        /* level of detail, once a triangle: texels per pixel, as log2 */
        ta = (o3_s64)(U[1] - U[0]) * (V[2] - V[0]) - (o3_s64)(U[2] - U[0]) * (V[1] - V[0]);
        if (ta < 0) ta = -ta;
        lam = ta ? (o3_log2_88(ta) - o3_log2_88(area < 0 ? -area : area) - 8 * 256) / 2 : -256;
        minf = tx->min;
        if (lam <= 0 || tx->levels < 2 || minf <= 2) {
            int filt = lam <= 0 ? tx->mag : (minf <= 2 ? minf : (minf == 5 || minf == 6 ? 2 : 1));
            tr.bilinear = filt == 2;
        } else {
            int fr = lam & 255;
            lv = lam >> 8;
            tr.bilinear = minf == 5 || minf == 6;           /* texels: LINEAR_MIP_* */
            if (minf == 3 || minf == 5) {                   /* levels: *_MIP_NEAREST */
                if (fr >= 128) lv++;
                fr = 0;
            }
            if (lv >= tx->levels - 1) { lv = tx->levels - 1; fr = 0; }
            tr.lfrac = fr;
        }
        o3_level_of(tx, lv, &tr.L0);
        if (tr.lfrac) o3_level_of(tx, lv + 1, &tr.L1);
        /* coordinates at the level drawn from, moved near 0 by whole textures when
         * they repeat, so their products with the perspective divisor fit */
        for (k = 0; k < 3; k++) { U[k] = o3_shr_s(U[k], lv); V[k] = o3_shr_s(V[k], lv); }
        if (tx->wrap_s == OGPU_WRAP_REPEAT) {
            o3_s32 tw = (tr.L0.w << 8) << (tr.lfrac ? 1 : 0), base;   /* whole textures at the next level too */
            lo = U[0] < U[1] ? U[0] : U[1]; if (U[2] < lo) lo = U[2];
            base = (tw & (tw - 1)) == 0 ? (lo & ~(tw - 1)) : (lo >= 0 ? lo / tw : -((-lo + tw - 1) / tw)) * tw;
            for (k = 0; k < 3; k++) U[k] -= base;
        }
        if (tx->wrap_t == OGPU_WRAP_REPEAT) {
            o3_s32 th = (tr.L0.h << 8) << (tr.lfrac ? 1 : 0), base;
            lo = V[0] < V[1] ? V[0] : V[1]; if (V[2] < lo) lo = V[2];
            base = (th & (th - 1)) == 0 ? (lo & ~(th - 1)) : (lo >= 0 ? lo / th : -((-lo + th - 1) / th)) * th;
            for (k = 0; k < 3; k++) V[k] -= base;
        }
        for (k = 0; k < 3; k++) {
            if (U[k] > 0x7FFFFFF) U[k] = 0x7FFFFFF;
            if (U[k] < -0x7FFFFFF) U[k] = -0x7FFFFFF;
            if (V[k] > 0x7FFFFFF) V[k] = 0x7FFFFFF;
            if (V[k] < -0x7FFFFFF) V[k] = -0x7FFFFFF;
        }
        /* perspective: q = w / max w (2^27..2^28 at the nearest vertex) */
        tr.persp = (pp->en & OGPU_R3D_PERSPECTIVE) && pp->has_w;
        if (tr.persp) {
            int emax = 0, e[3];
            for (k = 0; k < 3; k++) {
                e[k] = (int)((vv[k]->wbits >> 23) & 255);
                if (vv[k]->wbits >> 31) e[k] = 0;           /* w <= 0 never is the nearest */
                if (e[k] > emax) emax = e[k];
            }
            if (!emax || emax == 255) tr.persp = 0;
            else {
                for (k = 0; k < 3; k++) {
                    o3_s32 q;
                    if (!e[k]) q = 1;
                    else {
                        o3_u32 m = (vv[k]->wbits & 0x7FFFFFu) | 0x800000u;    /* 24 bits */
                        int sh = emax - e[k];
                        q = sh > 27 ? 1 : (o3_s32)o3_shr_u(m << 4, sh);
                        if (q < 1) q = 1;
                    }
                    tv[k] = *vv[k];
                    tv[k].at[AT_Q] = q;
                    tv[k].at[AT_U] = o3_mul_shr28(U[k], q);
                    tv[k].at[AT_V] = o3_mul_shr28(V[k], q);
                }
                active |= (1u << AT_U) | (1u << AT_V) | (1u << AT_Q);
            }
        }
        if (!tr.persp) {
            for (k = 0; k < 3; k++) {
                tv[k] = *vv[k];
                tv[k].at[AT_U] = U[k];
                tv[k].at[AT_V] = V[k];
            }
            active |= (1u << AT_U) | (1u << AT_V);
        }
        v0 = &tv[0]; v1 = &tv[1]; v2 = &tv[2];
    }

    /* planes: steps across and down, and the value at the start row in column rx */
    tr.rx = v0->x >> 4; ry = v0->y >> 4;
    tr.nact = 0;
    {
        o3_s32 offx = tr.rx * 16 + 8 - v0->x, offy = ry * 16 + 8 - v0->y;
        for (k = 0; k < AT_N; k++) {
            o3_s32 gx, gy, ref;
            if (!(active & (1u << k))) continue;
            tr.act[tr.nact++] = k;
            o3_gradient(v1->at[k] - v0->at[k], v2->at[k] - v0->at[k], dx1, dy1, dx2, dy2, area, &gx, &gy);
            tr.gx[k] = gx; tr.gy[k] = gy;
            /* the value at pixel (rx, ry): v0's, moved by less than a pixel */
            ref = (o3_s32)((o3_u32)v0->at[k] + (o3_u32)((gx >> 4) * offx) + (o3_u32)(((gx & 15) * offx) >> 4)
                        + (o3_u32)((gy >> 4) * offy) + (o3_u32)(((gy & 15) * offy) >> 4));
            tr.row[k] = (o3_s32)((o3_u32)ref + o3_mulw((o3_u32)gy, ystart - ry));
        }
    }

    fast = !t->slow && o3_fast_ok(t, pp, &tr);

    /* edges: the long one v0-v2 and the short ones v0-v1, v1-v2 */
    elong = area > 0 ? &el : &er;
    eshort = area > 0 ? &er : &el;
    o3_edge_init(elong, v0, v2, ystart);
    if (ystart < ymid) o3_edge_init(eshort, v0, v1, ystart);
    else o3_edge_init(eshort, v1, v2, ystart);
    for (py = ystart; py < yend; py++) {
        int xl, xr;
        if (py == ymid && ymid > ystart) o3_edge_init(eshort, v1, v2, py);
        xl = o3_edge_px(&el); xr = o3_edge_px(&er);
        if (xl < ax0) xl = ax0;
        if (xr > ax1) xr = ax1;
        if (xl < xr) {
            if (fast) o3_span_fast(t, pp, &tr, py, xl, xr);
            else o3_span(t, pp, &tr, py, xl, xr);
        }
        o3_edge_step(&el); o3_edge_step(&er);
        for (k = 0; k < tr.nact; k++) {
            int a = tr.act[k];
            tr.row[a] = (o3_s32)((o3_u32)tr.row[a] + (o3_u32)tr.gy[a]);
        }
    }
    t->triangles++;
}

/* ---- TRIANGLES ------------------------------------------------------------------------- */

static void o3_decode_vertex(const ogpu_u8 *p, o3_u32 layout, struct o3_vtx *v) {
    o3_s32 x = (o3_s32)o3_rd32(p), y = (o3_s32)o3_rd32(p + 4);
    int at = 20;
    /* 16.16 to 28.4, rounded; positions stay within +-16383 */
    if (x > 0x3FFF0000) x = 0x3FFF0000;
    if (x < -0x3FFF0000) x = -0x3FFF0000;
    if (y > 0x3FFF0000) y = 0x3FFF0000;
    if (y < -0x3FFF0000) y = -0x3FFF0000;
    v->x = (x + 0x800) >> 12;
    v->y = (y + 0x800) >> 12;
    v->at[AT_A] = (o3_s32)p[8] << 16;
    v->at[AT_R] = (o3_s32)p[9] << 16;
    v->at[AT_G] = (o3_s32)p[10] << 16;
    v->at[AT_B] = (o3_s32)p[11] << 16;
    v->un = (o3_s32)o3_rd32(p + 12);
    v->vn = (o3_s32)o3_rd32(p + 16);
    v->at[AT_Z] = 0; v->wbits = 0x3F800000u; v->at[AT_FI] = OGPU3D_FOGTAB << 16;
    v->at[AT_SR] = v->at[AT_SG] = v->at[AT_SB] = 0; v->at[AT_F] = 255 << 16;
    if (layout & OGPU_LAY_Z) { v->at[AT_Z] = (o3_s32)(o3_rd32(p + at) >> 2); at += 4; }
    if (layout & OGPU_LAY_W) {
        v->wbits = o3_rd32(p + at); at += 4;
        v->at[AT_FI] = o3_f2fix(v->wbits, 16 + 10, 0x3FFFFFFF);   /* w * 1024 in 16.16 */
    }
    if (layout & OGPU_LAY_SPEC) {
        v->at[AT_F] = (o3_s32)p[at] << 16;
        v->at[AT_SR] = (o3_s32)p[at + 1] << 16;
        v->at[AT_SG] = (o3_s32)p[at + 2] << 16;
        v->at[AT_SB] = (o3_s32)p[at + 3] << 16;
    }
}

static int o3_vertex_size(o3_u32 layout) {
    int n = 20;
    if (layout & OGPU_LAY_Z) n += 4;
    if (layout & OGPU_LAY_W) n += 4;
    if (layout & OGPU_LAY_SPEC) n += 4;
    if (layout & OGPU_LAY_UV1) n += 8;
    return n;
}

static int o3_op_triangles(struct ogpu3d *t, const ogpu_u8 *a, long args) {
    struct ogpu_core *c = t->core;
    o3_u32 vaddr = o3_rd32(a), vcount = o3_rd32(a + 4), stride = o3_rd32(a + 8);
    o3_u32 iaddr = o3_rd32(a + 12), icount = o3_rd32(a + 16), isize = o3_rd32(a + 20);
    o3_u32 texw = o3_rd32(a + 24) & 0xFFFF, flags = o3_rd32(a + 28), layout = 0;
    const ogpu_u8 *verts, *idx = 0;
    struct o3_pipe pp;
    struct ogpu3d_tex slot_tex;
    int three_d = args >= 9, prim, ax0, ay0, ax1, ay1;
    o3_u32 n, i, vsize;
    if (three_d) layout = o3_rd32(a + 32);
    prim = (int)(layout & OGPU_LAY_PRIM_MASK);
    vsize = (o3_u32)o3_vertex_size(layout);
    if (!vcount) return OGPU_OK;
    if (stride < vsize || vcount > 0x00FFFFFFu || stride > 0x10000u || prim > OGPU_LAY_FAN) return OGPU_ERR_UNSUPPORTED;
    verts = c->map(c->user, vaddr, stride * (vcount - 1) + vsize);
    if (!verts) return OGPU_ERR_NOMAP;
    if (iaddr) {
        if (isize != 1 && isize != 2 && isize != 4) return OGPU_ERR_UNSUPPORTED;
        if (!icount) return OGPU_OK;
        if (icount > 0x00FFFFFFu) return OGPU_ERR_UNSUPPORTED;
        idx = c->map(c->user, iaddr, icount * isize);
        if (!idx) return OGPU_ERR_NOMAP;
    }
    pp.dst = &c->slot[c->target];
    pp.fmt = pp.dst->format;
    if (pp.fmt != OGPU_FMT_ARGB32 && pp.fmt != OGPU_FMT_RGB565 && pp.fmt != OGPU_FMT_CLUT8) return OGPU_ERR_UNSUPPORTED;

    /* the settings */
    pp.tx = 0;
    pp.has_z = (layout & OGPU_LAY_Z) != 0;
    pp.has_w = (layout & OGPU_LAY_W) != 0;
    pp.has_spec = (layout & OGPU_LAY_SPEC) != 0;
    if (three_d) {
        pp.en = t->enables;
        pp.bsrc = t->bsrc; pp.bdst = t->bdst;
        if ((pp.en & OGPU_R3D_TEXTURE) && t->env[0] != OGPU_ENV_OFF && texw == OGPU_TRI_UNIT0 && t->tex[0].level[0])
            pp.tx = &t->tex[0];
        pp.env = t->env[0];
        o3_unpack(t->env_colour[0], pp.envc);
        pp.base = pp.tx ? pp.tx->base : 0;
    } else {
        /* SDL's form: Gouraud colour times texture, a blend mode, no Z */
        pp.en = OGPU_R3D_GOURAUD;
        pp.env = OGPU_ENV_MODULATE; pp.base = OGPU_TEXBASE_RGBA;
        o3_unpack(0, pp.envc);
        pp.bsrc = 2; pp.bdst = 1;                               /* ONE, ZERO: a copy */
        switch (flags & OGPU_TRI_BLEND_MASK) {
        case OGPU_TRI_BLEND_BLEND: pp.bsrc = 7; pp.bdst = 8; break;        /* SRC_ALPHA, 1 - SRC_ALPHA */
        case OGPU_TRI_BLEND_ADD: pp.bsrc = 7; pp.bdst = 2; break;          /* SRC_ALPHA, ONE */
        case OGPU_TRI_BLEND_MOD: pp.bsrc = 1; pp.bdst = 3; break;          /* ZERO, SRC_COLOR */
        case OGPU_TRI_BLEND_MUL: pp.bsrc = 4; pp.bdst = 8; break;          /* DST_COLOR, 1 - SRC_ALPHA */
        }
        if (pp.bsrc != 2 || pp.bdst != 1) pp.en |= OGPU_R3D_BLEND;
    }
    if (!pp.tx && texw < OGPU_MAX_SLOTS && (!three_d || (pp.en & OGPU_R3D_TEXTURE))) {
        /* a SURFACE slot as the texture */
        const struct ogpu_surface *s = &c->slot[texw];
        if (!s->pixels) return OGPU_ERR_NOSURFACE;
        if (s->bpr > 0xFFFF) return OGPU_ERR_UNSUPPORTED;
        slot_tex.level[0] = s->pixels; slot_tex.bpr[0] = s->bpr;
        slot_tex.w = s->w; slot_tex.h = s->h; slot_tex.levels = 1;
        slot_tex.format = s->format; slot_tex.bpp = o3_bpp_of(s->format);
        slot_tex.base = s->format == OGPU_FMT_A8 ? OGPU_TEXBASE_ALPHA : OGPU_TEXBASE_RGBA;
        slot_tex.table = 0;
        slot_tex.min = slot_tex.mag = (flags & OGPU_TRI_BILINEAR) ? 2 : 1;
        slot_tex.wrap_s = slot_tex.wrap_t = (flags & OGPU_TRI_CLAMP) ? OGPU_WRAP_CLAMP : OGPU_WRAP_REPEAT;
        slot_tex.border = 0;
        pp.tx = &slot_tex;
        pp.base = slot_tex.base;
        if (!three_d) pp.en |= OGPU_R3D_TEXTURE;
    }
    o3_unpack(three_d ? t->current : 0xFFFFFFFFu, pp.cur);
    o3_unpack(t->fogcolour, pp.fog);
    o3_unpack(t->bconst, pp.kc);
    pp.wmask = three_d ? o3_write_mask(t, pp.fmt) : 0xFFFFFFFFu;
    pp.mask_all = !three_d || pp.wmask == (pp.fmt == OGPU_FMT_ARGB32 ? 0xFFFFFFFFu : pp.fmt == OGPU_FMT_RGB565 ? 0xFFFFu : 0xFFu);
    if (!o3_draw_area(t, three_d, &ax0, &ay0, &ax1, &ay1)) return OGPU_OK;

    /* the primitives */
    n = idx ? icount : vcount;
    {
        struct o3_vtx vb[3];
        int have = 0;
        for (i = 0; i < n; i++) {
            o3_u32 vi = i;
            if (idx) {
                if (isize == 1) vi = idx[i];
                else if (isize == 2) vi = ((o3_u32)idx[i * 2] << 8) | idx[i * 2 + 1];
                else vi = o3_rd32(idx + i * 4);
                if (vi >= vcount) return OGPU_ERR_UNSUPPORTED;
            }
            if (prim == OGPU_LAY_LIST) {
                o3_decode_vertex(verts + vi * stride, layout, &vb[have]);
                if (++have == 3) {
                    o3_draw_triangle(t, &pp, &vb[0], &vb[1], &vb[2], ax0, ay0, ax1, ay1);
                    have = 0;
                }
            } else {
                /* strips and fans keep two vertices: a strip the last two, a fan the first and the last */
                if (have == 3) {
                    if (prim == OGPU_LAY_STRIP) vb[0] = vb[1];
                    vb[1] = vb[2];
                    have = 2;
                }
                o3_decode_vertex(verts + vi * stride, layout, &vb[have]);
                if (++have == 3) o3_draw_triangle(t, &pp, &vb[0], &vb[1], &vb[2], ax0, ay0, ax1, ay1);
            }
        }
    }
    return OGPU_OK;
}

/* ---- the entry points ------------------------------------------------------------------ */

void ogpu3d_init(struct ogpu3d *t, struct ogpu_core *c) {
    int i;
    t->core = c;
    for (i = 0; i < OGPU_TEX_UNITS; i++) {
        t->tex[i].level[0] = 0;
        t->env[i] = OGPU_ENV_MODULATE;
        t->env_colour[i] = 0;
    }
    t->depth.mem = 0; t->stencil.mem = 0;
    t->enables = OGPU_R3D_GOURAUD;
    t->zfunc = 2; t->afunc = 8; t->aref = 0; t->bsrc = 2; t->bdst = 1; t->bconst = 0;
    t->cmask = 0xFF0F; t->logicop = 4; t->fogmode = 0; t->fogcolour = 0; t->chroma = 1;
    t->sfunc = 2; t->sref = 0; t->smask = 255; t->sfail = t->szfail = t->szpass = 1; t->swmask = 255;
    t->chroma_lo = t->chroma_hi = 0; t->current = 0xFFFFFFFFu;
    t->pen_table = 0; t->stipple = 0; t->palette = 0;
    t->triangles = 0; t->pixels = 0; t->slow = 0;
    t->fogkey[0] = 0; t->fogkey[1] = t->fogkey[2] = t->fogkey[3] = 0;
    for (i = 0; i < OGPU3D_FOGTAB; i++) t->fogtab[i] = 255;
}

int ogpu3d_is_op(int op) {
    return op >= OGPU_OP_3D_FIRST && op <= OGPU_OP_3D_LAST;
}

int ogpu3d_op(struct ogpu3d *t, int op, const ogpu_u8 *cmd, long words) {
    struct ogpu_core *c = t->core;
    const ogpu_u8 *a = cmd + 4;
    long args = words - 1;
    switch (op) {
    case OGPU_OP_TRIANGLES:
        if (args < 8) return OGPU_ERR_BADLEN;
        if (c->target < 0 || !c->slot[c->target].pixels) return OGPU_ERR_NOSURFACE;
        if (c->slot[c->target].bpr > 0xFFFF) return OGPU_ERR_UNSUPPORTED;
        return o3_op_triangles(t, a, args);
    case OGPU_OP_RENDER3D:
        if (args < OGPU_R3D_ARGS) return OGPU_ERR_BADLEN;
        return o3_op_render3d(t, a);
    case OGPU_OP_TEXENV:
        if (args < 3) return OGPU_ERR_BADLEN;
        return o3_op_texenv(t, a);
    case OGPU_OP_TEXTURE:
        if (args < 11) return OGPU_ERR_BADLEN;
        return o3_op_texture(t, a, args);
    case OGPU_OP_DEPTH:
        if (args < 5) return OGPU_ERR_BADLEN;
        return o3_op_depth(t, a);
    case OGPU_OP_CLEAR3D:
        if (args < 4) return OGPU_ERR_BADLEN;
        return o3_op_clear3d(t, a);
    }
    return OGPU_ERR_BADOP;
}

#ifdef OGPU_CORE_HAS_D3
int ogpu_3d_run(struct ogpu_core *c, int op, const ogpu_u8 *cmd, long words) {
    return c->d3 ? ogpu3d_op(c->d3, op, cmd, words) : OGPU_ERR_BADOP;
}
#endif

long ogpu3d_run(struct ogpu3d *t, const ogpu_u8 *stream, long words) {
    struct ogpu_core *c = t->core;
    long at = 0, done = 0, run_from = 0;
    int first = OGPU_OK;
    long first_at = 0;
    while (at <= words) {
        int op = -1, len = 0;
        if (at < words) {
            o3_u32 hdr = o3_rd32(stream + at * 4);
            op = (int)OGPU_HDR_OP(hdr); len = (int)OGPU_HDR_WORDS(hdr);
            if (len < 1 || at + len > words) op = -2;          /* let the core report it */
        }
        if (at == words || op == -2 || ogpu3d_is_op(op)) {
            /* hand the 2D commands before this one to the core */
            if (at > run_from || op == -2) {
                done += ogpu_core_run(c, stream + run_from * 4, (op == -2 ? words : at) - run_from);
                if (c->last_error != OGPU_OK && first == OGPU_OK) { first = c->last_error; first_at = run_from + c->error_word; }
            }
            if (at == words || op == -2) break;
            {
                int r = ogpu3d_op(t, op, stream + at * 4, len);
                if (r != OGPU_OK && first == OGPU_OK) { first = r; first_at = at; }
                done++;
            }
            at += len;
            run_from = at;
            continue;
        }
        at += len;
    }
    c->last_error = first;
    c->error_word = first_at;
    return done;
}

int ogpu3d_supports(int op, int format) {
    if (!ogpu3d_is_op(op)) return OGPU_NONE;
    if (op == OGPU_OP_TRIANGLES)
        return format == OGPU_FMT_ARGB32 || format == OGPU_FMT_RGB565 ? OGPU_FULL
             : format == OGPU_FMT_CLUT8 ? OGPU_PARTIAL : OGPU_NONE;       /* CLUT8 through a pen table */
    return OGPU_FULL;
}
