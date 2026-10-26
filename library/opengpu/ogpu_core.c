/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's core (DESIGN.md section 5): one rasterizer for every back end.
 * The 68k runs it as the CPU back end, the Cradle's runtime runs it behind
 * ACRTG.gpu, and a PiStorm's spare ARM core runs it behind PiStorm.gpu;
 * tests/test_opengpu.c checks it against simple per-pixel references and
 * keeps golden checksums every build must match. Stream v1.7: the 2D
 * commands here (v1.6's paths, gradients, blur and blend modes in their
 * own section below, tested by tests/test_vector.c; v1.7's SDL 3 additions
 * through the commands, tested by tests/test_sdl3.c); the 3D range
 * (0x0030-0x003F) goes to ogpu_3d.c when the build links it (OGPU_WITH_3D).
 *
 * Integers only, no C library, pixels read and written byte by byte in the
 * Amiga's order, so the result is the same on a big- or little-endian CPU.
 *
 * Written for the 68k (8 October 2026). The pixel loops work on bytes and
 * channels (A, R, G, B kept apart), multiply only 16-bit values (MUL16:
 * mulu.w), shift only by constants of 8 or less, and step pointers instead
 * of multiplying per pixel. On a real 68040 that is as fast as the plain
 * form; on the AC090's JIT, where a shift by a register, a 32-bit multiply
 * or a bitfield instruction costs 70 ns or more (a shift by 24 nearly 2 us),
 * it is hundreds of times faster. The 68k builds pass -mnobitfield, as GCC
 * otherwise picks bfextu for (x >> 8) & 255. OGPU_PLAIN_C=1 puts back the
 * plain form of the few helpers that differ, for when the JIT is fixed.
 */
#include "ogpu_core.h"
#ifdef OGPU_WITH_3D
#include "ogpu_3d.h"
#endif

#ifndef OGPU_PLAIN_C
#define OGPU_PLAIN_C 0
#endif

/* Each command is its own function: inlined into the run, they only make
 * it hard to read what a pixel costs. */
#if defined(__GNUC__)
#define OP_FN static __attribute__((noinline)) int
#else
#define OP_FN static int
#endif

/* The per-pixel helpers go inline into each command's loop: a call costs
 * more than the work. */
#if defined(__GNUC__)
#define PX_FN static inline __attribute__((always_inline))
#else
#define PX_FN static
#endif

/* x * y for x, y in 0..65535: one mulu.w on the 68k. GCC turns the C form
 * into a 32-bit mulu.l on a 68040, so the 68k gets it in assembly. */
#if (defined(__mc68000__) || defined(__m68k__)) && defined(__GNUC__) && !OGPU_PLAIN_C
static inline unsigned MUL16(unsigned x, unsigned y) {
    __asm__ ("mulu.w %1,%0" : "+d"(x) : "d"(y) : "cc");
    return x;
}
static inline int SMUL16(int x, int y) {
    __asm__ ("muls.w %1,%0" : "+d"(x) : "d"(y) : "cc");
    return x;
}
#else
#define MUL16(x, y) ((unsigned)(unsigned short)(x) * (unsigned)(unsigned short)(y))
#define SMUL16(x, y) ((int)(short)(x) * (int)(short)(y))
#endif

/* ---- small helpers ---------------------------------------------------------------- */

static ogpu_u32 rd32(const ogpu_u8 *p) {
    return ((ogpu_u32)p[0] << 24) | ((ogpu_u32)p[1] << 16) | ((ogpu_u32)p[2] << 8) | (ogpu_u32)p[3];
}

static int s16(ogpu_u32 v) { v &= 0xFFFFUL; return v >= 0x8000UL ? (int)v - 0x10000 : (int)v; }
static int u16(ogpu_u32 v) { return (int)(v & 0xFFFFUL); }
static int hi_s(ogpu_u32 w) { return s16(w >> 16); }
static int lo_s(ogpu_u32 w) { return s16(w); }
static int hi_u(ogpu_u32 w) { return u16(w >> 16); }
static int lo_u(ogpu_u32 w) { return u16(w); }

/* The bytes of a word, high first, without a shift by 24 (a register shift
 * on the 68k). */
static void bytes_of(ogpu_u32 v, ogpu_u8 *b) {
#if OGPU_PLAIN_C
    b[0] = (ogpu_u8)(v >> 24); b[1] = (ogpu_u8)(v >> 16); b[2] = (ogpu_u8)(v >> 8); b[3] = (ogpu_u8)v;
#else
    ogpu_u32 hi = v >> 16;                  /* swap */
    b[0] = (ogpu_u8)(hi >> 8); b[1] = (ogpu_u8)hi; b[2] = (ogpu_u8)(v >> 8); b[3] = (ogpu_u8)v;
#endif
}

/* Format tables. (A switch over grouped formats becomes GCC's "1 << format,
 * then AND" bit test: a shift by a register, slow on the AC090's JIT.) */
static const unsigned char fmt_bpp[OGPU_FMT_COUNT] = { 0, 1, 2, 4, 1, 1, 2, 2, 2, 4, 4, 4, 4 };
static const unsigned char fmt_colour[OGPU_FMT_COUNT] = { 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1 };

static int bytes_per_pixel(int format) {
    return format > 0 && format < OGPU_FMT_COUNT ? fmt_bpp[format] : 0;
}

/* Formats with colours (COMPOSITE, MASK and the blends draw into them). */
static int is_colour(int format) {
    return format > 0 && format < OGPU_FMT_COUNT && fmt_colour[format];
}

/* A pixel as its four channels, 0-255 each. */
struct ch { unsigned a, r, g, b; };

PX_FN unsigned x5(unsigned v) { return (v << 3) | (v >> 2); }   /* 5 bits to 8 */
PX_FN unsigned x6(unsigned v) { return (v << 2) | (v >> 4); }   /* 6 bits to 8 */

/* Read a pixel's channels. CLUT8 and INDEX8 have none (callers keep them
 * out); A8 is coverage over black. */
PX_FN void ld(const ogpu_u8 *p, int f, struct ch *o) {
    unsigned hi, lo;
    switch (f) {
    case OGPU_FMT_ARGB32: o->a = p[0]; o->r = p[1]; o->g = p[2]; o->b = p[3]; return;
    case OGPU_FMT_BGRA32: o->b = p[0]; o->g = p[1]; o->r = p[2]; o->a = p[3]; return;
    case OGPU_FMT_XRGB32: o->a = 255; o->r = p[1]; o->g = p[2]; o->b = p[3]; return;
    case OGPU_FMT_RGBA32: o->r = p[0]; o->g = p[1]; o->b = p[2]; o->a = p[3]; return;
    case OGPU_FMT_ABGR32: o->a = p[0]; o->b = p[1]; o->g = p[2]; o->r = p[3]; return;
    case OGPU_FMT_RGB565: hi = p[0]; lo = p[1]; goto c565;
    case OGPU_FMT_RGB565PC: lo = p[0]; hi = p[1];
    c565:
        o->a = 255; o->r = x5(hi >> 3); o->g = x6(((hi & 7) << 3) | (lo >> 5)); o->b = x5(lo & 31);
        return;
    case OGPU_FMT_RGB555: hi = p[0]; lo = p[1]; goto c555;
    case OGPU_FMT_RGB555PC: lo = p[0]; hi = p[1];
    c555:
        o->a = 255; o->r = x5((hi >> 2) & 31); o->g = x5(((hi & 3) << 3) | (lo >> 5)); o->b = x5(lo & 31);
        return;
    case OGPU_FMT_A8: o->a = p[0]; o->r = o->g = o->b = 0; return;
    }
    o->a = 255; o->r = o->g = o->b = p[0];
}

/* Write channels as a pixel (truncating to 5 or 6 bits, as v1.0 did). */
PX_FN void st(ogpu_u8 *p, int f, unsigned a, unsigned r, unsigned g, unsigned b) {
    unsigned hi, lo;
    switch (f) {
    case OGPU_FMT_ARGB32: p[0] = (ogpu_u8)a; p[1] = (ogpu_u8)r; p[2] = (ogpu_u8)g; p[3] = (ogpu_u8)b; return;
    case OGPU_FMT_BGRA32: p[0] = (ogpu_u8)b; p[1] = (ogpu_u8)g; p[2] = (ogpu_u8)r; p[3] = (ogpu_u8)a; return;
    case OGPU_FMT_XRGB32: p[0] = 0; p[1] = (ogpu_u8)r; p[2] = (ogpu_u8)g; p[3] = (ogpu_u8)b; return;
    case OGPU_FMT_RGBA32: p[0] = (ogpu_u8)r; p[1] = (ogpu_u8)g; p[2] = (ogpu_u8)b; p[3] = (ogpu_u8)a; return;
    case OGPU_FMT_ABGR32: p[0] = (ogpu_u8)a; p[1] = (ogpu_u8)b; p[2] = (ogpu_u8)g; p[3] = (ogpu_u8)r; return;
    case OGPU_FMT_RGB565:
        p[0] = (ogpu_u8)((r & 0xF8) | (g >> 5)); p[1] = (ogpu_u8)(((g & 0x1C) << 3) | (b >> 3)); return;
    case OGPU_FMT_RGB565PC:
        p[1] = (ogpu_u8)((r & 0xF8) | (g >> 5)); p[0] = (ogpu_u8)(((g & 0x1C) << 3) | (b >> 3)); return;
    case OGPU_FMT_RGB555:
        hi = ((r & 0xF8) >> 1) | (g >> 6); lo = ((g & 0x38) << 2) | (b >> 3);
        p[0] = (ogpu_u8)hi; p[1] = (ogpu_u8)lo; return;
    case OGPU_FMT_RGB555PC:
        hi = ((r & 0xF8) >> 1) | (g >> 6); lo = ((g & 0x38) << 2) | (b >> 3);
        p[1] = (ogpu_u8)hi; p[0] = (ogpu_u8)lo; return;
    case OGPU_FMT_A8: p[0] = (ogpu_u8)a; return;
    }
    p[0] = (ogpu_u8)r;
}

/* A raw value of the target's format (FILL's colour, a pen, INVERT's mask)
 * as the bytes it is stored as. All four bytes are set (the ones a 1- or
 * 2-byte pixel doesn't use are 0), so a caller may read all four. */
static void raw_bytes(ogpu_u32 v, int f, ogpu_u8 *b) {
    ogpu_u8 w[4];
    bytes_of(v, w);
    b[1] = b[2] = b[3] = 0;
    switch (bytes_per_pixel(f)) {
    case 4: b[0] = f == OGPU_FMT_XRGB32 ? 0 : w[0]; b[1] = w[1]; b[2] = w[2]; b[3] = w[3]; return;
    case 2:
        if (f == OGPU_FMT_RGB565PC || f == OGPU_FMT_RGB555PC) { b[0] = w[3]; b[1] = w[2]; }
        else { b[0] = w[2]; b[1] = w[3]; }
        return;
    }
    b[0] = w[3];
}

/* What COMPLEMENT flips: every bit of a pen or a 16-bit pixel, and the
 * colour (not the alpha) of a 32-bit one. */
static void flip_bytes(int f, ogpu_u8 *b) {
    b[0] = b[1] = b[2] = b[3] = 0xFF;
    if (f == OGPU_FMT_ARGB32 || f == OGPU_FMT_XRGB32 || f == OGPU_FMT_ABGR32) b[0] = 0;
    if (f == OGPU_FMT_BGRA32 || f == OGPU_FMT_RGBA32) b[3] = 0;
}

/* x * y / 255, rounded, for x and y in 0..255. */
PX_FN unsigned mul255(unsigned x, unsigned y) {
    unsigned t = MUL16(x, y) + 128;
    return (t + (t >> 8)) >> 8;
}

static ogpu_u8 *px_at(const struct ogpu_surface *s, int x, int y) {
    return s->pixels + (long)y * s->bpr + (long)x * bytes_per_pixel(s->format);
}

/* The bytes h rows of `last` bytes take at bpr apart, in *len; 0 when that
 * passes 32 bits (as it can on the 68k), which no Amiga memory holds. */
static int span_len(ogpu_u32 bpr, int h, ogpu_u32 last, ogpu_u32 *len) {
    ogpu_u32 rows = (ogpu_u32)(h - 1);
    if (rows && bpr > (0xFFFFFFFFUL - last) / rows) return 0;
    *len = bpr * rows + last;
    return 1;
}

/* The drawable rectangle: the target, less the clip. */
static void bounds(const struct ogpu_core *c, int *x0, int *y0, int *x1, int *y1) {
    const struct ogpu_surface *t = &c->slot[c->target];
    *x0 = 0; *y0 = 0; *x1 = t->w; *y1 = t->h;
    if (c->cx1 > c->cx0) {
        if (c->cx0 > *x0) *x0 = c->cx0;
        if (c->cy0 > *y0) *y0 = c->cy0;
        if (c->cx1 < *x1) *x1 = c->cx1;
        if (c->cy1 < *y1) *y1 = c->cy1;
    }
}

/* Clip a rectangle on the target to the target and the clip rectangle.
 * Returns 0 when nothing is left; otherwise *ox, *oy say how far its corner
 * moved, so a source can move with it. */
static int clip_rect(const struct ogpu_core *c, int *x, int *y, int *w, int *h, int *ox, int *oy) {
    int x0, y0, x1, y1, nx, ny, nx1, ny1;
    bounds(c, &x0, &y0, &x1, &y1);
    nx = *x < x0 ? x0 : *x;
    ny = *y < y0 ? y0 : *y;
    nx1 = *x + *w > x1 ? x1 : *x + *w;
    ny1 = *y + *h > y1 ? y1 : *y + *h;
    if (nx1 <= nx || ny1 <= ny) return 0;
    *ox = nx - *x; *oy = ny - *y;
    *x = nx; *y = ny; *w = nx1 - nx; *h = ny1 - ny;
    return 1;
}

/* Copy one pixel's bytes. */
PX_FN void put_raw(ogpu_u8 *p, int bpp, const ogpu_u8 *v) {
    p[0] = v[0];
    if (bpp > 1) { p[1] = v[1]; if (bpp > 2) { p[2] = v[2]; p[3] = v[3]; } }
}
PX_FN void xor_raw(ogpu_u8 *p, int bpp, const ogpu_u8 *v) {
    p[0] ^= v[0];
    if (bpp > 1) { p[1] ^= v[1]; if (bpp > 2) { p[2] ^= v[2]; p[3] ^= v[3]; } }
}

/* v1.4: whole 32-bit words where a row allows it (fills and copies): the
 * same bytes as the byte loops, a quarter of the stores. The word is put
 * together in memory order, so it is right on either byte order. */
typedef unsigned int ogpu_w32;          /* 32 bits on the 68k and the hosts */
PX_FN ogpu_w32 word_of(const ogpu_u8 *b) {
    ogpu_w32 w;
    ogpu_u8 *p = (ogpu_u8 *)&w;
    p[0] = b[0]; p[1] = b[1]; p[2] = b[2]; p[3] = b[3];
    return w;
}
PX_FN int aligned4(const void *p) { return ((unsigned long)p & 3) == 0; }

/* OGPU_HOST (the Cradle's runtime defines it before it includes this file;
 * hosts with a C library and fast 32-bit multiplies): rows copied with the
 * C library's memmove, and COMPOSITE_PD's common cases worked on whole
 * pixels, two channels a multiply (each byte lane rounded and saturated
 * as mul255 and sat8 do, so the bytes are the same). The 68k keeps the
 * byte forms: there a 32-bit multiply is slow on the AC090's JIT. */
#ifndef OGPU_HOST
#define OGPU_HOST 0
#endif
#if OGPU_HOST
#include <string.h>
/* each byte lane of x times a / 255, rounded as mul255 */
PX_FN ogpu_w32 mul_lanes(ogpu_w32 x, ogpu_w32 a) {
    ogpu_w32 rb = (x & 0x00FF00FFu) * a + 0x00800080u, ag = ((x >> 8) & 0x00FF00FFu) * a + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
    return rb | ag;
}
/* each byte lane of x + y, saturating at 255 */
PX_FN ogpu_w32 add_lanes(ogpu_w32 x, ogpu_w32 y) {
    ogpu_w32 rb = (x & 0x00FF00FFu) + (y & 0x00FF00FFu), ag = ((x >> 8) & 0x00FF00FFu) + ((y >> 8) & 0x00FF00FFu);
    rb |= 0x01000100u - ((rb >> 8) & 0x00FF00FFu);
    ag |= 0x01000100u - ((ag >> 8) & 0x00FF00FFu);
    return (rb & 0x00FF00FFu) | ((ag & 0x00FF00FFu) << 8);
}
/* where an ARGB32 pixel's A (its first byte in memory) is in a word read on this host */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LANE_A(w) ((w) & 0xFFu)
#define LANE_A_MASK 0x000000FFu
#else
#define LANE_A(w) ((w) >> 24)
#define LANE_A_MASK 0xFF000000u
#endif
#endif

/* n bytes of one row from sp to dp, front to back (no overlap that matters) */
PX_FN void copy_fwd(ogpu_u8 *dp, const ogpu_u8 *sp, int n) {
#if OGPU_HOST
    memmove(dp, sp, (size_t)n);
    return;
#endif
    if (aligned4(dp) && aligned4(sp)) {
        ogpu_w32 *q = (ogpu_w32 *)dp;
        const ogpu_w32 *r = (const ogpu_w32 *)sp;
        int k = n >> 2;
        n &= 3;
        while (k >= 4) { q[0] = r[0]; q[1] = r[1]; q[2] = r[2]; q[3] = r[3]; q += 4; r += 4; k -= 4; }
        while (k--) *q++ = *r++;
        dp = (ogpu_u8 *)q; sp = (const ogpu_u8 *)r;
    }
    while (n--) *dp++ = *sp++;
}

/* MOVE16 (Open, 22 Oct 2026): a row copied 16 bytes a time, as whole
 * cache lines, when source and destination are both in Fast RAM on the
 * CPU's own bus (the library lists it; MOVE16 is a burst, which Zorro and
 * video RAM may not take) and line up on 16 bytes with each other. The
 * bytes are the same as copy_fwd's. 68k builds only. */
#if (defined(__mc68040__) || defined(__mc68060__)) && defined(__GNUC__) && !OGPU_HOST
#define OGPU_MOVE16 1
PX_FN int in_fast(const struct ogpu_core *c, const ogpu_u8 *p, int n) {
    ogpu_u32 a = (ogpu_u32)p, e = a + (ogpu_u32)n;
    int i;
    for (i = 0; i < c->nfast; i++)
        if (a >= c->fast_lo[i] && e <= c->fast_hi[i]) return 1;
    return 0;
}
static void copy_move16(ogpu_u8 *dp, const ogpu_u8 *sp, int n) {
    int head = (int)((16 - ((ogpu_u32)dp & 15)) & 15), lines;
    if (head) { copy_fwd(dp, sp, head); dp += head; sp += head; n -= head; }
    lines = n >> 4;
    n &= 15;
    if (lines) {
        const ogpu_u8 *s = sp;
        ogpu_u8 *d = dp;
        int k = lines;
        __asm__ volatile ("1:\n\tmove16 (%0)+,(%1)+\n\tsubq.l #1,%2\n\tbne.s 1b"
                          : "+a"(s), "+a"(d), "+d"(k) : : "cc", "memory");
        sp = s; dp = d;
    }
    if (n) copy_fwd(dp, sp, n);
}
#else
#define OGPU_MOVE16 0
#endif

/* n words of wv from q */
PX_FN void fill_words(ogpu_w32 *q, ogpu_w32 wv, int n) {
    while (n >= 4) { q[0] = wv; q[1] = wv; q[2] = wv; q[3] = wv; q += 4; n -= 4; }
    while (n--) *q++ = wv;
}

/* The draw modes of a 1-bit source (TEMPLATE, PATTERN, LINE), worked out
 * once per command. */
struct pen { ogpu_u8 fg[4], bg[4], flip[4]; int bpp, mode; };

static void pen_init(struct pen *p, int format, ogpu_u32 fg, ogpu_u32 bg, int mode) {
    raw_bytes(fg, format, p->fg);
    raw_bytes(bg, format, p->bg);
    flip_bytes(format, p->flip);
    p->bpp = bytes_per_pixel(format);
    p->mode = mode;
}

PX_FN void plot_bit(ogpu_u8 *p, const struct pen *pn, int bit) {
    if (pn->mode & OGPU_INVERSVID) bit = !bit;
    if (pn->mode & OGPU_COMPLEMENT) {
        if (bit) xor_raw(p, pn->bpp, pn->flip);
    } else if (bit) {
        put_raw(p, pn->bpp, pn->fg);
    } else if (pn->mode & OGPU_JAM2) {
        put_raw(p, pn->bpp, pn->bg);
    }
}

/* ---- blending --------------------------------------------------------------------- */

/* v1.1's operators: put source s on dp with coverage ea, by OVER, ADD or IN
 * (COMPOSITE's flags). */
PX_FN void blend11(ogpu_u8 *dp, int f, const struct ch *s, unsigned ea, ogpu_u32 flags) {
    struct ch d;
    unsigned r, g, b, oa, ia;
    if (flags & OGPU_COMP_IN) {
        ld(dp, f, &d);
        r = mul255(d.r, ea); g = mul255(d.g, ea); b = mul255(d.b, ea); oa = mul255(d.a, ea);
    } else if (flags & OGPU_COMP_ADD) {
        if (!ea) return;
        ld(dp, f, &d);
        r = d.r + mul255(s->r, ea); g = d.g + mul255(s->g, ea); b = d.b + mul255(s->b, ea); oa = d.a + ea;
    } else {
        if (ea == 255) { st(dp, f, 255, s->r, s->g, s->b); return; }
        if (!ea) return;
        ld(dp, f, &d);
        ia = 255 - ea;
        r = mul255(s->r, ea) + mul255(d.r, ia);
        g = mul255(s->g, ea) + mul255(d.g, ia);
        b = mul255(s->b, ea) + mul255(d.b, ia);
        oa = ea + mul255(d.a, ia);
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (oa > 255) oa = 255;
    st(dp, f, oa, r, g, b);
}

/* v1.2's blend modes (SDL's): source s with alpha sa on dp. */
PX_FN void blend12(ogpu_u8 *dp, int f, const struct ch *s, unsigned sa, int mode) {
    struct ch d;
    unsigned r, g, b, oa, ia;
    switch (mode) {
    case OGPU_BLEND_NONE:
        st(dp, f, sa, s->r, s->g, s->b);
        return;
    case OGPU_BLEND_ADD:
        if (!sa) return;
        ld(dp, f, &d);
        r = d.r + mul255(s->r, sa); g = d.g + mul255(s->g, sa); b = d.b + mul255(s->b, sa); oa = d.a;
        break;
    case OGPU_BLEND_MOD:
        ld(dp, f, &d);
        r = mul255(s->r, d.r); g = mul255(s->g, d.g); b = mul255(s->b, d.b); oa = d.a;
        break;
    case OGPU_BLEND_MUL:
        ld(dp, f, &d);
        ia = 255 - sa;
        r = mul255(s->r, d.r) + mul255(d.r, ia);
        g = mul255(s->g, d.g) + mul255(d.g, ia);
        b = mul255(s->b, d.b) + mul255(d.b, ia);
        oa = d.a;
        break;
    case OGPU_BLEND_BLEND_PREMUL:       /* v1.7: s's colour is premultiplied (not times sa) */
        if (sa == 255) { st(dp, f, 255, s->r, s->g, s->b); return; }
        ld(dp, f, &d);
        ia = 255 - sa;
        r = s->r + mul255(d.r, ia); g = s->g + mul255(d.g, ia); b = s->b + mul255(d.b, ia);
        oa = sa + mul255(d.a, ia);
        break;
    case OGPU_BLEND_ADD_PREMUL:         /* v1.7 */
        ld(dp, f, &d);
        r = d.r + s->r; g = d.g + s->g; b = d.b + s->b; oa = d.a;
        break;
    default:            /* OGPU_BLEND_BLEND */
        if (sa == 255) { st(dp, f, 255, s->r, s->g, s->b); return; }
        if (!sa) return;
        ld(dp, f, &d);
        ia = 255 - sa;
        r = mul255(s->r, sa) + mul255(d.r, ia);
        g = mul255(s->g, sa) + mul255(d.g, ia);
        b = mul255(s->b, sa) + mul255(d.b, ia);
        oa = sa + mul255(d.a, ia);
        break;
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (oa > 255) oa = 255;
    st(dp, f, oa, r, g, b);
}

/* An ARGB32 colour word as channels. */
static void ch_of(ogpu_u32 v, struct ch *o) {
    ogpu_u8 b[4];
    bytes_of(v, b);
    o->a = b[0]; o->r = b[1]; o->g = b[2]; o->b = b[3];
}

/* ---- the commands ----------------------------------------------------------------- */

OP_FN op_surface(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), address = rd32(a + 4), bpr = rd32(a + 8), wh = rd32(a + 12), format = rd32(a + 16);
    int w = hi_u(wh), h = lo_u(wh), bpp = bytes_per_pixel((int)format);
    struct ogpu_surface *s;
    ogpu_u8 *mem;
    ogpu_u32 len;
    if (slot >= OGPU_MAX_SLOTS) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    s->pixels = 0;
    s->table = 0;                                   /* v1.7: a SURFACE forgets the slot's PALETTE */
    if (!bpp || !w || !h || w > OGPU_MAX_SIZE || h > OGPU_MAX_SIZE
        || bpr < (ogpu_u32)w * bpp || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w * bpp, &len))
        return OGPU_ERR_UNSUPPORTED;
    mem = c->map(c->user, address, len);
    if (!mem) return OGPU_ERR_NOMAP;
    s->pixels = mem; s->bpr = (long)bpr; s->w = w; s->h = h; s->format = (int)format;
    if (c->target == (int)slot) { c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; }
    return OGPU_OK;
}

/* PALETTE (v1.7): an INDEX8 slot's colours, 256 ARGB32 words, read
 * whenever the slot is drawn from; table 0 forgets them. */
OP_FN op_palette(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), table = rd32(a + 4);
    struct ogpu_surface *s;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    s->table = 0;
    if (s->format != OGPU_FMT_INDEX8) return OGPU_ERR_UNSUPPORTED;
    if (!table) return OGPU_OK;
    if (!(s->table = c->map(c->user, table, 1024))) return OGPU_ERR_NOMAP;
    return OGPU_OK;
}

OP_FN op_fill(struct ogpu_core *c, const ogpu_u8 *a, int invert) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), v = rd32(a + 8);
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    const struct ogpu_surface *t = &c->slot[c->target];
    int bpp = bytes_per_pixel(t->format);
    ogpu_u8 pv[4], m[4];
    ogpu_u8 *row;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    raw_bytes(v, t->format, pv);
    if (invert) {
        flip_bytes(t->format, m);
        if (bpp == 4 && t->format != OGPU_FMT_XRGB32)       /* INVERT's mask reaches the alpha too, */
            m[0] = m[1] = m[2] = m[3] = 0xFF;               /* but XRGB32's X stays 0 */
        for (i = 0; i < 4; i++) pv[i] &= m[i];
    }
    row = px_at(t, x, y);
    for (j = 0; j < h; j++, row += t->bpr) {
        ogpu_u8 *p = row;
        if (invert) {
            if (bpp == 4) { ogpu_u8 b1 = pv[1], b2 = pv[2], b3 = pv[3], b0 = pv[0];
                for (i = 0; i < w; i++, p += 4) { p[0] ^= b0; p[1] ^= b1; p[2] ^= b2; p[3] ^= b3; } }
            else if (bpp == 2) { ogpu_u8 b0 = pv[0], b1 = pv[1];
                for (i = 0; i < w; i++, p += 2) { p[0] ^= b0; p[1] ^= b1; } }
            else { ogpu_u8 b0 = pv[0]; for (i = 0; i < w; i++) p[i] ^= b0; }
        } else if (bpp == 4) {
            ogpu_u8 b0 = pv[0], b1 = pv[1], b2 = pv[2], b3 = pv[3];
            if (aligned4(p)) {
                fill_words((ogpu_w32 *)p, word_of(pv), w);
            } else
                for (i = 0; i < w; i++, p += 4) { p[0] = b0; p[1] = b1; p[2] = b2; p[3] = b3; }
        } else if (bpp == 2) {
            ogpu_u8 b0 = pv[0], b1 = pv[1];
            i = 0;
            if (((unsigned long)p & 1) == 0) {
                if (!aligned4(p) && w) { p[0] = b0; p[1] = b1; p += 2; i = 1; }
                if (i + 2 <= w) {
                    ogpu_u8 q4[4];
                    int k = (w - i) >> 1;
                    q4[0] = b0; q4[1] = b1; q4[2] = b0; q4[3] = b1;
                    fill_words((ogpu_w32 *)p, word_of(q4), k);
                    i += k << 1; p += k << 2;
                }
            }
            for (; i < w; i++, p += 2) { p[0] = b0; p[1] = b1; }
        } else {
            ogpu_u8 b0 = pv[0];
            for (i = 0; i < w; i++) p[i] = b0;
        }
    }
    return OGPU_OK;
}

OP_FN op_copy(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), dxy = rd32(a + 8), wh = rd32(a + 12);
    const struct ogpu_surface *t = &c->slot[c->target], *s;
    int sx = hi_s(sxy), sy = lo_s(sxy), x = hi_s(dxy), y = lo_s(dxy), w = hi_u(wh), h = lo_u(wh);
    int ox, oy, bpp, row, j, n;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    if (s->format != t->format) return OGPU_ERR_UNSUPPORTED;
    /* Keep the source inside its surface, moving the destination with it. */
    if (sx < 0) { x -= sx; w += sx; sx = 0; }
    if (sy < 0) { y -= sy; h += sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return OGPU_OK;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    sx += ox; sy += oy;
    bpp = bytes_per_pixel(t->format);
    n = w * bpp;
    for (row = 0; row < h; row++) {
        /* Overlap (one surface, or two slots on the same memory): rows bottom
         * up when the destination is later in memory, bytes backwards when
         * a row's destination starts inside its source. */
        int r = px_at(t, x, y) > px_at(s, sx, sy) ? h - 1 - row : row;
        const ogpu_u8 *sp = px_at(s, sx, sy + r);
        ogpu_u8 *dp = px_at(t, x, y + r);
#if OGPU_HOST
        (void)j;
        memmove(dp, sp, (size_t)n);         /* overlap safe, the same bytes as the loops */
#else
        if (dp > sp && dp < sp + n) { for (j = n - 1; j >= 0; j--) dp[j] = sp[j]; }
#if OGPU_MOVE16
        else if (n >= 64 && c->nfast && !(((ogpu_u32)dp ^ (ogpu_u32)sp) & 15) && (dp >= sp + n || dp + n <= sp) &&
                 in_fast(c, dp, n) && in_fast(c, sp, n)) copy_move16(dp, sp, n);
#endif
        else copy_fwd(dp, sp, n);
#endif
    }
    return OGPU_OK;
}

OP_FN op_template(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), first = rd32(a + 8), xy = rd32(a + 12), wh = rd32(a + 16);
    ogpu_u32 fg = rd32(a + 20), bg = rd32(a + 24);
    int mode = (int)rd32(a + 28);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    const ogpu_u8 *src;
    ogpu_u8 *drow;
    struct pen pn;
    if (!w || !h) return OGPU_OK;
    if (first > 0xFFFFUL || bpr > 0xFFFFUL) return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, bpr * (ogpu_u32)(h - 1) + (first + (ogpu_u32)w + 7) / 8);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    pen_init(&pn, t->format, fg, bg, mode);
    src += (long)oy * (long)bpr;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, src += bpr, drow += t->bpr) {
        ogpu_u32 bit = first + (ogpu_u32)ox;
        const ogpu_u8 *sb = src + (bit >> 3);
        unsigned m = 0x80u >> (bit & 7), byte = *sb;    /* once a row */
        ogpu_u8 *p = drow;
        for (i = 0; i < w; i++, p += pn.bpp) {
            plot_bit(p, &pn, (byte & m) != 0);
            if (!(m >>= 1) && i + 1 < w) { m = 0x80; byte = *++sb; }
        }
    }
    return OGPU_OK;
}

OP_FN op_pattern(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), rows = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12);
    ogpu_u32 fg = rd32(a + 16), bg = rd32(a + 20);
    int mode = (int)rd32(a + 24);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    const ogpu_u8 *pat;
    ogpu_u8 *drow;
    struct pen pn;
    if (!rows || rows > 256 || (rows & (rows - 1))) return OGPU_ERR_UNSUPPORTED;
    pat = c->map(c->user, address, rows * 2);
    if (!pat) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    pen_init(&pn, t->format, fg, bg, mode);
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, drow += t->bpr) {
        const ogpu_u8 *pw = pat + (((ogpu_u32)(y + j) & (rows - 1)) << 1);
        unsigned bits = ((unsigned)pw[0] << 8) | pw[1];
        unsigned m = 0x8000u >> (x & 15);               /* once a row */
        ogpu_u8 *p = drow;
        for (i = 0; i < w; i++, p += pn.bpp) {
            plot_bit(p, &pn, (bits & m) != 0);
            if (!(m >>= 1)) m = 0x8000u;
        }
    }
    return OGPU_OK;
}

/* A Bresenham line from (x0, y0) to (x1, y1), each pixel inside the
 * drawable rectangle given to fn; with skip_last the last is left out. */
typedef void (*plot_fn)(ogpu_u8 *p, const void *arg);

static void walk_line(const struct ogpu_core *c, int x0, int y0, int x1, int y1, int skip_last, plot_fn fn, const void *arg) {
    const struct ogpu_surface *t = &c->slot[c->target];
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int stepx = x0 < x1 ? 1 : -1, stepy = y0 < y1 ? 1 : -1, err = dx - dy;
    int bx0, by0, bx1, by1, bpp = bytes_per_pixel(t->format), inside;
    long px_step = stepx > 0 ? bpp : -bpp, row_step = stepy > 0 ? t->bpr : -t->bpr;
    ogpu_u8 *p = 0;
    bounds(c, &bx0, &by0, &bx1, &by1);
    inside = x0 >= bx0 && y0 >= by0 && x0 < bx1 && y0 < by1;
    if (inside) p = px_at(t, x0, y0);
    for (;;) {
        int last = x0 == x1 && y0 == y1;
        if (inside && !(last && skip_last)) fn(p, arg);
        if (last) break;
        {
            int e2 = 2 * err, mx = 0, my = 0;
            if (e2 > -dy) { err -= dy; x0 += stepx; mx = 1; }
            if (e2 < dx) { err += dx; y0 += stepy; my = 1; }
            if (x0 >= bx0 && y0 >= by0 && x0 < bx1 && y0 < by1) {
                if (inside) { if (mx) p += px_step; if (my) p += row_step; }
                else p = px_at(t, x0, y0);
                inside = 1;
            } else inside = 0;
        }
    }
}

static void plot_pen(ogpu_u8 *p, const void *arg) { plot_bit(p, (const struct pen *)arg, 1); }

OP_FN op_line(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 p0 = rd32(a), p1 = rd32(a + 4), fg = rd32(a + 8);
    int mode = (int)rd32(a + 12) & ~OGPU_INVERSVID;
    struct pen pn;
    pen_init(&pn, c->slot[c->target].format, fg, 0, mode);
    walk_line(c, hi_s(p0), lo_s(p0), hi_s(p1), lo_s(p1), 0, plot_pen, &pn);
    return OGPU_OK;
}

OP_FN op_pixels(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), format = rd32(a + 8), table = rd32(a + 12);
    ogpu_u32 xy = rd32(a + 16), wh = rd32(a + 20);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    int sbpp = bytes_per_pixel((int)format), dbpp = bytes_per_pixel(t->format);
    const ogpu_u8 *src, *ctab = 0;
    ogpu_u8 *drow;
    ogpu_u32 len;
    int raw;
    if (!sbpp || !w || !h) return sbpp ? OGPU_OK : OGPU_ERR_UNSUPPORTED;
    /* A CLUT8 target takes pens only; true-colour targets take anything
     * but bare pens, which have no colours without a table. */
    if (t->format == OGPU_FMT_CLUT8 || t->format == OGPU_FMT_INDEX8) {   /* INDEX8 (v1.7): indexes, as pens */
        if (format != OGPU_FMT_CLUT8 && !(format == OGPU_FMT_INDEX8 && !table)) return OGPU_ERR_UNSUPPORTED;
        raw = 1;
    } else {
        if (format == OGPU_FMT_CLUT8) return OGPU_ERR_UNSUPPORTED;
        /* A8 goes only to A8: coverage has no colour, and colours no coverage. */
        if ((format == OGPU_FMT_A8) != (t->format == OGPU_FMT_A8)) return OGPU_ERR_UNSUPPORTED;
        raw = (int)format == t->format && format != OGPU_FMT_XRGB32;
        if (format == OGPU_FMT_INDEX8) {
            if (!table) return OGPU_ERR_UNSUPPORTED;
            ctab = c->map(c->user, table, 1024);
            if (!ctab) return OGPU_ERR_NOMAP;
        }
    }
    if (bpr < (ogpu_u32)w * sbpp || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w * sbpp, &len))
        return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, len);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    src += (long)oy * (long)bpr + (long)ox * sbpp;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, src += bpr, drow += t->bpr) {
        const ogpu_u8 *sp = src;
        ogpu_u8 *dp = drow;
        if (raw) {
            int n = w * dbpp;
            for (i = 0; i < n; i++) dp[i] = sp[i];
            continue;
        }
        if (t->format == OGPU_FMT_XRGB32 && (format == OGPU_FMT_ARGB32 || format == OGPU_FMT_XRGB32)) {
            /* The colour as it is, X 0: a 32-bit OpenRTG screen's ARGB pixels. */
            for (i = 0; i < w; i++, sp += 4, dp += 4) { dp[0] = 0; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3]; }
            continue;
        }
        for (i = 0; i < w; i++, sp += sbpp, dp += dbpp) {
            struct ch s;
            if (ctab) { const ogpu_u8 *e = ctab + ((unsigned)sp[0] << 2); s.a = e[0]; s.r = e[1]; s.g = e[2]; s.b = e[3]; }
            else ld(sp, (int)format, &s);
            st(dp, t->format, s.a, s.r, s.g, s.b);
        }
    }
    return OGPU_OK;
}

/* One channel of a and b mixed by f/256. */
PX_FN unsigned mix1(unsigned a, unsigned b, unsigned f) {
    return (MUL16(a, 256 - f) + MUL16(b, f)) >> 8;
}
PX_FN void mix(struct ch *o, const struct ch *a, const struct ch *b, unsigned f) {
    o->a = mix1(a->a, b->a, f); o->r = mix1(a->r, b->r, f); o->g = mix1(a->g, b->g, f); o->b = mix1(a->b, b->b, f);
}

/* x * bytes a pixel, by shifts. */
/* Row y of a surface, with a 16-bit multiply when the row fits in one
 * (every Amiga screen's does). */
PX_FN ogpu_u8 *row_at(const struct ogpu_surface *s, int y) {
    if (s->bpr <= 0xFFFF) return s->pixels + MUL16(y, s->bpr);
    return s->pixels + (long)y * s->bpr;
}

PX_FN long xoff(int x, int bpp) { return bpp == 4 ? (long)x << 2 : bpp == 2 ? (long)x << 1 : (long)x; }

/* A source's pixel (x, y) of a rectangle whose row pointers are known; an
 * INDEX8 slot's through its PALETTE (v1.7; callers check it has one). */
PX_FN void sample_at(const struct ogpu_surface *s, const ogpu_u8 *row, int x, struct ch *o) {
    if (s->format == OGPU_FMT_INDEX8) {
        const ogpu_u8 *e = s->table + ((unsigned)row[x] << 2);
        o->a = e[0]; o->r = e[1]; o->g = e[2]; o->b = e[3];
        return;
    }
    ld(row + xoff(x, fmt_bpp[s->format]), s->format, o);
}

/* A source a colour command reads: colour, A8 where it says so, or (v1.7)
 * INDEX8 with a palette. */
static int colour_source(const struct ogpu_surface *s, int a8) {
    return is_colour(s->format) || (a8 && s->format == OGPU_FMT_A8) || (s->format == OGPU_FMT_INDEX8 && s->table);
}

/* PIXELART (v1.7) along one axis: p, the pixel's centre (16.16 texels
 * from the rectangle's corner); b16, the texels a pixel spans (16.16, 256 to
 * 65536) and rb = 2^24 / b16. Returns n = floor(p - b / 2), the first of the
 * texel pair (unclamped), and in *f the weight of n + 1 (0-256):
 * smoothstep(1 - b, 1, fract(p - b / 2)), worked in 1/4096ths. 32-bit
 * products, as only PIXELART pays for them. */
PX_FN int pa_axis(long p, ogpu_u32 b16, ogpu_u32 rb, unsigned *f) {
    long t = p - (long)(b16 >> 1);
    long sv = (long)((ogpu_u32)t & 0xFFFFUL) - 65536L + (long)b16;    /* fract(t) - (1 - b) */
    if (sv <= 0) *f = 0;
    else if (sv >= (long)b16) *f = 256;
    else {
        ogpu_u32 tt = ((ogpu_u32)sv * rb) >> 12;                /* sv < b16: the product is below 2^24 */
        ogpu_u32 q = (tt * tt) >> 12;
        *f = (unsigned)((q * (12288UL - (tt << 1))) >> 16);
    }
    return (int)(t >> 16);
}

/* The texels a target pixel spans one way (16.16), as PIXELART's b16 and rb. */
static void pa_box(ogpu_u32 span, ogpu_u32 *b16, ogpu_u32 *rb) {
    if (span < 256) span = 256;
    if (span > 65536UL) span = 65536UL;
    *b16 = span;
    *rb = 0x1000000UL / span;
}

/* PIXELART's pixel centres one way (16.16): exact at every pixel, a
 * remainder carried, as its steep edges show a truncated 16.16 step's drift
 * across a wide picture. Centre i of n pixels over size texels is
 * (2i + 1) * size * 65536 / 2n. Past 32767 pixels, the plain step. */
struct pa_walk { ogpu_u32 p, e, step, inc, den; };

static void pa_start(struct pa_walk *w, ogpu_u32 size, ogpu_u32 n, ogpu_u32 first) {
    ogpu_u32 big = size << 16, extra;
    w->step = big / n;
    if (n > 32767) { w->p = first * w->step + (w->step >> 1); w->e = 0; w->inc = 0; w->den = 1; return; }
    w->den = n << 1;
    w->inc = (big % n) << 1;                            /* below den */
    extra = big % w->den + first * w->inc;              /* below 2n * n: 32 bits */
    w->p = big / w->den + first * w->step + extra / w->den;
    w->e = extra % w->den;
}

PX_FN void pa_next(struct pa_walk *w) {
    w->p += w->step;
    w->e += w->inc;
    if (w->e >= w->den) { w->e -= w->den; w->p++; }
}

/* Bilinear between rows r0 and r1 at x0..x1 with weights wx, wy (0-256). */
PX_FN void sample_bi(const struct ogpu_surface *s, const ogpu_u8 *r0, const ogpu_u8 *r1, int x0, int x1,
                      unsigned wx, unsigned wy, struct ch *o) {
    struct ch p00, p10, p01, p11, t0, t1;
    sample_at(s, r0, x0, &p00); sample_at(s, r0, x1, &p10);
    sample_at(s, r1, x0, &p01); sample_at(s, r1, x1, &p11);
    mix(&t0, &p00, &p10, wx);
    mix(&t1, &p01, &p11, wx);
    mix(o, &t0, &t1, wy);
}

OP_FN op_composite(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), swh = rd32(a + 8), dxy = rd32(a + 12), dwh = rd32(a + 16);
    ogpu_u32 alpha = rd32(a + 20) & 255, flags = rd32(a + 24);
    const struct ogpu_surface *t = &c->slot[c->target], *s, *m = 0;
    int sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh);
    int x = hi_s(dxy), y = lo_s(dxy), w = hi_u(dwh), h = lo_u(dwh), ox, oy, i, j, dbpp, mx = 0, my = 0;
    int mode = flags & OGPU_COMP_BLENDMODE ? OGPU_COMP_MODE_OF(flags) : -1, fast, srcalpha;
    int pixelart = (flags & OGPU_COMP_PIXELART) != 0, premul = mode == OGPU_BLEND_BLEND_PREMUL || mode == OGPU_BLEND_ADD_PREMUL;
    ogpu_u32 pbx = 0, prx = 0, pby = 0, pry = 0;
    ogpu_u32 stepx, stepy;
    struct pa_walk wx0 = { 0, 0, 0, 0, 1 }, wxr = wx0, wyr = wx0;
    int dw = w, dh = h;
    ogpu_u8 *drow;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    if (!is_colour(t->format) && t->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
    if (!colour_source(s, 1)) return OGPU_ERR_UNSUPPORTED;
    if ((flags & OGPU_COMP_ADD) && (flags & OGPU_COMP_IN)) return OGPU_ERR_UNSUPPORTED;
    if (pixelart && (flags & OGPU_COMP_BILINEAR)) return OGPU_ERR_UNSUPPORTED;
    if (mode > OGPU_BLEND_LAST) return OGPU_ERR_UNSUPPORTED;
    if (flags & OGPU_COMP_MASK) {
        ogpu_u32 mslot = rd32(a + 28), mxy = rd32(a + 32);
        if (mslot >= OGPU_MAX_SLOTS || !c->slot[mslot].pixels) return OGPU_ERR_NOSURFACE;
        m = &c->slot[mslot];
        if (m->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        mx = hi_s(mxy); my = lo_s(mxy);
        if (mx < 0 || my < 0 || mx + w > m->w || my + h > m->h) return OGPU_ERR_UNSUPPORTED;
    }
    if (!sw || !sh || !w || !h) return OGPU_OK;
    if (sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return OGPU_ERR_UNSUPPORTED;
    stepx = ((ogpu_u32)sw << 16) / (ogpu_u32)w;         /* once a command */
    stepy = ((ogpu_u32)sh << 16) / (ogpu_u32)h;
    if (pixelart) { pa_box(stepx, &pbx, &prx); pa_box(stepy, &pby, &pry); }
    dbpp = bytes_per_pixel(t->format);
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    if (pixelart) { pa_start(&wx0, (ogpu_u32)sw, (ogpu_u32)dw, (ogpu_u32)ox); pa_start(&wyr, (ogpu_u32)sh, (ogpu_u32)dh, (ogpu_u32)oy); }
    fast = s->format == OGPU_FMT_ARGB32 && (t->format == OGPU_FMT_ARGB32 || t->format == OGPU_FMT_RGB565)
           && mode < 0 && !m && !(flags & (OGPU_COMP_ADD | OGPU_COMP_IN | OGPU_COMP_BILINEAR | OGPU_COMP_PIXELART));
    srcalpha = (flags & OGPU_COMP_SRCALPHA) != 0;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, drow += t->bpr) {
        ogpu_u32 py = (ogpu_u32)(oy + j) * stepy + (stepy >> 1);    /* 16.16, the pixel's centre */
        ogpu_u32 px = (ogpu_u32)ox * stepx + (stepx >> 1);
        ogpu_u8 *dp = drow;
        const ogpu_u8 *mp = m ? px_at(m, mx + ox, my + oy + j) : 0;
        const ogpu_u8 *r0, *r1;
        int y0, y1;
        unsigned wy = 0;
        if (flags & OGPU_COMP_BILINEAR) {
            long fy = (long)py - 0x8000L;
            if (fy < 0) fy = 0;
            y0 = (int)(fy >> 16); wy = (unsigned)(fy >> 8) & 255;
            if (y0 >= sh - 1) { y0 = sh - 1; wy = 0; }
            y1 = y0 + 1 < sh ? y0 + 1 : y0;
        } else if (pixelart) {
            y0 = pa_axis((long)wyr.p, pby, pry, &wy);
            pa_next(&wyr);
            wxr = wx0;
            y1 = y0 + 1;
            if (y0 < 0) y0 = 0;
            if (y0 > sh - 1) y0 = sh - 1;
            if (y1 < 0) y1 = 0;
            if (y1 > sh - 1) y1 = sh - 1;
        } else {
            y0 = (int)(py >> 16);
            if (y0 >= sh) y0 = sh - 1;
            y1 = y0;
        }
        r0 = px_at(s, 0, sy + y0);                      /* once a row */
        r1 = y1 == y0 ? r0 : px_at(s, 0, sy + y1);
        if (fast) {
            /* ARGB32 over ARGB32 or RGB565, nearest, OVER: the common case, without the
             * general reads and writes. */
            const ogpu_u8 *srow = r0 + xoff(sx, 4);
            for (i = 0; i < w; i++, dp += dbpp, px += stepx) {
                int nx = (int)(px >> 16);
                const ogpu_u8 *q;
                unsigned ea, ia, r, g, b;
                if (nx >= sw) nx = sw - 1;
                q = srow + xoff(nx, 4);
                ea = mul255(srcalpha ? q[0] : 255, alpha);
                if (ea == 255) { r = q[1]; g = q[2]; b = q[3]; if (dbpp == 4) dp[0] = 255; }
                else if (!ea) continue;
                else {
                    ia = 255 - ea;
                    if (dbpp == 4) {
                        r = mul255(q[1], ea) + mul255(dp[1], ia);
                        g = mul255(q[2], ea) + mul255(dp[2], ia);
                        b = mul255(q[3], ea) + mul255(dp[3], ia);
                        { unsigned oa = ea + mul255(dp[0], ia); dp[0] = (ogpu_u8)(oa > 255 ? 255 : oa); }
                    } else {
                        struct ch d;
                        ld(dp, OGPU_FMT_RGB565, &d);
                        r = mul255(q[1], ea) + mul255(d.r, ia);
                        g = mul255(q[2], ea) + mul255(d.g, ia);
                        b = mul255(q[3], ea) + mul255(d.b, ia);
                    }
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
                if (dbpp == 4) { dp[1] = (ogpu_u8)r; dp[2] = (ogpu_u8)g; dp[3] = (ogpu_u8)b; }
                else { dp[0] = (ogpu_u8)((r & 0xF8) | (g >> 5)); dp[1] = (ogpu_u8)(((g & 0x1C) << 3) | (b >> 3)); }
            }
            continue;
        }
        for (i = 0; i < w; i++, dp += dbpp, px += stepx) {
            struct ch sp;
            unsigned ea;
            if (flags & OGPU_COMP_BILINEAR) {
                long fx = (long)px - 0x8000L;
                int x0, x1;
                unsigned wx;
                if (fx < 0) fx = 0;
                x0 = (int)(fx >> 16); wx = (unsigned)(fx >> 8) & 255;
                if (x0 >= sw - 1) { x0 = sw - 1; wx = 0; }
                x1 = x0 + 1 < sw ? x0 + 1 : x0;
                sample_bi(s, r0, r1, sx + x0, sx + x1, wx, wy, &sp);
            } else if (pixelart) {
                unsigned wx;
                int x0 = pa_axis((long)wxr.p, pbx, prx, &wx), x1 = x0 + 1;
                pa_next(&wxr);
                if (x0 < 0) x0 = 0;
                if (x0 > sw - 1) x0 = sw - 1;
                if (x1 < 0) x1 = 0;
                if (x1 > sw - 1) x1 = sw - 1;
                sample_bi(s, r0, r1, sx + x0, sx + x1, wx, wy, &sp);
            } else {
                int nx = (int)(px >> 16);
                if (nx >= sw) nx = sw - 1;
                sample_at(s, r0, sx + nx, &sp);
            }
            ea = mul255((flags & OGPU_COMP_SRCALPHA) ? sp.a : 255, alpha);
            if (mp) {
                ea = mul255(ea, mp[i]);
                if (premul) { sp.r = mul255(sp.r, mp[i]); sp.g = mul255(sp.g, mp[i]); sp.b = mul255(sp.b, mp[i]); }
            }
            if (mode >= 0) blend12(dp, t->format, &sp, ea, mode);
            else blend11(dp, t->format, &sp, ea, flags);
        }
    }
    return OGPU_OK;
}

/* MASK (v1.1): one ARGB32 colour drawn OVER the target through an A8 mask
 * in memory, as anti-aliased glyphs are. */
OP_FN op_mask(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12), colour = rd32(a + 16);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, dbpp = bytes_per_pixel(t->format);
    ogpu_u32 len;
    const ogpu_u8 *src;
    ogpu_u8 *drow;
    struct ch col;
    if (t->format == OGPU_FMT_CLUT8 || t->format == OGPU_FMT_INDEX8) return OGPU_ERR_UNSUPPORTED;
    if (!w || !h) return OGPU_OK;
    if (bpr < (ogpu_u32)w || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w, &len)) return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, len);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    ch_of(colour, &col);
    src += (long)oy * (long)bpr + ox;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, src += bpr, drow += t->bpr) {
        ogpu_u8 *dp = drow;
        for (i = 0; i < w; i++, dp += dbpp)
            blend11(dp, t->format, &col, mul255(src[i], col.a), 0);
    }
    return OGPU_OK;
}

/* ---- v1.2: SDL 2's renderer ------------------------------------------------------- */

/* FILL_BLEND: an ARGB32 colour over a rectangle by a blend mode. */
OP_FN op_fill_blend(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), colour = rd32(a + 8);
    int mode = (int)(rd32(a + 12) & OGPU_BLEND_MASK);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, bpp = bytes_per_pixel(t->format);
    struct ch col;
    ogpu_u8 *drow;
    if (!is_colour(t->format) || mode > OGPU_BLEND_LAST) return OGPU_ERR_UNSUPPORTED;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    ch_of(colour, &col);
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, drow += t->bpr) {
        ogpu_u8 *p = drow;
        for (i = 0; i < w; i++, p += bpp) blend12(p, t->format, &col, col.a, mode);
    }
    return OGPU_OK;
}

struct blend_arg { struct ch col; int format, mode; };
static void plot_blend(ogpu_u8 *p, const void *arg) {
    const struct blend_arg *b = arg;
    blend12(p, b->format, &b->col, b->col.a, b->mode);
}

/* LINES_BLEND: lines through points in memory, by a blend mode. */
OP_FN op_lines_blend(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), count = rd32(a + 4), colour = rd32(a + 8), flags = rd32(a + 12);
    struct blend_arg b;
    const ogpu_u8 *pts;
    ogpu_u32 i, step = flags & OGPU_LINES_STRIP ? 1 : 2;
    b.format = c->slot[c->target].format;
    b.mode = (int)(flags & OGPU_BLEND_MASK);
    if (!is_colour(b.format) || b.mode > OGPU_BLEND_LAST) return OGPU_ERR_UNSUPPORTED;
    if (count < 2) return OGPU_OK;
    if (count > 0x00FFFFFFUL) return OGPU_ERR_UNSUPPORTED;
    if (!(pts = c->map(c->user, address, count * 4))) return OGPU_ERR_NOMAP;
    ch_of(colour, &b.col);
    for (i = 0; i + 1 < count; i += step) {
        ogpu_u32 p0 = rd32(pts + i * 4), p1 = rd32(pts + i * 4 + 4);
        walk_line(c, hi_s(p0), lo_s(p0), hi_s(p1), lo_s(p1), !(flags & OGPU_LINES_LAST), plot_blend, &b);
    }
    return OGPU_OK;
}

/* POINTS_BLEND: single pixels at points in memory. */
OP_FN op_points_blend(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), count = rd32(a + 4), colour = rd32(a + 8);
    int mode = (int)(rd32(a + 12) & OGPU_BLEND_MASK);
    const struct ogpu_surface *t = &c->slot[c->target];
    const ogpu_u8 *pts;
    struct ch col;
    int x0, y0, x1, y1;
    ogpu_u32 i;
    if (!is_colour(t->format) || mode > OGPU_BLEND_LAST) return OGPU_ERR_UNSUPPORTED;
    if (!count) return OGPU_OK;
    if (count > 0x00FFFFFFUL) return OGPU_ERR_UNSUPPORTED;
    if (!(pts = c->map(c->user, address, count * 4))) return OGPU_ERR_NOMAP;
    ch_of(colour, &col);
    bounds(c, &x0, &y0, &x1, &y1);
    for (i = 0; i < count; i++) {
        ogpu_u32 p = rd32(pts + i * 4);
        int x = hi_s(p), y = lo_s(p);
        if (x >= x0 && y >= y0 && x < x1 && y < y1) blend12(px_at(t, x, y), t->format, &col, col.a, mode);
    }
    return OGPU_OK;
}

/* 16.16 helpers for COMPOSITE_AFFINE, once a command or a row: 64-bit
 * products, as long long (libgcc's on the 68k, outside the pixel loop). */
typedef long long ogpu_s64;

OP_FN op_composite_affine(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), swh = rd32(a + 8), colour = rd32(a + 36), flags = rd32(a + 40);
    long m00 = (long)rd32(a + 12), m01 = (long)rd32(a + 16), m02 = (long)rd32(a + 20);
    long m10 = (long)rd32(a + 24), m11 = (long)rd32(a + 28), m12 = (long)rd32(a + 32);
    const struct ogpu_surface *t = &c->slot[c->target], *s;
    int sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh), mode = (int)(flags & OGPU_BLEND_MASK);
    int bx0, by0, bx1, by1, x, y, k, dbpp = bytes_per_pixel(t->format), sbpp;
    ogpu_s64 det, i00, i01, i10, i11;
    long minx = 0x7FFFFFFFL, miny = 0x7FFFFFFFL, maxx = -0x7FFFFFFFL, maxy = -0x7FFFFFFFL;
    long usz, vsz;
    struct ch col;
    ogpu_u32 pbu = 0, pru = 0, pbv = 0, prv = 0;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    if (!is_colour(t->format) || !colour_source(s, 0) || mode > OGPU_BLEND_LAST) return OGPU_ERR_UNSUPPORTED;
    if ((flags & OGPU_AFF_PIXELART) && (flags & OGPU_AFF_BILINEAR)) return OGPU_ERR_UNSUPPORTED;
    /* 32-bit words as signed, on any host */
    if (m00 > 0x7FFFFFFFL) m00 -= 0x100000000LL;
    if (m01 > 0x7FFFFFFFL) m01 -= 0x100000000LL;
    if (m02 > 0x7FFFFFFFL) m02 -= 0x100000000LL;
    if (m10 > 0x7FFFFFFFL) m10 -= 0x100000000LL;
    if (m11 > 0x7FFFFFFFL) m11 -= 0x100000000LL;
    if (m12 > 0x7FFFFFFFL) m12 -= 0x100000000LL;
    if (!sw || !sh) return OGPU_OK;
    if (sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return OGPU_ERR_UNSUPPORTED;
    if (m00 >= 0x800000L || m00 <= -0x800000L || m01 >= 0x800000L || m01 <= -0x800000L
        || m10 >= 0x800000L || m10 <= -0x800000L || m11 >= 0x800000L || m11 <= -0x800000L)
        return OGPU_ERR_UNSUPPORTED;                /* scale beyond 127 */
    det = (ogpu_s64)m00 * m11 - (ogpu_s64)m01 * m10;   /* 32.32 */
    if (det == 0) return OGPU_OK;                   /* flat: nothing to draw */
    /* The inverse, 16.16: target offsets to source offsets. */
    i00 = (ogpu_s64)m11 * 4294967296LL / det; i01 = -((ogpu_s64)m01 * 4294967296LL) / det;
    i10 = -((ogpu_s64)m10 * 4294967296LL) / det; i11 = (ogpu_s64)m00 * 4294967296LL / det;
    if (i00 >= 0x40000000LL || i00 <= -0x40000000LL || i01 >= 0x40000000LL || i01 <= -0x40000000LL
        || i10 >= 0x40000000LL || i10 <= -0x40000000LL || i11 >= 0x40000000LL || i11 <= -0x40000000LL)
        return OGPU_ERR_UNSUPPORTED;                /* shrunk below 1/16384 */
    /* The source's corners on the target bound what can be drawn. */
    for (k = 0; k < 4; k++) {
        long u = k & 1 ? (long)sw << 16 : 0, v = k & 2 ? (long)sh << 16 : 0;
        long tx = (long)(((ogpu_s64)m00 * u + (ogpu_s64)m01 * v) / 65536) + m02;
        long ty = (long)(((ogpu_s64)m10 * u + (ogpu_s64)m11 * v) / 65536) + m12;
        if (tx < minx) minx = tx;
        if (tx > maxx) maxx = tx;
        if (ty < miny) miny = ty;
        if (ty > maxy) maxy = ty;
    }
    bounds(c, &bx0, &by0, &bx1, &by1);
    if ((minx >> 16) > bx0) bx0 = (int)(minx >> 16);
    if ((miny >> 16) > by0) by0 = (int)(miny >> 16);
    if ((maxx >> 16) + 1 < bx1) bx1 = (int)(maxx >> 16) + 1;
    if ((maxy >> 16) + 1 < by1) by1 = (int)(maxy >> 16) + 1;
    if (bx1 <= bx0 || by1 <= by0) return OGPU_OK;
    ch_of(colour, &col);
    sbpp = bytes_per_pixel(s->format);
    (void)sbpp;
    usz = (long)sw << 16; vsz = (long)sh << 16;
    if (flags & OGPU_AFF_PIXELART) {
        /* the texels one target pixel spans each way, as SDL's fwidth(): |du/dx| + |du/dy| */
        ogpu_s64 su = (i00 < 0 ? -i00 : i00) + (i01 < 0 ? -i01 : i01), sv = (i10 < 0 ? -i10 : i10) + (i11 < 0 ? -i11 : i11);
        pa_box(su > 0x7FFFFFFFLL ? 0x7FFFFFFFUL : (ogpu_u32)su, &pbu, &pru);
        pa_box(sv > 0x7FFFFFFFLL ? 0x7FFFFFFFUL : (ogpu_u32)sv, &pbv, &prv);
    }
    for (y = by0; y < by1; y++) {
        /* The first pixel's centre, in source offsets, exactly; then steps. */
        ogpu_s64 dx = ((ogpu_s64)bx0 << 16) + 0x8000 - m02, dy = ((ogpu_s64)y << 16) + 0x8000 - m12;
        long u = (long)((i00 * dx + i01 * dy) >> 16), v = (long)((i10 * dx + i11 * dy) >> 16);
        long du = (long)i00, dv = (long)i10;
        ogpu_u8 *dp = px_at(t, bx0, y);
        for (x = bx0; x < bx1; x++, dp += dbpp, u += du, v += dv) {
            struct ch sp;
            unsigned sa;
            if (u < 0 || v < 0 || u >= usz || v >= vsz) continue;
            if (flags & OGPU_AFF_BILINEAR) {
                long fu = u - 0x8000L, fv = v - 0x8000L;
                int x0, y0, x1, y1;
                unsigned wx, wy;
                const ogpu_u8 *r0;
                if (fu < 0) fu = 0;
                if (fv < 0) fv = 0;
                x0 = (int)(fu >> 16); wx = (unsigned)(fu >> 8) & 255;
                y0 = (int)(fv >> 16); wy = (unsigned)(fv >> 8) & 255;
                if (x0 >= sw - 1) { x0 = sw - 1; wx = 0; }
                if (y0 >= sh - 1) { y0 = sh - 1; wy = 0; }
                x1 = x0 + 1 < sw ? x0 + 1 : x0;
                y1 = y0 + 1 < sh ? y0 + 1 : y0;
                r0 = row_at(s, sy + y0);
                sample_bi(s, r0, y1 == y0 ? r0 : r0 + s->bpr, sx + x0, sx + x1, wx, wy, &sp);
            } else if (flags & OGPU_AFF_PIXELART) {
                unsigned wx, wy;
                int x0 = pa_axis(u, pbu, pru, &wx), y0 = pa_axis(v, pbv, prv, &wy), x1 = x0 + 1, y1 = y0 + 1;
                if (x0 < 0) x0 = 0;
                if (x0 > sw - 1) x0 = sw - 1;
                if (x1 > sw - 1) x1 = sw - 1;
                if (x1 < 0) x1 = 0;
                if (y0 < 0) y0 = 0;
                if (y0 > sh - 1) y0 = sh - 1;
                if (y1 > sh - 1) y1 = sh - 1;
                if (y1 < 0) y1 = 0;
                sample_bi(s, row_at(s, sy + y0), row_at(s, sy + y1), sx + x0, sx + x1, wx, wy, &sp);
            } else {
                sample_at(s, row_at(s, sy + (int)(v >> 16)), sx + (int)(u >> 16), &sp);
            }
            sa = (flags & OGPU_AFF_SRCALPHA) ? mul255(sp.a, col.a) : col.a;
            if (col.r != 255) sp.r = mul255(sp.r, col.r);
            if (col.g != 255) sp.g = mul255(sp.g, col.g);
            if (col.b != 255) sp.b = mul255(sp.b, col.b);
            blend12(dp, t->format, &sp, sa, mode);
        }
    }
    return OGPU_OK;
}

static unsigned clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : (unsigned)v; }

/* A colour space's coefficients, 8.8: Y's scale, V into R, U and V out of
 * G, U into B, and Y's offset. BT.601, BT.709 and BT.2020, video range
 * (16-235) or full; JPEG is BT.601's full range. */
static void yuv_space(int space, int *ky, int *kr, int *kgu, int *kgv, int *kb, int *yoff) {
    static const short k[6][6] = {
        { 298, 409, 100, 208, 516, 16 },        /* BT601 */
        { 298, 459, 55, 136, 541, 16 },         /* BT709 */
        { 256, 359, 88, 183, 454, 0 },          /* JPEG */
        { 256, 403, 48, 120, 475, 0 },          /* BT709_FULL (v1.7) */
        { 298, 430, 48, 167, 548, 16 },         /* BT2020 (v1.7) */
        { 256, 377, 42, 146, 482, 0 },          /* BT2020_FULL (v1.7) */
    };
    const short *q = k[space >= 0 && space <= OGPU_YUV_BT2020_FULL ? space : 0];
    *ky = q[0]; *kr = q[1]; *kgu = q[2]; *kgv = q[3]; *kb = q[4]; *yoff = q[5];
}

/* P010's sample (a big-endian word, the value in its top 10 bits) as 8 bits, rounded. */
PX_FN int p010(const ogpu_u8 *q) { int v = q[0] + (q[1] >> 7); return v > 255 ? 255 : v; }

/* YUV: a picture in one of the video layouts, converted 1:1 into the
 * target. Integer coefficients, 8.8 (yuv_space): BT.601, BT.709 or BT.2020,
 * video or full range; P010 (v1.7) is NV12's arithmetic on its samples'
 * top 8 bits, rounded. */
OP_FN op_yuv(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 ya = rd32(a), ybpr = rd32(a + 4), ua = rd32(a + 8), ubpr = rd32(a + 12), va = rd32(a + 16), vbpr = rd32(a + 20);
    ogpu_u32 wh = rd32(a + 24), xy = rd32(a + 28), fmt = rd32(a + 32);
    const struct ogpu_surface *t = &c->slot[c->target];
    int layout = (int)(fmt & 255), space = (int)((fmt >> 8) & 255);
    int w = hi_u(wh), h = lo_u(wh), x = hi_s(xy), y = lo_s(xy), ox, oy, i, j, dbpp = bytes_per_pixel(t->format);
    int ky, kr, kgu, kgv, kb, yoff;
    const ogpu_u8 *yp, *up = 0, *vp = 0;
    ogpu_u32 len;
    ogpu_u8 *drow;
    int packed = layout >= OGPU_YUV_YUY2 && layout <= OGPU_YUV_YVYU, semi = layout == OGPU_YUV_NV12 || layout == OGPU_YUV_NV21;
    int wide = layout == OGPU_YUV_P010, sb = wide ? 2 : 1;     /* v1.7: P010's 16-bit samples */
    if (!is_colour(t->format) || layout < OGPU_YUV_I420 || layout > OGPU_YUV_P010 || space > OGPU_YUV_BT2020_FULL)
        return OGPU_ERR_UNSUPPORTED;
    if (wide) semi = 1;
    if (!w || !h) return OGPU_OK;
    if ((w | h) & 1) return OGPU_ERR_UNSUPPORTED;
    yuv_space(space, &ky, &kr, &kgu, &kgv, &kb, &yoff);
    if (packed) {
        if (ybpr < (ogpu_u32)w * 2 || ybpr > 0x01000000UL || !span_len(ybpr, h, (ogpu_u32)w * 2, &len)) return OGPU_ERR_UNSUPPORTED;
        if (!(yp = c->map(c->user, ya, len))) return OGPU_ERR_NOMAP;
    } else {
        if (ybpr < (ogpu_u32)w * sb || ybpr > 0x01000000UL || !span_len(ybpr, h, (ogpu_u32)w * sb, &len)) return OGPU_ERR_UNSUPPORTED;
        if (!(yp = c->map(c->user, ya, len))) return OGPU_ERR_NOMAP;
        if (semi) {
            if (ubpr < (ogpu_u32)w * sb || ubpr > 0x01000000UL || !span_len(ubpr, h / 2, (ogpu_u32)w * sb, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(up = c->map(c->user, ua, len))) return OGPU_ERR_NOMAP;
        } else {
            if (ubpr < (ogpu_u32)w / 2 || ubpr > 0x01000000UL || !span_len(ubpr, h / 2, (ogpu_u32)w / 2, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(up = c->map(c->user, ua, len))) return OGPU_ERR_NOMAP;
            if (vbpr < (ogpu_u32)w / 2 || vbpr > 0x01000000UL || !span_len(vbpr, h / 2, (ogpu_u32)w / 2, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(vp = c->map(c->user, va, len))) return OGPU_ERR_NOMAP;
        }
    }
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, drow += t->bpr) {
        int sy = oy + j;
        const ogpu_u8 *yr = yp + (long)sy * (long)ybpr;
        const ogpu_u8 *ur = up ? up + (long)(sy >> 1) * (long)ubpr : 0, *vr = vp ? vp + (long)(sy >> 1) * (long)vbpr : 0;
        ogpu_u8 *dp = drow;
        for (i = 0; i < w; i++, dp += dbpp) {
            int sx = ox + i, Y, U, V, cy, r, g, b;
            if (packed) {
                const ogpu_u8 *q = yr + ((long)(sx >> 1) << 2);
                switch (layout) {
                case OGPU_YUV_YUY2: Y = q[(sx & 1) << 1]; U = q[1]; V = q[3]; break;
                case OGPU_YUV_UYVY: Y = q[1 + ((sx & 1) << 1)]; U = q[0]; V = q[2]; break;
                default: Y = q[(sx & 1) << 1]; V = q[1]; U = q[3]; break;      /* YVYU */
                }
            } else if (wide) {
                const ogpu_u8 *q = ur + ((sx >> 1) << 2);
                Y = p010(yr + (sx << 1)); U = p010(q); V = p010(q + 2);
            } else {
                Y = yr[sx];
                if (semi) {
                    const ogpu_u8 *q = ur + ((sx >> 1) << 1);
                    if (layout == OGPU_YUV_NV12) { U = q[0]; V = q[1]; } else { V = q[0]; U = q[1]; }
                } else { U = ur[sx >> 1]; V = vr[sx >> 1]; }
            }
            cy = SMUL16(Y - yoff, ky) + 128;
            U -= 128; V -= 128;
            r = (cy + SMUL16(V, kr)) >> 8;
            g = (cy - SMUL16(U, kgu) - SMUL16(V, kgv)) >> 8;
            b = (cy + SMUL16(U, kb)) >> 8;
            st(dp, t->format, 255, clamp255(r), clamp255(g), clamp255(b));
        }
    }
    return OGPU_OK;
}

/* YUV_SCALED (v1.5, Open, 22 Oct 2026): a part of a YUV picture converted
 * straight into a destination rect of another size, nearest, so a video
 * frame that is shown at its size or smaller is converted once, into the
 * target, with no ARGB32 copy in between. The arithmetic per pixel is
 * YUV's; source pixels are picked as COMPOSITE's nearest picks them (the
 * centre of each destination pixel, 16.16). */
OP_FN op_yuv_scaled(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 ya = rd32(a), ybpr = rd32(a + 4), ua = rd32(a + 8), ubpr = rd32(a + 12), va = rd32(a + 16), vbpr = rd32(a + 20);
    ogpu_u32 pwh = rd32(a + 24), fmt = rd32(a + 28), sxy = rd32(a + 32), swh = rd32(a + 36), dxy = rd32(a + 40), dwh = rd32(a + 44);
    const struct ogpu_surface *t = &c->slot[c->target];
    int layout = (int)(fmt & 255), space = (int)((fmt >> 8) & 255);
    int pw = hi_u(pwh), ph = lo_u(pwh), sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh);
    int x = hi_s(dxy), y = lo_s(dxy), w = hi_u(dwh), h = lo_u(dwh), ox, oy, i, j, dbpp = bytes_per_pixel(t->format);
    int ky, kr, kgu, kgv, kb, yoff;
    const ogpu_u8 *yp, *up = 0, *vp = 0;
    ogpu_u32 len, stepx, stepy, fx0, fy;
    ogpu_u8 *drow;
    int packed = layout >= OGPU_YUV_YUY2 && layout <= OGPU_YUV_YVYU, semi = layout == OGPU_YUV_NV12 || layout == OGPU_YUV_NV21;
    int wide = layout == OGPU_YUV_P010, sb = wide ? 2 : 1;     /* v1.7: P010's 16-bit samples */
    if (c->target < 0 || !is_colour(t->format) || layout < OGPU_YUV_I420 || layout > OGPU_YUV_P010 || space > OGPU_YUV_BT2020_FULL)
        return OGPU_ERR_UNSUPPORTED;
    if (wide) semi = 1;
    if (!pw || !ph || !sw || !sh || !w || !h) return OGPU_OK;
    if ((pw | ph) & 1) return OGPU_ERR_UNSUPPORTED;
    if (sx < 0 || sy < 0 || sx + sw > pw || sy + sh > ph) return OGPU_ERR_UNSUPPORTED;
    yuv_space(space, &ky, &kr, &kgu, &kgv, &kb, &yoff);
    if (packed) {
        if (ybpr < (ogpu_u32)pw * 2 || ybpr > 0x01000000UL || !span_len(ybpr, ph, (ogpu_u32)pw * 2, &len)) return OGPU_ERR_UNSUPPORTED;
        if (!(yp = c->map(c->user, ya, len))) return OGPU_ERR_NOMAP;
    } else {
        if (ybpr < (ogpu_u32)pw * sb || ybpr > 0x01000000UL || !span_len(ybpr, ph, (ogpu_u32)pw * sb, &len)) return OGPU_ERR_UNSUPPORTED;
        if (!(yp = c->map(c->user, ya, len))) return OGPU_ERR_NOMAP;
        if (semi) {
            if (ubpr < (ogpu_u32)pw * sb || ubpr > 0x01000000UL || !span_len(ubpr, ph / 2, (ogpu_u32)pw * sb, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(up = c->map(c->user, ua, len))) return OGPU_ERR_NOMAP;
        } else {
            if (ubpr < (ogpu_u32)pw / 2 || ubpr > 0x01000000UL || !span_len(ubpr, ph / 2, (ogpu_u32)pw / 2, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(up = c->map(c->user, ua, len))) return OGPU_ERR_NOMAP;
            if (vbpr < (ogpu_u32)pw / 2 || vbpr > 0x01000000UL || !span_len(vbpr, ph / 2, (ogpu_u32)pw / 2, &len)) return OGPU_ERR_UNSUPPORTED;
            if (!(vp = c->map(c->user, va, len))) return OGPU_ERR_NOMAP;
        }
    }
    /* 16.16 steps, once per command; the first pixel's source is at the
     * centre of the destination's first pixel */
    stepx = ((ogpu_u32)sw << 16) / (ogpu_u32)w;
    stepy = ((ogpu_u32)sh << 16) / (ogpu_u32)h;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    fx0 = ((ogpu_u32)sx << 16) + (stepx >> 1) + stepx * (ogpu_u32)ox;
    fy = ((ogpu_u32)sy << 16) + (stepy >> 1) + stepy * (ogpu_u32)oy;
    drow = px_at(t, x, y);
    for (j = 0; j < h; j++, drow += t->bpr, fy += stepy) {
        int py = (int)(fy >> 16);
        const ogpu_u8 *yr = yp + (long)py * (long)ybpr;
        const ogpu_u8 *ur = up ? up + (long)(py >> 1) * (long)ubpr : 0, *vr = vp ? vp + (long)(py >> 1) * (long)vbpr : 0;
        ogpu_u8 *dp = drow;
        ogpu_u32 fx = fx0;
        for (i = 0; i < w; i++, dp += dbpp, fx += stepx) {
            int px = (int)(fx >> 16), Y, U, V, cy, r, g, b;
            if (packed) {
                const ogpu_u8 *q = yr + ((long)(px >> 1) << 2);
                switch (layout) {
                case OGPU_YUV_YUY2: Y = q[(px & 1) << 1]; U = q[1]; V = q[3]; break;
                case OGPU_YUV_UYVY: Y = q[1 + ((px & 1) << 1)]; U = q[0]; V = q[2]; break;
                default: Y = q[(px & 1) << 1]; V = q[1]; U = q[3]; break;      /* YVYU */
                }
            } else if (wide) {
                const ogpu_u8 *q = ur + ((px >> 1) << 2);
                Y = p010(yr + (px << 1)); U = p010(q); V = p010(q + 2);
            } else {
                Y = yr[px];
                if (semi) {
                    const ogpu_u8 *q = ur + ((px >> 1) << 1);
                    if (layout == OGPU_YUV_NV12) { U = q[0]; V = q[1]; } else { V = q[0]; U = q[1]; }
                } else { U = ur[px >> 1]; V = vr[px >> 1]; }
            }
            cy = SMUL16(Y - yoff, ky) + 128;
            U -= 128; V -= 128;
            r = (cy + SMUL16(V, kr)) >> 8;
            g = (cy - SMUL16(U, kgu) - SMUL16(V, kgv)) >> 8;
            b = (cy + SMUL16(U, kb)) >> 8;
            st(dp, t->format, 255, clamp255(r), clamp255(g), clamp255(b));
        }
    }
    return OGPU_OK;
}

/* ---- COMPOSITE_PD (v1.4) ------------------------------------------------------------ */

/* Porter-Duff on premultiplied channels, as stream.h says: each product
 * rounded (mul255), the sum saturating. pixman's own arithmetic, so its
 * results come out the same. */
PX_FN unsigned sat8(unsigned v) { return v > 255 ? 255 : v; }
PX_FN unsigned pd2(unsigned x, unsigned fa, unsigned y, unsigned fb) {
    return sat8(mul255(x, fa) + mul255(y, fb));
}
/* v1.6's helpers that are called per pixel but are too big to copy into
 * each caller: one copy each keeps the library small. */
#if defined(__GNUC__)
#define BIG_FN static __attribute__((noinline))
#else
#define BIG_FN static
#endif

/* ---- v1.6: the W3C blend modes ----------------------------------------------------- */

/* an integer square root, rounded down */
static ogpu_u32 isqrt64(ogpu_s64 sv) {
    ogpu_s64 r = 0, bit = (ogpu_s64)1 << 62;
    if (sv <= 0) return 0;
    while (bit > sv) bit >>= 2;
    while (bit) {
        if (sv >= r + bit) { sv -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (ogpu_u32)r;
}

/* Compositing and Blending Level 1 on premultiplied channels, in units of
 * 1/(255 * 255): T is as ad B(Cd, Cs) for one channel of a separable mode. */
static long bl_sep(int op, long s, long sa, long d, long da) {
    long sada = sa * da, cs, cb, b, q;
    switch (op) {
    case OGPU_BLEND_MULTIPLY: return s * d;
    case OGPU_BLEND_SCREEN: return sa * d + da * s - s * d;
    case OGPU_BLEND_OVERLAY: return 2 * d <= da ? 2 * s * d : sada - 2 * (da - d) * (sa - s);
    case OGPU_BLEND_DARKEN: return s * da < d * sa ? s * da : d * sa;
    case OGPU_BLEND_LIGHTEN: return s * da > d * sa ? s * da : d * sa;
    case OGPU_BLEND_COLOR_DODGE:
        if (!d) return 0;
        if (s >= sa) return sada;
        q = sa * sa * d / (sa - s);
        return q < sada ? q : sada;
    case OGPU_BLEND_COLOR_BURN:
        if (d >= da) return sada;
        if (!s) return 0;
        q = sa * sa * (da - d) / s;
        return q < sada ? sada - q : 0;
    case OGPU_BLEND_HARD_LIGHT: return 2 * s <= sa ? 2 * s * d : sada - 2 * (da - d) * (sa - s);
    case OGPU_BLEND_DIFFERENCE: q = s * da - d * sa; return q < 0 ? -q : q;
    case OGPU_BLEND_EXCLUSION: return sa * d + da * s - 2 * s * d;
    }
    /* soft light, on the colours unpremultiplied, 16.16 */
    if (!sa || !da) return 0;
    cs = (s << 16) / sa; cb = (d << 16) / da;
    if (cs <= 0x8000) b = cb - (long)(((ogpu_s64)(0x10000 - 2 * cs) * cb >> 16) * (0x10000 - cb) >> 16);
    else {
        q = cb <= 0x4000 ? (long)((((((ogpu_s64)16 * cb - 0xC0000) * cb >> 16) + 0x40000) * cb) >> 16)
                         : (long)isqrt64((ogpu_s64)cb * 65536);
        b = cb + (long)((ogpu_s64)(2 * cs - 0x10000) * (q - cb) >> 16);
    }
    return (long)(((ogpu_s64)b * sada + 0x8000) >> 16);
}

/* The non-separable modes' helpers, on three channels scaled by as ad. */
static long bl_lum(const long *c) { return (30 * c[0] + 59 * c[1] + 11 * c[2]) / 100; }
static void bl_setlum(long *c, long a, long l) {
    long n, x;
    int k;
    l -= bl_lum(c);
    for (k = 0; k < 3; k++) c[k] += l;
    l = bl_lum(c); n = x = c[0];
    for (k = 1; k < 3; k++) { if (c[k] < n) n = c[k]; if (c[k] > x) x = c[k]; }
    for (k = 0; k < 3; k++) {       /* ClipColor */
        if (n < 0) c[k] = l + (long)((ogpu_s64)(c[k] - l) * l / (l - n));
        if (x > a) c[k] = l + (long)((ogpu_s64)(c[k] - l) * (a - l) / (x - l));
    }
}
static long bl_sat(const long *c) {
    long n = c[0], x = c[0];
    int k;
    for (k = 1; k < 3; k++) { if (c[k] < n) n = c[k]; if (c[k] > x) x = c[k]; }
    return x - n;
}
static void bl_setsat(long *c, long s) {
    int lo = 0, mid = 1, hi = 2, t;
    if (c[lo] > c[mid]) { t = lo; lo = mid; mid = t; }
    if (c[mid] > c[hi]) { t = mid; mid = hi; hi = t; }
    if (c[lo] > c[mid]) { t = lo; lo = mid; mid = t; }
    if (c[hi] > c[lo]) { c[mid] = (long)((ogpu_s64)(c[mid] - c[lo]) * s / (c[hi] - c[lo])); c[hi] = s; }
    else c[mid] = c[hi] = 0;
    c[lo] = 0;
}

/* d = the blend mode op of s over d (premultiplied). */
static void blend_w3c(struct ch *d, const struct ch *s, int op) {
    long sa = (long)s->a, da = (long)d->a, sc[3], dc[3], t[3], a[3], b[3], v;
    int k;
    sc[0] = (long)s->r; sc[1] = (long)s->g; sc[2] = (long)s->b;
    dc[0] = (long)d->r; dc[1] = (long)d->g; dc[2] = (long)d->b;
    if (op >= OGPU_BLEND_HUE) {
        for (k = 0; k < 3; k++) { a[k] = sc[k] * da; b[k] = dc[k] * sa; }
        switch (op) {
        case OGPU_BLEND_HUE: bl_setsat(a, bl_sat(b)); bl_setlum(a, sa * da, bl_lum(b)); break;
        case OGPU_BLEND_SATURATION: v = bl_lum(b); bl_setsat(b, bl_sat(a)); bl_setlum(b, sa * da, v);
            for (k = 0; k < 3; k++) a[k] = b[k];
            break;
        case OGPU_BLEND_COLOR: bl_setlum(a, sa * da, bl_lum(b)); break;
        default: v = bl_lum(a); for (k = 0; k < 3; k++) a[k] = b[k];
            bl_setlum(a, sa * da, v); break;                                /* LUMINOSITY */
        }
        for (k = 0; k < 3; k++) t[k] = a[k];
    } else
        for (k = 0; k < 3; k++) t[k] = bl_sep(op, sc[k], sa, dc[k], da);
    for (k = 0; k < 3; k++) {
        v = (sc[k] * (255 - da) + dc[k] * (255 - sa) + t[k] + 127) / 255;
        sc[k] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    d->r = (unsigned)sc[0]; d->g = (unsigned)sc[1]; d->b = (unsigned)sc[2];
    d->a = sat8((unsigned)(sa + da) - mul255((unsigned)sa, (unsigned)da));
}


PX_FN void pd_apply(struct ch *d, const struct ch *s, int op) {
    if (op >= OGPU_BLEND_MULTIPLY) { blend_w3c(d, s, op); return; }
    unsigned as = s->a, ad = d->a, fa, fb;
    switch (op) {
    case OGPU_PD_CLEAR: d->a = d->r = d->g = d->b = 0; return;
    case OGPU_PD_SRC: *d = *s; return;
    case OGPU_PD_DST: return;
    case OGPU_PD_ADD:
        d->a = sat8(s->a + d->a); d->r = sat8(s->r + d->r); d->g = sat8(s->g + d->g); d->b = sat8(s->b + d->b);
        return;
    case OGPU_PD_OVER: fa = 255; fb = 255 - as; break;
    case OGPU_PD_OVER_REVERSE: fa = 255 - ad; fb = 255; break;
    case OGPU_PD_IN: fa = ad; fb = 0; break;
    case OGPU_PD_IN_REVERSE: fa = 0; fb = as; break;
    case OGPU_PD_OUT: fa = 255 - ad; fb = 0; break;
    case OGPU_PD_OUT_REVERSE: fa = 0; fb = 255 - as; break;
    case OGPU_PD_ATOP: fa = ad; fb = 255 - as; break;
    case OGPU_PD_ATOP_REVERSE: fa = 255 - ad; fb = as; break;
    default: fa = 255 - ad; fb = 255 - as; break;       /* OGPU_PD_XOR */
    }
    d->a = pd2(s->a, fa, d->a, fb);
    d->r = pd2(s->r, fa, d->r, fb);
    d->g = pd2(s->g, fa, d->g, fb);
    d->b = pd2(s->b, fa, d->b, fb);
}

/* v mod n, for n > 0, never negative. */
static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

OP_FN op_composite_pd(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 of = rd32(a), src = rd32(a + 4), sxy = rd32(a + 8), mask = rd32(a + 12), mxy = rd32(a + 16);
    ogpu_u32 dxy = rd32(a + 20), dwh = rd32(a + 24);
    const struct ogpu_surface *t = &c->slot[c->target], *s = 0, *m = 0;
    int op = (int)(of & 255), x = hi_s(dxy), y = lo_s(dxy), w = hi_u(dwh), h = lo_u(dwh), ox, oy, i, j;
    int sx = hi_s(sxy), sy = lo_s(sxy), mx = hi_s(mxy), my = lo_s(mxy), dbpp, sbpp = 0, mbpp = 0;
    int srep = (of & OGPU_PD_SRC_REPEAT) != 0, mrep = (of & OGPU_PD_MASK_REPEAT) != 0;
    unsigned malpha = 255;
    struct ch solid = { 0, 0, 0, 0 };
    ogpu_u8 *drow;
    if (op >= OGPU_PD_OPS && (op < OGPU_BLEND_MULTIPLY || op > OGPU_BLEND_LUMINOSITY)) return OGPU_ERR_UNSUPPORTED;
    if (!is_colour(t->format) && t->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
    if ((of & OGPU_PD_MASK) && (of & OGPU_PD_MASK_SOLID)) return OGPU_ERR_UNSUPPORTED;
    if (of & OGPU_PD_SOLID) ch_of(src, &solid);
    else {
        if (src >= OGPU_MAX_SLOTS || !c->slot[src].pixels) return OGPU_ERR_NOSURFACE;
        s = &c->slot[src];
        if (!is_colour(s->format) && s->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        sbpp = bytes_per_pixel(s->format);
    }
    if (of & OGPU_PD_MASK) {
        if (mask >= OGPU_MAX_SLOTS || !c->slot[mask].pixels) return OGPU_ERR_NOSURFACE;
        m = &c->slot[mask];
        if (!is_colour(m->format) && m->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        mbpp = bytes_per_pixel(m->format);
    } else if (of & OGPU_PD_MASK_SOLID) malpha = (unsigned)(mask & 255);
    if (!w || !h) return OGPU_OK;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    if (op == OGPU_PD_DST) return OGPU_OK;
    dbpp = bytes_per_pixel(t->format);
    drow = px_at(t, x, y);
    /* The browser's two commonest, without the general reads and writes
     * (the same arithmetic): an ARGB32 image OVER ARGB32 or XRGB32, and a
     * solid colour OVER them through an A8 mask (text). */
    if (op == OGPU_PD_OVER && (t->format == OGPU_FMT_ARGB32 || t->format == OGPU_FMT_XRGB32) && !srep && !mrep &&
        ((s && s->format == OGPU_FMT_ARGB32 && !(of & (OGPU_PD_MASK | OGPU_PD_MASK_SOLID)) &&
          sx + ox >= 0 && sy + oy >= 0 && sx + ox + w <= s->w && sy + oy + h <= s->h) ||
         (!s && m && m->format == OGPU_FMT_A8 && mx + ox >= 0 && my + oy >= 0 && mx + ox + w <= m->w && my + oy + h <= m->h))) {
        int xa = t->format == OGPU_FMT_XRGB32;
#if OGPU_HOST
        /* whole pixels, where every row is on a 4-byte boundary */
        if (aligned4(drow) && !(t->bpr & 3) && (!s || (aligned4(s->pixels) && !(s->bpr & 3)))) {
            ogpu_u8 cb[4];
            ogpu_w32 cw, xmask = xa ? ~LANE_A_MASK : 0xFFFFFFFFu;
            cb[0] = (ogpu_u8)solid.a; cb[1] = (ogpu_u8)solid.r; cb[2] = (ogpu_u8)solid.g; cb[3] = (ogpu_u8)solid.b;
            cw = word_of(cb);
            for (j = 0; j < h; j++, drow += t->bpr) {
                ogpu_w32 *dp = (ogpu_w32 *)drow;
                if (s) {
                    const ogpu_w32 *sp = (const ogpu_w32 *)(row_at(s, sy + oy + j) + xoff(sx + ox, 4));
                    /* no branches: a = 255 gives v, v = 0 gives d, exactly */
                    for (i = 0; i < w; i++) {
                        ogpu_w32 v = sp[i];
                        dp[i] = add_lanes(v, mul_lanes(dp[i], 255 - LANE_A(v))) & xmask;
                    }
                } else {
                    const ogpu_u8 *mp = row_at(m, my + oy + j) + (mx + ox);
                    for (i = 0; i < w; i++) {
                        ogpu_w32 v = mul_lanes(cw, mp[i]);     /* 255 gives cw, 0 gives 0, exactly */
                        dp[i] = add_lanes(v, mul_lanes(dp[i], 255 - LANE_A(v))) & xmask;
                    }
                }
            }
            return OGPU_OK;
        }
#endif
        for (j = 0; j < h; j++, drow += t->bpr) {
            ogpu_u8 *dp = drow;
            if (s) {
                const ogpu_u8 *sp = row_at(s, sy + oy + j) + xoff(sx + ox, 4);
                for (i = 0; i < w; i++, dp += 4, sp += 4) {
                    unsigned sa = sp[0], ia;
                    if (sa == 255) { dp[0] = xa ? 0 : 255; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3]; continue; }
                    if (!(sa | sp[1] | sp[2] | sp[3])) continue;
                    ia = 255 - sa;
                    dp[0] = xa ? 0 : (ogpu_u8)sat8(sa + mul255(dp[0], ia));
                    dp[1] = (ogpu_u8)sat8(sp[1] + mul255(dp[1], ia));
                    dp[2] = (ogpu_u8)sat8(sp[2] + mul255(dp[2], ia));
                    dp[3] = (ogpu_u8)sat8(sp[3] + mul255(dp[3], ia));
                }
            } else {
                const ogpu_u8 *mp = row_at(m, my + oy + j) + (mx + ox);
                for (i = 0; i < w; i++, dp += 4) {
                    unsigned ma = mp[i], ca, cr, cg, cb, ia;
                    if (!ma) continue;
                    if (ma == 255) { ca = solid.a; cr = solid.r; cg = solid.g; cb = solid.b; }
                    else { ca = mul255(solid.a, ma); cr = mul255(solid.r, ma); cg = mul255(solid.g, ma); cb = mul255(solid.b, ma); }
                    if (ca == 255) { dp[0] = xa ? 0 : 255; dp[1] = (ogpu_u8)cr; dp[2] = (ogpu_u8)cg; dp[3] = (ogpu_u8)cb; continue; }
                    if (!(ca | cr | cg | cb)) continue;
                    ia = 255 - ca;
                    dp[0] = xa ? 0 : (ogpu_u8)sat8(ca + mul255(dp[0], ia));
                    dp[1] = (ogpu_u8)sat8(cr + mul255(dp[1], ia));
                    dp[2] = (ogpu_u8)sat8(cg + mul255(dp[2], ia));
                    dp[3] = (ogpu_u8)sat8(cb + mul255(dp[3], ia));
                }
            }
        }
        return OGPU_OK;
    }
    for (j = 0; j < h; j++, drow += t->bpr) {
        ogpu_u8 *dp = drow;
        const ogpu_u8 *srow = 0, *mrow = 0;
        int scol = 0, mcol = 0, sok = 1, mok = 1;
        if (s) {
            int ry = sy + oy + j;
            if (srep) ry = wrap(ry, s->h);
            if (ry >= 0 && ry < s->h) srow = row_at(s, ry); else sok = 0;
            scol = sx + ox;
            if (srep) scol = wrap(scol, s->w);
        }
        if (m) {
            int ry = my + oy + j;
            if (mrep) ry = wrap(ry, m->h);
            if (ry >= 0 && ry < m->h) mrow = row_at(m, ry); else mok = 0;
            mcol = mx + ox;
            if (mrep) mcol = wrap(mcol, m->w);
        }
        for (i = 0; i < w; i++, dp += dbpp) {
            struct ch sp, d;
            unsigned ma = malpha;
            if (s) {
                if (sok && scol >= 0 && scol < s->w) ld(srow + xoff(scol, sbpp), s->format, &sp);
                else sp.a = sp.r = sp.g = sp.b = 0;
                if (++scol == s->w && srep) scol = 0;
            } else sp = solid;
            if (m) {
                if (mok && mcol >= 0 && mcol < m->w) {
                    struct ch mp;
                    ld(mrow + xoff(mcol, mbpp), m->format, &mp);
                    ma = mp.a;
                } else ma = 0;
                if (++mcol == m->w && mrep) mcol = 0;
            }
            if (ma != 255) {
                sp.a = mul255(sp.a, ma); sp.r = mul255(sp.r, ma); sp.g = mul255(sp.g, ma); sp.b = mul255(sp.b, ma);
            }
            if (op == OGPU_PD_OVER && sp.a == 255) {          /* covers: as SRC */
                st(dp, t->format, 255, sp.r, sp.g, sp.b);
                continue;
            }
            if (op == OGPU_PD_OVER && !(sp.a | sp.r | sp.g | sp.b)) continue;
            ld(dp, t->format, &d);
            pd_apply(&d, &sp, op);
            st(dp, t->format, d.a, d.r, d.g, d.b);
        }
    }
    return OGPU_OK;
}

/* v1.6's commands are built for size (GCC): their time is in a few inner
 * loops, and the rest is set-up once a command. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif

/* ---- v1.6: anti-aliased paths ------------------------------------------------------ */

/* OpenFont's rasteriser (openamigafont src/of_raster.c, of_raster_a8),
 * moved here: exact area coverage in integers. Each line adds, to every
 * cell it crosses, how far it climbs there (cover) and that climb times
 * twice its mean place across the cell (area); one sweep along each row
 * then gives each pixel the cover of everything left of it less the part
 * of its own lines' climb left of them. Coordinates are 24.8 (256
 * subpixels a pixel). Here it works on a band of rows at a time, in work
 * memory the command gives, and adds the even-odd rule. Each pixel row is
 * four rows of cells (64 subpixels high), each swept and its winding rule
 * applied alone: area alone can't tell where two pieces overlap inside
 * one pixel (a self-crossing, a stroke's inner join), so a pixel there is
 * wrong by at most what one sub-row holds of it.
 *
 * Team 5, 25 Oct 2026 (for a 68040 without a driver): the path is walked
 * once, into a list of its edges, and each band is drawn from the list;
 * a band's cells are cleared only where the walk writes them (a bit a
 * pixel says which), and the sweep takes the pixels between written ones
 * as one run of the same coverage. The pixels are the same as before. */
#define OGPU_SUBROWS 4
#define OGPU_LIST_BYTES 0x100000UL  /* the most work memory the edge list may take (65536 edges) */
struct rast {
    int *cell;              /* the band's cells: 8 a pixel, its sub-rows' 4 covers, then their 4 areas */
    ogpu_w32 *seen;         /* a bit a pixel (tw words a row, the row's first pixel the top bit): the
                               pixels whose cells the walk wrote; the others read as 0. 0: every cell
                               is cleared before the walk instead */
    int *list;              /* collecting: the walk's edges (x0, y0, x1, y1 on the target, 24.8) */
    long nlist, cap;        /* edges walked, and room for cap of them (past it, only counted) */
    int w, h, eo, tw;       /* eo: the even-odd rule */
    int collect;            /* 1: edge() adds to the list instead of drawing */
    long ox, oy;            /* the band's corner on the target, 24.8 */
};

/* bit_of[k]: pixel k of a 32-pixel word of seen bits */
static const ogpu_w32 bit_of[32] = {
    0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u, 0x08000000u, 0x04000000u, 0x02000000u, 0x01000000u,
    0x00800000u, 0x00400000u, 0x00200000u, 0x00100000u, 0x00080000u, 0x00040000u, 0x00020000u, 0x00010000u,
    0x00008000u, 0x00004000u, 0x00002000u, 0x00001000u, 0x00000800u, 0x00000400u, 0x00000200u, 0x00000100u,
    0x00000080u, 0x00000040u, 0x00000020u, 0x00000010u, 0x00000008u, 0x00000004u, 0x00000002u, 0x00000001u
};

/* A cell's sum, past 32 bits only when some 65536 lines meet in one cell:
 * even-odd needs only its low bits, so it wraps; non-zero stops at the
 * largest, keeping its sign (its pixel is then full, or nearly). */
PX_FN int cell_add(int v, int d, int eo) {
    if (eo) return (int)((unsigned)v + (unsigned)d);
    if (d > 0 ? v > 0x7FFFFFFF - d : v < -0x7FFFFFFF - d) return d > 0 ? 0x7FFFFFFF : -0x7FFFFFFF;
    return v + d;
}

/* sub-row j (0 .. 4h - 1), pixel i (inline: on the 68k a call costs more than it) */
PX_FN void cell(struct rast *r, long i, int j, int dy, long fx0, long fx1) {
    int *c;
    if (i >= r->w || !dy) return;   /* right of the band: covers nothing */
    c = r->cell + (((long)MUL16(j >> 2, r->w) + i) << 3);
    if (r->seen) {                  /* the pixel's first write clears its cells */
        ogpu_w32 *s = r->seen + MUL16(j >> 2, r->tw) + (i >> 5), b = bit_of[i & 31];
        if (!(*s & b)) { *s |= b; c[0] = c[1] = c[2] = c[3] = c[4] = c[5] = c[6] = c[7] = 0; }
    }
    c += j & 3;
    c[0] = cell_add(c[0], dy, r->eo);
    c[4] = cell_add(c[4], dy * (int)(fx0 + fx1), r->eo);
}

/* How many pixels from i on (to w) the walk didn't write, at least one. */
static int unseen(const ogpu_w32 *s, int i, int w) {
    const ogpu_w32 *p = s + (i >> 5);
    ogpu_w32 v = *p & ((bit_of[i & 31] << 1) - 1);      /* pixel i's bit and those after it */
    int at = i & ~31;
    while (!v) {
        at += 32;
        if (at >= w) return w - i;
        v = *++p;
    }
    if (!(v & 0xFFFF0000u)) { at += 16; v <<= 16; }      /* the first one set */
    if (!(v & 0xFF000000u)) { at += 8; v <<= 8; }
    if (!(v & 0xF0000000u)) { at += 4; v <<= 4; }
    if (!(v & 0xC0000000u)) { at += 2; v <<= 2; }
    if (!(v & 0x80000000u)) at += 1;
    return (at < w ? at : w) - i;
}

/* the part of a line within row j, from xa to xb (0 .. w * 256), climbing dy (inline: once a sub-row an edge crosses) */
PX_FN void row_piece(struct rast *r, int j, long xa, long xb, int dy) {
    long ia = xa >> 8, ib = xb >> 8, i, step, x_from, x_to, dx;
    int done = 0, share, last;
    if (ia == ib) { cell(r, ia, j, dy, xa - (ia << 8), xb - (ia << 8)); return; }
    /* the climb shared out by how much of the line's width each column
     * holds, the remainder kept so the shares add up */
    dx = xb - xa; step = dx > 0 ? 1 : -1; x_from = xa;
    for (i = ia;; i += step) {
        last = i == ib;
        if (last) { x_to = xb; share = dy - done; }
        else { x_to = step > 0 ? (i + 1) << 8 : i << 8; share = (int)((long)dy * (x_to - xa) / dx) - done; }
        cell(r, i, j, share, x_from - (i << 8), x_to - (i << 8));
        done += share;
        if (last) break;
        x_from = x_to;
    }
}

static ogpu_s64 floor_div(ogpu_s64 a, ogpu_s64 b) {
    ogpu_s64 q = a / b;
    if ((a % b) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* a line in the band, x already inside 0 .. w * 256. Its x at the top of
 * each sub-row is x0 + floor((x1 - x0) (y - y0) / (y1 - y0)); from the
 * second sub-row on it is stepped, a quotient and a remainder carried, so
 * a sub-row costs no 64-bit multiply or divide (libgcc's, slow on the 68k).
 * The same x as worked out afresh each time (Team 5, 25 Oct 2026). */
static void rows(struct rast *r, long x0, long y0, long x1, long y1) {
    long y, yn, ylim = (long)r->h << 8, t, dx, d, xa, xb, q = 0, qs = 0;
    unsigned long e = 0, es = 0;            /* 0 <= e, es < d */
    int sign = 1, first = 1;
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; sign = -1; }
    if (y1 <= 0 || y0 >= ylim) return;
    dx = x1 - x0; d = y1 - y0;              /* |dx| <= w * 256 (2^22), 0 < d < 2^31 */
    y = y0 < 0 ? 0 : y0;
    xa = y == y0 ? x0 : x0 + (long)floor_div((ogpu_s64)dx * (y - y0), d);
    for (; y < y1 && y < ylim; y = yn, xa = xb) {
        long j = y >> 6;                    /* sub-rows */
        yn = (j + 1) << 6;
        if (yn >= y1) { yn = y1; xb = x1; }
        else {
            if (first) {                    /* the first sub-row's end, and the step from one to the next */
                ogpu_s64 nn = (ogpu_s64)dx * (yn - y0);
                long s = dx * 64;
                q = (long)floor_div(nn, d); e = (unsigned long)(nn - (ogpu_s64)q * d);
                qs = s / d;
                if (s % d < 0) qs--;
                es = (unsigned long)(s - qs * d);
                first = 0;
            } else {
                q += qs; e += es;
                if (e >= (unsigned long)d) { e -= (unsigned long)d; q++; }
            }
            xb = x0 + q;
        }
        row_piece(r, (int)j, xa, xb, sign * (int)(yn - y));
    }
}

/* A line on the target (24.8). What lies left of the band climbs in its
 * first column (as a line down its left edge); what lies right of it
 * covers nothing there. */
static void edge_rel(struct rast *r, long x0, long y0, long x1, long y1) {
    long xl = (long)r->w << 8, yc;
    if (y0 == y1) return;
    if ((x0 < 0 && x1 > 0) || (x0 > 0 && x1 < 0)) {
        yc = y0 + (long)((ogpu_s64)(y1 - y0) * -x0 / (x1 - x0));
        edge_rel(r, x0, y0, 0, yc); edge_rel(r, 0, yc, x1, y1);
        return;
    }
    if ((x0 < xl && x1 > xl) || (x0 > xl && x1 < xl)) {
        yc = y0 + (long)((ogpu_s64)(y1 - y0) * (xl - x0) / (x1 - x0));
        edge_rel(r, x0, y0, xl, yc); edge_rel(r, xl, yc, x1, y1);
        return;
    }
    if (x0 >= xl && x1 >= xl) return;
    if (x0 < 0 || x1 < 0) x0 = x1 = 0;
    rows(r, x0, y0, x1, y1);
}
static void edge(struct rast *r, long x0, long y0, long x1, long y1) {
    if (r->collect) {               /* kept for every band (a level one covers nothing) */
        if (y0 != y1) {
            if (r->nlist < r->cap) {
                int *e = r->list + (r->nlist << 2);
                e[0] = (int)x0; e[1] = (int)y0; e[2] = (int)x1; e[3] = (int)y1;
            }
            r->nlist++;
        }
        return;
    }
    edge_rel(r, x0 - r->ox, y0 - r->oy, x1 - r->ox, y1 - r->oy);
}

/* ---- walking a path: filled, or stroked into an outline that is filled -------------- */

struct seg { long ux, uy, nx, ny, len; };   /* direction (16.16, length 1), normal (half the width), length */

struct pctx {
    struct rast *r;
    const ogpu_u8 *path;
    long words;
    long m[6];                  /* the path's space to the target, 16.16 */
    long tol;                   /* how far a cut curve may stray, in the space it is cut in */
    long cx, cy, sx, sy;        /* the current point and the subpath's start */
    int stroke;
    /* PATH_STROKE */
    long hw, miter;             /* half the width (24.8), the miter limit (16.16) */
    int cap, join;
    const ogpu_u8 *dash;
    int ndash, dashing, di, on, active;
    long doff, rem;             /* the dash offset and what is left of the dash (24.8) */
    int sn;                     /* segments in the piece being stroked */
    long ax, ay, px, py;        /* its first point, and its last segment's end */
    struct seg s0, sp;          /* its first and last segments */
    long ls[4], lf[4], lc[4];   /* the + and - sides (x, y, x, y): the first segment's starts
                                   and ends, and where each side's next edge starts */
    /* a dashed subpath on at its start: 1 while its first piece goes on, 2
     * once it has ended with that piece's start left to draw (its start, first
     * segment, sides): joined to the last piece if the subpath closes on */
    int fp;
    long fax, fay, fls[4], flf[4];
    struct seg fs0;
};

static long sgn32(ogpu_u32 v) {
    v &= 0xFFFFFFFFUL;
    return (long)((ogpu_s64)v - (v & 0x80000000UL ? 0x100000000LL : 0));
}
static long clampl(ogpu_s64 v, long lim) { return v > lim ? lim : v < -lim ? -lim : (long)v; }
#define OGPU_COORD_MAX (1L << 29)

/* A point of the path's space on the target, 24.8. */
static void xform(const struct pctx *c, long x, long y, long *ox, long *oy) {
    *ox = clampl(((((ogpu_s64)c->m[0] * x + (ogpu_s64)c->m[1] * y) >> 16) + (c->m[2] >> 8)), OGPU_COORD_MAX);
    *oy = clampl(((((ogpu_s64)c->m[3] * x + (ogpu_s64)c->m[4] * y) >> 16) + (c->m[5] >> 8)), OGPU_COORD_MAX);
}

/* An edge of a stroke's outline (the path's space). */
static void out(struct pctx *c, long x0, long y0, long x1, long y1) {
    long a, b, d, e;
    xform(c, x0, y0, &a, &b);
    xform(c, x1, y1, &d, &e);
    edge(c->r, a, b, d, e);
}

/* An edge on side sd: the + side runs forward, the - side back, so the
 * two sides and the caps go round the outline one way. */
static void side(struct pctx *c, int sd, long x0, long y0, long x1, long y1) {
    if (sd > 0) out(c, x0, y0, x1, y1); else out(c, x1, y1, x0, y0);
}

/* The arc round (px, py) from vector v0 to v1 (each half the width long,
 * at most a quarter turn apart), cut where its chords stray past tol. */
static void arc(struct pctx *c, int sd, long px, long py, long x0, long y0, long x1, long y1, int depth) {
    long dx = x1 - x0, dy = y1 - y0, mx = x0 + x1, my = y0 + y1, l;
    if (depth < 10 && (ogpu_s64)dx * dx + (ogpu_s64)dy * dy > 8 * (ogpu_s64)c->hw * c->tol
        && (l = (long)isqrt64((ogpu_s64)mx * mx + (ogpu_s64)my * my)) > 0) {
        mx = (long)((ogpu_s64)mx * c->hw / l); my = (long)((ogpu_s64)my * c->hw / l);
        arc(c, sd, px, py, x0, y0, mx, my, depth + 1);
        arc(c, sd, px, py, mx, my, x1, y1, depth + 1);
        return;
    }
    side(c, sd, px + x0, py + y0, px + x1, py + y1);
}

static int seg_of(const struct pctx *c, long ax, long ay, long bx, long by, struct seg *s) {
    long dx = bx - ax, dy = by - ay;
    ogpu_s64 l2 = (ogpu_s64)dx * dx + (ogpu_s64)dy * dy;
    if (!l2) return 0;
    s->len = (long)isqrt64(l2);
    if (!s->len) s->len = 1;
    s->ux = (long)(((ogpu_s64)dx * 65536) / s->len); s->uy = (long)(((ogpu_s64)dy * 65536) / s->len);
    s->nx = (long)(-(ogpu_s64)s->uy * c->hw >> 16); s->ny = (long)((ogpu_s64)s->ux * c->hw >> 16);
    return 1;
}

/* The join at P between segments a and b. For each side (e, s: x, y of
 * the + side, then the - side): where a's edge ends and b's starts, and
 * what lies between drawn. The outer side gets the join; the inner one
 * meets where its two edges cross, when that is within half of each
 * segment, or else goes in to P and out again (the part drawn twice is
 * inside the stroke). */
static void join(struct pctx *c, long px, long py, const struct seg *a, const struct seg *b, int style, long *e, long *s) {
    ogpu_s64 cr = (ogpu_s64)a->ux * b->uy - (ogpu_s64)a->uy * b->ux;
    long co = (long)(((ogpu_s64)a->ux * b->ux + (ogpu_s64)a->uy * b->uy) >> 16), q = 0x10000 + co, l;
    long sx = a->nx + b->nx, sy = a->ny + b->ny, mx, my, ax, ay, bx, by, t;
    int k, sd, outer = cr > 0 ? -1 : 1;
    for (k = 0; k < 4; k += 2) {
        sd = k ? -1 : 1;
        ax = px + sd * a->nx; ay = py + sd * a->ny; bx = px + sd * b->nx; by = py + sd * b->ny;
        e[k] = ax; e[k + 1] = ay; s[k] = bx; s[k + 1] = by;
        if (!cr && co > 0) { s[k] = ax; s[k + 1] = ay; continue; }   /* straight on */
        if (sd == outer) {
            if (style == OGPU_JOIN_ROUND) {
                mx = sd * sx; my = sd * sy;
                l = (long)isqrt64((ogpu_s64)mx * mx + (ogpu_s64)my * my);
                if (l * 8 < c->hw) { mx = (long)((ogpu_s64)a->ux * c->hw >> 16); my = (long)((ogpu_s64)a->uy * c->hw >> 16); }
                else { mx = (long)((ogpu_s64)mx * c->hw / l); my = (long)((ogpu_s64)my * c->hw / l); }
                arc(c, sd, px, py, ax - px, ay - py, mx, my, 0);
                arc(c, sd, px, py, mx, my, bx - px, by - py, 0);
            } else if (style == OGPU_JOIN_MITER && q > 0
                       && (ogpu_s64)q * ((c->miter >> 4) * (ogpu_s64)(c->miter >> 4)) >= ((ogpu_s64)1 << 41)) {
                mx = px + sd * (long)(((ogpu_s64)sx * 65536) / q); my = py + sd * (long)(((ogpu_s64)sy * 65536) / q);
                side(c, sd, ax, ay, mx, my); side(c, sd, mx, my, bx, by);
            } else side(c, sd, ax, ay, bx, by);
            continue;
        }
        if (q > 0) {
            t = (long)(((ogpu_s64)b->nx * a->ux + (ogpu_s64)b->ny * a->uy) >> 16);
            t = (long)(((ogpu_s64)(t < 0 ? -t : t) * 65536) / q);
            if (2 * t <= a->len && 2 * t <= b->len) {
                e[k] = s[k] = px + sd * (long)(((ogpu_s64)sx * 65536) / q);
                e[k + 1] = s[k + 1] = py + sd * (long)(((ogpu_s64)sy * 65536) / q);
                continue;
            }
        }
        side(c, sd, ax, ay, px, py); side(c, sd, px, py, bx, by);
    }
}

/* A cap at P from +v to -v round h: at a piece's end v is the normal, at
 * its start the normal turned back (dir -1). */
static void cap(struct pctx *c, long px, long py, const struct seg *g, int dir) {
    long vx = dir * g->nx, vy = dir * g->ny;
    long hx = dir * (long)((ogpu_s64)g->ux * c->hw >> 16), hy = dir * (long)((ogpu_s64)g->uy * c->hw >> 16);
    if (c->cap == OGPU_CAP_ROUND) {
        arc(c, 1, px, py, vx, vy, hx, hy, 0);
        arc(c, 1, px, py, hx, hy, -vx, -vy, 0);
    } else if (c->cap == OGPU_CAP_SQUARE) {
        out(c, px + vx, py + vy, px + vx + hx, py + vy + hy);
        out(c, px + vx + hx, py + vy + hy, px - vx + hx, py - vy + hy);
        out(c, px - vx + hx, py - vy + hy, px - vx, py - vy);
    } else out(c, px + vx, py + vy, px - vx, py - vy);
}

static void stk_begin(struct pctx *c, long x, long y) { c->sn = 0; c->ax = c->px = x; c->ay = c->py = y; }

/* The stroke's next segment, to (x, y); smooth: inside a curve, joined round. */
static void stk_seg(struct pctx *c, long x, long y, int smooth) {
    struct seg s;
    long e[4], st[4];
    int k;
    if (!seg_of(c, c->px, c->py, x, y, &s)) return;
    if (!c->sn) {
        c->s0 = s;
        c->ls[0] = c->ax + s.nx; c->ls[1] = c->ay + s.ny; c->ls[2] = c->ax - s.nx; c->ls[3] = c->ay - s.ny;
    } else {
        join(c, c->px, c->py, &c->sp, &s, smooth ? OGPU_JOIN_ROUND : c->join, e, st);
        if (c->sn == 1) for (k = 0; k < 4; k++) c->lf[k] = e[k];       /* the first segment waits for the end */
        else { side(c, 1, c->lc[0], c->lc[1], e[0], e[1]); side(c, -1, c->lc[2], c->lc[3], e[2], e[3]); }
        for (k = 0; k < 4; k++) c->lc[k] = st[k];
    }
    c->sp = s; c->px = x; c->py = y; c->sn++;
}

/* Both sides' edges from a to b (x, y, x, y). */
static void sides(struct pctx *c, const long *a, const long *b) {
    side(c, 1, a[0], a[1], b[0], b[1]); side(c, -1, a[2], a[3], b[2], b[3]);
}

/* The end of a piece: closed, a join where it began; open, caps. The first
 * piece of a dashed subpath on at its start keeps its start for later. */
static void stk_end(struct pctx *c, int closed) {
    long e[4], st[4];
    int k;
    if (!c->sn) { if (c->fp == 1) c->fp = 0; return; }     /* nothing drawn: no first piece */
    if (closed && c->sn > 1) {
        join(c, c->ax, c->ay, &c->sp, &c->s0, c->join, e, st);
        sides(c, c->lc, e); sides(c, st, c->lf);
    } else {
        e[0] = c->px + c->sp.nx; e[1] = c->py + c->sp.ny; e[2] = c->px - c->sp.nx; e[3] = c->py - c->sp.ny;
        if (c->sn > 1) sides(c, c->lc, e);
        cap(c, c->px, c->py, &c->sp, 1);
        if (c->fp == 1) {
            c->fp = 2; c->fax = c->ax; c->fay = c->ay; c->fs0 = c->s0;
            for (k = 0; k < 4; k++) { c->fls[k] = c->ls[k]; c->flf[k] = c->sn == 1 ? e[k] : c->lf[k]; }
        } else {
            sides(c, c->ls, c->sn == 1 ? e : c->lf);
            cap(c, c->ax, c->ay, &c->s0, -1);
        }
    }
    c->sn = 0;
}

/* Dashes: the length of dash i (24.8). */
static long dash_len(const struct pctx *c, int i) {
    long v = sgn32(rd32(c->dash + (long)i * 4)) >> 8;
    return v < 0 ? 0 : v;
}

/* At each subpath's start: the pattern from the dash offset. */
static void dash_start(struct pctx *c) {
    ogpu_s64 total = 0, off;
    int i;
    c->dashing = 0; c->active = 0; c->fp = 0;
    if (!c->ndash) return;
    for (i = 0; i < c->ndash; i++) total += dash_len(c, i);
    if (c->ndash & 1) total *= 2;
    if (total <= 0) return;
    off = c->doff % total;
    if (off < 0) off += total;
    c->dashing = 1; c->di = 0; c->on = 1;
    while (off >= dash_len(c, c->di)) { off -= dash_len(c, c->di); c->di = (c->di + 1) % c->ndash; c->on ^= 1; }
    c->rem = dash_len(c, c->di) - (long)off;
    c->fp = c->on;
}

/* A line of the path being stroked, cut into dashes. */
static void dash_seg(struct pctx *c, long x0, long y0, long x1, long y1, int smooth) {
    long dx = x1 - x0, dy = y1 - y0, len, pos = 0, step;
    if (!c->dashing) { stk_seg(c, x1, y1, smooth); return; }
    len = (long)isqrt64((ogpu_s64)dx * dx + (ogpu_s64)dy * dy);
    while (pos < len) {
        step = c->rem < len - pos ? c->rem : len - pos;
        if (c->on) {
            if (!c->active) {
                stk_begin(c, x0 + (long)((ogpu_s64)dx * pos / len), y0 + (long)((ogpu_s64)dy * pos / len));
                c->active = 1; smooth = 0;
            }
            stk_seg(c, x0 + (long)((ogpu_s64)dx * (pos + step) / len), y0 + (long)((ogpu_s64)dy * (pos + step) / len), smooth);
        }
        pos += step; c->rem -= step;
        if (!c->rem) {
            if (c->active) { stk_end(c, 0); c->active = 0; }
            c->on ^= 1; c->di = (c->di + 1) % c->ndash; c->rem = dash_len(c, c->di);
        }
    }
}

/* The next point of the walk: filled, a line on the target; stroked, a
 * segment (smooth: inside a curve). */
static void path_to(struct pctx *c, long x, long y, int smooth) {
    if (!c->stroke) edge(c->r, c->cx, c->cy, x, y);
    else dash_seg(c, c->cx, c->cy, x, y, smooth);
    c->cx = x; c->cy = y;
}

static void sub_begin(struct pctx *c) {
    if (!c->stroke) return;
    dash_start(c);
    if (!c->dashing) stk_begin(c, c->cx, c->cy);
}

static void sub_end(struct pctx *c, int closed) {
    long e[4], st[4];
    if (!c->stroke) { edge(c->r, c->cx, c->cy, c->sx, c->sy); return; }    /* a fill closes every subpath */
    if (closed && (c->cx != c->sx || c->cy != c->sy)) path_to(c, c->sx, c->sy, 0);
    if (!c->dashing) stk_end(c, closed);
    else if (c->fp == 1) {                  /* one dash all the way: as undashed */
        c->fp = 0;
        if (c->active) stk_end(c, closed);
    } else {
        if (c->active && closed && c->fp && c->sn) {
            /* on at both ends of a closed subpath: the last piece joins the first */
            join(c, c->fax, c->fay, &c->sp, &c->fs0, c->join, e, st);
            sides(c, c->sn == 1 ? c->ls : c->lc, e); sides(c, st, c->flf);
            if (c->sn > 1) sides(c, c->ls, c->lf);
            cap(c, c->ax, c->ay, &c->s0, -1);
            c->sn = c->fp = 0;
        } else if (c->active) stk_end(c, 0);
        if (c->fp) { sides(c, c->fls, c->flf); cap(c, c->fax, c->fay, &c->fs0, -1); c->fp = 0; }
    }
    c->active = 0;
}

/* A curve from the current point (x[0], y[0]) through np - 1 more, cut
 * into lines: n of them keep each within tol, n by the largest second
 * difference of its points (de Casteljau at each step, in 64 bits). */
static void flatten(struct pctx *c, long *x, long *y, int np) {
    ogpu_s64 dd = 0, v, want;
    long ax[4], ay[4], t;
    int k, i, l, n;
    for (k = 0; k + 2 < np; k++) {
        v = (ogpu_s64)x[k] - 2 * (ogpu_s64)x[k + 1] + x[k + 2]; if (v < 0) v = -v; if (v > dd) dd = v;
        v = (ogpu_s64)y[k] - 2 * (ogpu_s64)y[k + 1] + y[k + 2]; if (v < 0) v = -v; if (v > dd) dd = v;
    }
    want = dd * (np == 3 ? 2 : 6) / (8 * c->tol) + 1;
    n = (int)isqrt64(want);
    if ((ogpu_s64)n * n < want) n++;
    if (n > 1024) n = 1024;
    for (k = 1; k <= n; k++) {
        t = (long)(((ogpu_s64)k * 65536) / n);
        for (i = 0; i < np; i++) { ax[i] = x[i]; ay[i] = y[i]; }
        for (l = np - 1; l > 0; l--)
            for (i = 0; i < l; i++) {
                ax[i] += (long)(((ogpu_s64)(ax[i + 1] - ax[i]) * t) >> 16);
                ay[i] += (long)(((ogpu_s64)(ay[i + 1] - ay[i]) * t) >> 16);
            }
        if (k == n) { ax[0] = x[np - 1]; ay[0] = y[np - 1]; }
        path_to(c, ax[0], ay[0], k > 1);
    }
}

/* The path, once: filled, its points on the target; stroked, in its own
 * space (the outline goes to the target edge by edge). */
static void walk(struct pctx *c) {
    long at = 0, x[4], y[4];
    int v, np, k;
    c->cx = c->sx = c->cy = c->sy = 0;      /* a path starts at (0, 0) */
    if (!c->stroke) { xform(c, 0, 0, &c->cx, &c->cy); c->sx = c->cx; c->sy = c->cy; }
    sub_begin(c);
    while (at < c->words) {
        v = (int)rd32(c->path + at * 4);
        np = v == OGPU_PATH_QUAD ? 2 : v == OGPU_PATH_CUBIC ? 3 : v == OGPU_PATH_CLOSE ? 0 : 1;
        at++;
        for (k = 1; k <= np; k++, at += 2) {
            x[k] = clampl(sgn32(rd32(c->path + at * 4)), OGPU_COORD_MAX);
            y[k] = clampl(sgn32(rd32(c->path + at * 4 + 4)), OGPU_COORD_MAX);
            if (!c->stroke) xform(c, x[k], y[k], &x[k], &y[k]);
        }
        switch (v) {
        case OGPU_PATH_MOVE:
            sub_end(c, 0);
            c->cx = c->sx = x[1]; c->cy = c->sy = y[1];
            sub_begin(c);
            break;
        case OGPU_PATH_LINE: path_to(c, x[1], y[1], 0); break;
        case OGPU_PATH_CLOSE:
            sub_end(c, 1);
            c->cx = c->sx; c->cy = c->sy;
            sub_begin(c);
            break;
        default: x[0] = c->cx; y[0] = c->cy; flatten(c, x, y, np + 1); break;
        }
    }
    sub_end(c, 0);
}

/* ---- v1.6: paint (a colour, a slot, a gradient) ------------------------------------- */

struct paint {
    int kind;                   /* 0 a colour, 1 a slot, 2 a gradient, 3 a pattern */
    struct ch col;
    const struct ogpu_surface *s;
    int ox, oy, rep;
    /* a gradient */
    const ogpu_u8 *stops;
    int n, gk, spread, none;
    ogpu_s64 inv[6];            /* the target to the gradient's space, 16.16 (the move may pass 32 bits) */
    long g[6];                  /* its geometry, 16.16 */
    long dx, dy, sh;            /* LINEAR: (x1, y1) - (x0, y0), 24.8 >> sh */
    ogpu_s64 dd, ga;            /* LINEAR: |d|^2; RADIAL: a */
    ogpu_s64 t24, dt24;         /* LINEAR: t at the pixel (8.24), and its step */
    ogpu_s64 gx, gy;            /* the pixel in the gradient's space, 16.16 */
    long lo, hi;                /* the stops' interval cached */
    ogpu_u32 rc;
    struct ch c0, c1;
    /* a pattern: its slot's rectangle, edges, filter and alpha */
    int psx, psy, psw, psh, edge, bil;
    unsigned alpha;
};

/* The matrix in the 6 words at m (16.16, a space to the target) inverted
 * into p->inv, as COMPOSITE_AFFINE does; 0 when it is flat. */
static int set_inv(struct paint *p, const ogpu_u8 *mw) {
    long m[6];
    ogpu_s64 det;
    int k;
    for (k = 0; k < 6; k++) m[k] = clampl(sgn32(rd32(mw + 4 * k)), 0x3FFFFFFF);
    det = (ogpu_s64)m[0] * m[4] - (ogpu_s64)m[1] * m[3];
    if (!det) return 0;
    p->inv[0] = clampl((ogpu_s64)m[4] * 4294967296LL / det, 0x3FFFFFFF);
    p->inv[1] = clampl(-((ogpu_s64)m[1] * 4294967296LL) / det, 0x3FFFFFFF);
    p->inv[3] = clampl(-((ogpu_s64)m[3] * 4294967296LL) / det, 0x3FFFFFFF);
    p->inv[4] = clampl((ogpu_s64)m[0] * 4294967296LL / det, 0x3FFFFFFF);
    p->inv[2] = -((p->inv[0] * m[2] + p->inv[1] * m[5]) >> 16);       /* within 2^45 */
    p->inv[5] = -((p->inv[3] * m[2] + p->inv[4] * m[5]) >> 16);
    return 1;
}

/* LINEAR's t (8.24) for a step (dgx, dgy) in the gradient's space (16.16). */
static ogpu_s64 lin_t24(const struct paint *p, ogpu_s64 dgx, ogpu_s64 dgy) {
    ogpu_s64 v;
    if (dgx > 0x3FFFFFFF) dgx = 0x3FFFFFFF;
    if (dgx < -0x3FFFFFFF) dgx = -0x3FFFFFFF;
    if (dgy > 0x3FFFFFFF) dgy = 0x3FFFFFFF;
    if (dgy < -0x3FFFFFFF) dgy = -0x3FFFFFFF;
    v = dgx * p->dx + dgy * p->dy;
    v = p->sh <= 16 ? v * ((ogpu_s64)1 << (16 - p->sh)) : v / ((ogpu_s64)1 << (p->sh - 16));
    return v / p->dd;
}

static int paint_init(struct ogpu_core *c, struct paint *p, ogpu_u32 fl, ogpu_u32 word, ogpu_u32 xy) {
    const ogpu_u8 *g;
    ogpu_s64 v;
    int k;
    for (k = 0; k < (int)sizeof *p; k++) ((ogpu_u8 *)p)[k] = 0;
    if (fl & OGPU_PATH_SLOT) {
        if (word >= OGPU_MAX_SLOTS || !c->slot[word].pixels) return OGPU_ERR_NOSURFACE;
        p->s = &c->slot[word];
        if (!is_colour(p->s->format) && p->s->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        p->kind = 1; p->ox = hi_s(xy); p->oy = lo_s(xy); p->rep = (fl & OGPU_PATH_REPEAT) != 0;
        return OGPU_OK;
    }
    if (fl & OGPU_PATH_PATTERN) {
        ogpu_u32 slot, sxy, swh;
        if (!(g = c->map(c->user, word, OGPU_PATTERN_WORDS * 4))) return OGPU_ERR_NOMAP;
        slot = rd32(g); sxy = rd32(g + 8); swh = rd32(g + 12);
        if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
        p->s = &c->slot[slot];
        p->edge = (int)(rd32(g + 4) & 3); p->bil = (rd32(g + 4) & OGPU_PATTERN_BILINEAR) != 0;
        p->psx = hi_s(sxy); p->psy = lo_s(sxy); p->psw = hi_u(swh); p->psh = lo_u(swh);
        if (!p->psw || !p->psh) { p->psx = p->psy = 0; p->psw = p->s->w; p->psh = p->s->h; }
        if (!is_colour(p->s->format) && p->s->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        if (p->psx < 0 || p->psy < 0 || p->psx + p->psw > p->s->w || p->psy + p->psh > p->s->h) return OGPU_ERR_UNSUPPORTED;
        p->alpha = (unsigned)(rd32(g + 40) & 255);
        p->kind = 3;
        if (!set_inv(p, g + 16)) p->none = 1;
        return OGPU_OK;
    }
    if (!(fl & OGPU_PATH_GRADIENT)) { ch_of(word, &p->col); return OGPU_OK; }
    if (!(g = c->map(c->user, word, 56))) return OGPU_ERR_NOMAP;
    p->gk = (int)(rd32(g) & 255); p->spread = (int)((rd32(g) >> 8) & 255);
    p->n = (int)(rd32(g + 4) > 4096 ? 0 : rd32(g + 4));
    if (p->gk < OGPU_GRAD_LINEAR || p->gk > OGPU_GRAD_CONIC || p->spread > OGPU_SPREAD_REFLECT || !p->n)
        return OGPU_ERR_UNSUPPORTED;
    if (!(g = c->map(c->user, word, (ogpu_u32)OGPU_GRAD_WORDS(p->n) * 4))) return OGPU_ERR_NOMAP;
    p->kind = 2; p->stops = g + 56; p->lo = 1; p->hi = 0;
    for (k = 0; k < 6; k++) p->g[k] = sgn32(rd32(g + 32 + 4 * k));
    if (!set_inv(p, g + 8)) { p->none = 1; return OGPU_OK; }
    if (p->gk == OGPU_GRAD_LINEAR) {
        p->dx = (p->g[2] - p->g[0]) >> 8; p->dy = (p->g[3] - p->g[1]) >> 8; p->sh = 0;
        while (p->dx >= 0x8000 || p->dx <= -0x8000 || p->dy >= 0x8000 || p->dy <= -0x8000) { p->dx >>= 1; p->dy >>= 1; p->sh++; }
        p->dd = (ogpu_s64)p->dx * p->dx + (ogpu_s64)p->dy * p->dy;
        if (!p->dd) { p->none = 1; return OGPU_OK; }
        p->dt24 = lin_t24(p, p->inv[0], p->inv[3]);
    } else if (p->gk == OGPU_GRAD_RADIAL) {
        for (k = 0; k < 6; k++) p->g[k] >>= 8;      /* 24.8 */
        v = (ogpu_s64)(p->g[3] - p->g[0]) * (p->g[3] - p->g[0]) + (ogpu_s64)(p->g[4] - p->g[1]) * (p->g[4] - p->g[1]);
        p->ga = v - (ogpu_s64)(p->g[5] - p->g[2]) * (p->g[5] - p->g[2]);
        if (!v && p->g[5] == p->g[2]) p->none = 1;  /* two equal circles: nothing */
    }
    return OGPU_OK;
}

/* At the start of a row: pixel x, y's centre in the gradient's space. */
static void paint_row(struct paint *p, int x, int y) {
    ogpu_s64 X = ((ogpu_s64)x * 65536) + 0x8000, Y = ((ogpu_s64)y * 65536) + 0x8000;
    if (p->kind < 2 || p->none) return;
    p->gx = ((p->inv[0] * X + p->inv[1] * Y) >> 16) + p->inv[2];
    p->gy = ((p->inv[3] * X + p->inv[4] * Y) >> 16) + p->inv[5];
    if (p->kind == 2 && p->gk == OGPU_GRAD_LINEAR) p->t24 = lin_t24(p, p->gx - p->g[0], p->gy - p->g[1]);
}
static void paint_step(struct paint *p) {
    if (p->kind >= 2 && !p->none) { p->gx += p->inv[0]; p->gy += p->inv[3]; p->t24 += p->dt24; }
}
/* n steps at once */
static void paint_skip(struct paint *p, int n) {
    if (p->kind >= 2 && !p->none) { p->gx += n * p->inv[0]; p->gy += n * p->inv[3]; p->t24 += n * p->dt24; }
}

/* atan(z) in turns for z in 0..1 (16.16): z/8 + z(1-z)(0.03895 + 0.01055 z), within 0.0003 */
static long atan_turns(long z) {
    return (z >> 3) + (long)((((ogpu_s64)z * (0x10000 - z)) >> 16) * (2552 + ((692 * z) >> 16)) >> 16);
}

/* The gradient's t at the pixel (16.16, before the spread); 0 when no
 * circle reaches it (RADIAL). */
BIG_FN int grad_t(const struct paint *p, long *t) {
    ogpu_s64 dx, dy, b, cc, a, m, disc, r1, r2;
    long sq;
    int k = 0;
    if (p->gk == OGPU_GRAD_LINEAR) { *t = clampl(p->t24 >> 8, 0x3FFFFFFFL); return 1; }
    if (p->gk == OGPU_GRAD_CONIC) {
        long ax, ay, z, an;
        dx = p->gx - p->g[0]; dy = p->gy - p->g[1];
        while (dx >= 0x40000000LL || dx <= -0x40000000LL || dy >= 0x40000000LL || dy <= -0x40000000LL) { dx /= 2; dy /= 2; }
        ax = (long)(dx < 0 ? -dx : dx); ay = (long)(dy < 0 ? -dy : dy);
        if (!ax && !ay) an = 0;
        else {
            z = (long)(((ogpu_s64)(ax < ay ? ax : ay) * 65536) / (ax < ay ? ay : ax));
            an = atan_turns(z);
            if (ay > ax) an = 0x4000 - an;
            if (dx < 0) an = 0x8000 - an;
            if (dy < 0) an = 0x10000 - an;
        }
        *t = (an - p->g[2]) & 0xFFFF;
        return 1;
    }
    /* RADIAL: the largest t with |p - c(t)| = r(t) >= 0, from a t^2 - 2 b t + c = 0 */
    dx = (p->gx >> 8) - p->g[0]; dy = (p->gy >> 8) - p->g[1];
    if (dx > 0x3FFFFFFF) dx = 0x3FFFFFFF;
    if (dx < -0x3FFFFFFF) dx = -0x3FFFFFFF;
    if (dy > 0x3FFFFFFF) dy = 0x3FFFFFFF;
    if (dy < -0x3FFFFFFF) dy = -0x3FFFFFFF;
    b = dx * (p->g[3] - p->g[0]) + dy * (p->g[4] - p->g[1]) + (ogpu_s64)p->g[2] * (p->g[5] - p->g[2]);
    cc = dx * dx + dy * dy - (ogpu_s64)p->g[2] * p->g[2];
    a = p->ga;
    m = (a < 0 ? -a : a) | (b < 0 ? -b : b) | (cc < 0 ? -cc : cc);
    while (m >= 0x40000000LL) { m >>= 1; k++; }
    a >>= k; b >>= k; cc >>= k;     /* the same roots, in 31 bits */
    if (!a) {
        if (!b) return 0;
        r1 = r2 = cc * 32768 / b;
    } else {
        disc = b * b - a * cc;
        if (disc < 0) return 0;
        sq = (long)isqrt64(disc);
        r1 = (b + sq) * 65536 / a; r2 = (b - sq) * 65536 / a;
        if (r2 > r1) { m = r1; r1 = r2; r2 = m; }
    }
    r1 = clampl(r1, 0x3FFFFFFFL); r2 = clampl(r2, 0x3FFFFFFFL);
    if ((ogpu_s64)p->g[2] * 65536 + r1 * (p->g[5] - p->g[2]) >= 0) m = r1;
    else if ((ogpu_s64)p->g[2] * 65536 + r2 * (p->g[5] - p->g[2]) >= 0) m = r2;
    else return 0;
    *t = (long)m;
    return 1;
}

/* The stops' colour at t (0 .. 0x10000): premultiplied, mixed linearly. */
BIG_FN void grad_colour(struct paint *p, long t, struct ch *o) {
    unsigned long w;
    if (t < p->lo || t >= p->hi) {
        long prev = 0, of = 0;
        int k;
        for (k = 0; k < p->n; k++) {
            of = sgn32(rd32(p->stops + 8 * k));
            if (of < prev) of = prev;
            if (of > 0x10000) of = 0x10000;
            if (t < of) break;
            prev = of;
        }
        if (k == 0) { ch_of(rd32(p->stops + 4), &p->c0); p->lo = -0x7FFFFFFFL; p->hi = of; p->rc = 0; }
        else if (k == p->n) { ch_of(rd32(p->stops + 8 * k - 4), &p->c0); p->lo = prev; p->hi = 0x7FFFFFFFL; p->rc = 0; }
        else {
            ch_of(rd32(p->stops + 8 * k - 4), &p->c0); ch_of(rd32(p->stops + 8 * k + 4), &p->c1);
            p->lo = prev; p->hi = of; p->rc = (ogpu_u32)(0xFFFFFFFFUL / (unsigned long)(of - prev));
        }
    }
    if (!p->rc) { *o = p->c0; return; }
    w = (((unsigned long)(t - p->lo) * p->rc) & 0xFFFFFFFFUL) >> 16;
    o->a = (unsigned)((p->c0.a * (0x10000 - w) + p->c1.a * w + 0x8000) >> 16);
    o->r = (unsigned)((p->c0.r * (0x10000 - w) + p->c1.r * w + 0x8000) >> 16);
    o->g = (unsigned)((p->c0.g * (0x10000 - w) + p->c1.g * w + 0x8000) >> 16);
    o->b = (unsigned)((p->c0.b * (0x10000 - w) + p->c1.b * w + 0x8000) >> 16);
}

/* A pattern's coordinate k on an edge of n pixels: inside it, or -1. */
static int pat_edge(int k, int n, int e) {
    if (e == OGPU_PATTERN_PAD) return k < 0 ? 0 : k >= n ? n - 1 : k;
    if (e == OGPU_PATTERN_REPEAT) return wrap(k, n);
    if (e == OGPU_PATTERN_REFLECT) { k = wrap(k, 2 * n); return k >= n ? 2 * n - 1 - k : k; }
    return k < 0 || k >= n ? -1 : k;
}
BIG_FN void pat_px(const struct paint *p, int u, int v, struct ch *o) {
    u = pat_edge(u, p->psw, p->edge); v = pat_edge(v, p->psh, p->edge);
    if (u < 0 || v < 0) { o->a = o->r = o->g = o->b = 0; return; }
    ld(row_at(p->s, p->psy + v) + xoff(p->psx + u, fmt_bpp[p->s->format]), p->s->format, o);
}

/* The paint at pixel x of the row. */
BIG_FN void paint_px(struct paint *p, int x, int y, struct ch *o) {
    long t;
    if (p->kind == 0) { *o = p->col; return; }
    if (p->kind == 1) {
        const struct ogpu_surface *s = p->s;
        int u = x - p->ox, v = y - p->oy;
        if (p->rep) { u = wrap(u, s->w); v = wrap(v, s->h); }
        if (u < 0 || v < 0 || u >= s->w || v >= s->h) { o->a = o->r = o->g = o->b = 0; return; }
        ld(row_at(s, v) + xoff(u, fmt_bpp[s->format]), s->format, o);
        return;
    }
    if (p->kind == 3 && !p->none) {
        long u = clampl(p->gx, 0x3FFFFFFFL), v = clampl(p->gy, 0x3FFFFFFFL);
        if (p->bil) {
            struct ch q[4], t0, t1;
            int x0, y0;
            u -= 0x8000; v -= 0x8000;
            x0 = (int)(u >> 16); y0 = (int)(v >> 16);
            pat_px(p, x0, y0, &q[0]); pat_px(p, x0 + 1, y0, &q[1]);
            pat_px(p, x0, y0 + 1, &q[2]); pat_px(p, x0 + 1, y0 + 1, &q[3]);
            mix(&t0, &q[0], &q[1], (unsigned)(u >> 8) & 255);
            mix(&t1, &q[2], &q[3], (unsigned)(u >> 8) & 255);
            mix(o, &t0, &t1, (unsigned)(v >> 8) & 255);
        } else pat_px(p, (int)(u >> 16), (int)(v >> 16), o);
        if (p->alpha != 255) { o->a = mul255(o->a, p->alpha); o->r = mul255(o->r, p->alpha); o->g = mul255(o->g, p->alpha); o->b = mul255(o->b, p->alpha); }
        return;
    }
    if (p->none || !grad_t(p, &t)) { o->a = o->r = o->g = o->b = 0; return; }
    if (p->spread == OGPU_SPREAD_REPEAT) t &= 0xFFFF;
    else if (p->spread == OGPU_SPREAD_REFLECT) { t &= 0x1FFFF; if (t > 0x10000) t = 0x20000 - t; }
    else t = t < 0 ? 0 : t > 0x10000 ? 0x10000 : t;
    grad_colour(p, t, o);
}

/* s by operator op onto the pixel at dp. */
BIG_FN void put_op(ogpu_u8 *dp, int f, struct ch *s, int op) {
    struct ch d;
    if (op == OGPU_PD_OVER && s->a == 255) { st(dp, f, 255, s->r, s->g, s->b); return; }
    ld(dp, f, &d);
    pd_apply(&d, s, op);
    st(dp, f, d.a, d.r, d.g, d.b);
}

/* The operators that change a pixel s = 0 doesn't: they act on the whole
 * drawable rectangle. */
static int unbounded(int op) {
    return op == OGPU_PD_CLEAR || op == OGPU_PD_SRC || op == OGPU_PD_IN || op == OGPU_PD_IN_REVERSE
           || op == OGPU_PD_OUT || op == OGPU_PD_ATOP_REVERSE;
}
static int op_ok(int op) {
    return op < OGPU_PD_OPS || (op >= OGPU_BLEND_MULTIPLY && op <= OGPU_BLEND_LUMINOSITY);
}

/* n pixels of an ARGB32 (xa 0) or XRGB32 (xa 1) target from dp, by
 * operator op with the same source s: as put_op draws them, without its
 * reads and writes of channels. OVER and SRC write whole words where the
 * result is the same for every pixel, and OVER blends a long run through
 * a table of what the target's channels keep (Team 5, 25 Oct 2026). */
static void solid_run(ogpu_u8 *dp, int n, const struct ch *s, int op, int xa) {
    unsigned sa = s->a, sr = s->r, sg = s->g, sb = s->b, fb;
    int k;
    if (op == OGPU_PD_SRC || (op == OGPU_PD_OVER && sa == 255)) {
        ogpu_u8 b[4];
        b[0] = (ogpu_u8)(xa ? 0 : op == OGPU_PD_SRC ? sa : 255); b[1] = (ogpu_u8)sr; b[2] = (ogpu_u8)sg; b[3] = (ogpu_u8)sb;
        if (aligned4(dp)) fill_words((ogpu_w32 *)(void *)dp, word_of(b), n);
        else for (k = 0; k < n; k++, dp += 4) { dp[0] = b[0]; dp[1] = b[1]; dp[2] = b[2]; dp[3] = b[3]; }
        return;
    }
    if (op != OGPU_PD_OVER) {
        struct ch c = *s;
        for (k = 0; k < n; k++, dp += 4) put_op(dp, xa ? OGPU_FMT_XRGB32 : OGPU_FMT_ARGB32, &c, op);
        return;
    }
    /* OVER: d = s + d (255 - sa) / 255 a channel (pd2 with fa 255, as
     * mul255(x, 255) is x), saturating; XRGB32's X is written 0 */
    fb = 255 - sa;
    if (n >= 64) {
        ogpu_u8 keep[256];
        unsigned v;
        for (v = 0; v < 256; v++) keep[v] = (ogpu_u8)mul255(v, fb);
        for (k = 0; k < n; k++, dp += 4) {
            dp[0] = (ogpu_u8)(xa ? 0 : sat8(sa + keep[dp[0]]));
            dp[1] = (ogpu_u8)sat8(sr + keep[dp[1]]);
            dp[2] = (ogpu_u8)sat8(sg + keep[dp[2]]);
            dp[3] = (ogpu_u8)sat8(sb + keep[dp[3]]);
        }
        return;
    }
    for (k = 0; k < n; k++, dp += 4) {
        dp[0] = (ogpu_u8)(xa ? 0 : sat8(sa + mul255(dp[0], fb)));
        dp[1] = (ogpu_u8)sat8(sr + mul255(dp[1], fb));
        dp[2] = (ogpu_u8)sat8(sg + mul255(dp[2], fb));
        dp[3] = (ogpu_u8)sat8(sb + mul255(dp[3], fb));
    }
}

/* One pixel s by SRC or OVER into an ARGB32 (xa 0) or XRGB32 target: put_op's bytes. */
PX_FN void put_src_over(ogpu_u8 *dp, const struct ch *s, int op, int xa) {
    unsigned fb;
    if (op == OGPU_PD_SRC || s->a == 255) {
        dp[0] = (ogpu_u8)(xa ? 0 : op == OGPU_PD_SRC ? s->a : 255);
        dp[1] = (ogpu_u8)s->r; dp[2] = (ogpu_u8)s->g; dp[3] = (ogpu_u8)s->b;
        return;
    }
    fb = 255 - s->a;
    dp[0] = (ogpu_u8)(xa ? 0 : sat8(s->a + mul255(dp[0], fb)));
    dp[1] = (ogpu_u8)sat8(s->r + mul255(dp[1], fb));
    dp[2] = (ogpu_u8)sat8(s->g + mul255(dp[2], fb));
    dp[3] = (ogpu_u8)sat8(s->b + mul255(dp[3], fb));
}

/* (c0 (65536 - w) + c1 w + 32768) / 65536, rounded down (grad_colour's
 * mix of a channel, 0 <= w < 65536), with one 16-bit multiply */
PX_FN unsigned lerp16(unsigned c0, unsigned c1, unsigned long w) {
    return c1 >= c0 ? c0 + ((MUL16(c1 - c0, w) + 0x8000UL) >> 16) : c0 - ((MUL16(c0 - c1, w) + 0x7FFFUL) >> 16);
}

/* A LINEAR gradient's colour at the pixel (paint_px's, from p->t24),
 * the stops' interval taken from grad_colour's cache while t stays in it. */
PX_FN void grad_linear(struct paint *p, struct ch *o) {
    long t = clampl(p->t24 >> 8, 0x3FFFFFFFL);
    unsigned long w;
    if (p->spread == OGPU_SPREAD_REPEAT) t &= 0xFFFF;
    else if (p->spread == OGPU_SPREAD_REFLECT) { t &= 0x1FFFF; if (t > 0x10000) t = 0x20000 - t; }
    else t = t < 0 ? 0 : t > 0x10000 ? 0x10000 : t;
    if (t < p->lo || t >= p->hi || !p->rc) { grad_colour(p, t, o); return; }
    w = (((unsigned long)(t - p->lo) * p->rc) & 0xFFFFFFFFUL) >> 16;
    o->a = lerp16(p->c0.a, p->c1.a, w); o->r = lerp16(p->c0.r, p->c1.r, w);
    o->g = lerp16(p->c0.g, p->c1.g, w); o->b = lerp16(p->c0.b, p->c1.b, w);
}

/* n pixels of a LINEAR gradient at coverage al, OVER or SRC, into an
 * ARGB32 (xa 0) or XRGB32 target from dp: paint_px and put_op's bytes,
 * worked out inline; one colour for the run where t doesn't change along
 * the row (a gradient top to bottom) (Team 5, 25 Oct 2026). */
static void grad_run(struct paint *p, ogpu_u8 *dp, int n, unsigned al, int op, int xa) {
    struct ch s;
    unsigned fb;
    int k;
    if (!p->dt24) {
        grad_linear(p, &s);
        if (al != 255) { s.a = mul255(s.a, al); s.r = mul255(s.r, al); s.g = mul255(s.g, al); s.b = mul255(s.b, al); }
        solid_run(dp, n, &s, op, xa);
    } else
        for (k = 0; k < n; k++, dp += 4, p->t24 += p->dt24) {
            grad_linear(p, &s);
            if (al != 255) { s.a = mul255(s.a, al); s.r = mul255(s.r, al); s.g = mul255(s.g, al); s.b = mul255(s.b, al); }
            if (op == OGPU_PD_SRC || s.a == 255) {
                dp[0] = (ogpu_u8)(xa ? 0 : op == OGPU_PD_SRC ? s.a : 255);
                dp[1] = (ogpu_u8)s.r; dp[2] = (ogpu_u8)s.g; dp[3] = (ogpu_u8)s.b;
                continue;
            }
            fb = 255 - s.a;
            dp[0] = (ogpu_u8)(xa ? 0 : sat8(s.a + mul255(dp[0], fb)));
            dp[1] = (ogpu_u8)sat8(s.r + mul255(dp[1], fb));
            dp[2] = (ogpu_u8)sat8(s.g + mul255(dp[2], fb));
            dp[3] = (ogpu_u8)sat8(s.b + mul255(dp[3], fb));
        }
    p->gx += n * p->inv[0]; p->gy += n * p->inv[3];     /* as n paint_steps (t24 stepped above, or not moving) */
}

/* The sweep's place in a pixel row, and what it draws with. */
struct prun {
    ogpu_u8 *dp;                /* pixel x on the target */
    int x, y, f, dbpp, op, bounded, mox;
    const struct ogpu_surface *m;
    const ogpu_u8 *mrow;        /* the mask's row, or 0 (the mask is 0 there) */
    struct paint *pt;
};

/* n pixels from r->x on with coverage al (before the mask), as the
 * per-pixel form draws them: the paint times the coverage, by the
 * operator. */
BIG_FN void put_run(struct prun *r, int n, unsigned al) {
    struct paint *p = r->pt;
    struct ch s;
    int k;
    if (!r->m) {
        if (!al && r->bounded) {                        /* nothing to draw */
            paint_skip(p, n);
            r->x += n; r->dp += (long)n * r->dbpp;
            return;
        }
        if (!p->kind) {                                 /* a colour: the same source for the whole run */
            ogpu_u8 *dp = r->dp;
            s = p->col;
            if (al != 255) { s.a = mul255(s.a, al); s.r = mul255(s.r, al); s.g = mul255(s.g, al); s.b = mul255(s.b, al); }
            r->x += n; r->dp += (long)n * r->dbpp;
            if (r->f == OGPU_FMT_ARGB32 || r->f == OGPU_FMT_XRGB32) {
                solid_run(dp, n, &s, r->op, r->f == OGPU_FMT_XRGB32);
                return;
            }
            for (k = 0; k < n; k++, dp += r->dbpp) put_op(dp, r->f, &s, r->op);
            return;
        }
        if (p->kind == 2 && p->gk == OGPU_GRAD_LINEAR && !p->none && (r->f == OGPU_FMT_ARGB32 || r->f == OGPU_FMT_XRGB32)
            && (r->op == OGPU_PD_OVER || r->op == OGPU_PD_SRC)) {
            grad_run(p, r->dp, n, al, r->op, r->f == OGPU_FMT_XRGB32);
            r->x += n; r->dp += (long)n * 4;
            return;
        }
    }
    for (k = 0; k < n; k++, r->x++, r->dp += r->dbpp, paint_step(p)) {
        unsigned a = al;
        if (r->m) {
            int mx = r->x - r->mox;
            struct ch mp;
            if (r->mrow && mx >= 0 && mx < r->m->w) { ld(r->mrow + xoff(mx, fmt_bpp[r->m->format]), r->m->format, &mp); a = mul255(a, mp.a); }
            else a = 0;
        }
        if (!a && r->bounded) continue;
        paint_px(p, r->x, r->y, &s);
        if (a != 255) { s.a = mul255(s.a, a); s.r = mul255(s.r, a); s.g = mul255(s.g, a); s.b = mul255(s.b, a); }
        put_op(r->dp, r->f, &s, r->op);
    }
}

/* A pixel's coverage, 0-255, from its sub-rows' sums of cover left of it
 * and their areas in it (ar; 0 when its cells are clear). */
PX_FN unsigned path_cov(const long *acc, const int *ar, int evenodd) {
    long cov, sub, a2;
    int q;
    for (cov = 0, q = 0; q < OGPU_SUBROWS; q++) {
        /* each sub-row's coverage, 0 .. 0x4000 (256 x 64 subpixels), in 32
         * bits: even-odd wants it modulo 0x8000 (so its cells may wrap);
         * non-zero past 0x4000 is full, and is taken as full from acc * 256
         * = 2^30 (a winding of some 65536; area / 2 is under 2^30, and near
         * it only with some 65536 lines in one cell) */
        a2 = ar ? ar[q] / 2 : 0;
        if (evenodd) {
            sub = (long)((((unsigned long)acc[q] << 8) - (unsigned long)a2) & 0x7FFF);
            if (sub > 0x4000) sub = 0x8000 - sub;
        } else if (acc[q] >= 0x400000L || acc[q] <= -0x400000L) sub = 0x4000;
        else {
            sub = acc[q] * 256 - a2;
            if (sub < 0) sub = -sub;
            if (sub > 0x4000) sub = 0x4000;
        }
        cov += sub;
    }
    return (unsigned)((cov * 255 + 0x8000) >> 16);
}

/* PATH_FILL and PATH_STROKE (v1.6). */
OP_FN op_path(struct ogpu_core *c, const ogpu_u8 *a, int stroke) {
    ogpu_u32 fl = rd32(a), mslot = rd32(a + 12), mxy = rd32(a + 16), paddr = rd32(a + 20), pwords = rd32(a + 24);
    ogpu_u32 waddr = rd32(a + 52), wbytes = rd32(a + 56);
    const struct ogpu_surface *t = &c->slot[c->target], *m = 0;
    int op = (int)(fl & 255), evenodd = (fl & OGPU_PATH_EVENODD) != 0, mox = hi_s(mxy), moy = lo_s(mxy);
    int rx0, ry0, rx1, ry1, w, h, bh, band, i, j, dbpp = bytes_per_pixel(t->format), r, bounded = !unbounded(op), tw, colour;
    long minx = 0x7FFFFFFFL, miny = 0x7FFFFFFFL, maxx = -0x7FFFFFFFL, maxy = -0x7FFFFFFFL, at, x, y, ex, k;
    ogpu_u32 rowb, len;
    struct pctx pc;
    struct paint pt;
    struct rast rs;
    struct prun pr;
    ogpu_u8 *work, *cells;
    if (!op_ok(op) || (!is_colour(t->format) && t->format != OGPU_FMT_A8)) return OGPU_ERR_UNSUPPORTED;
    if (fl & OGPU_PATH_MASK) {
        if (mslot >= OGPU_MAX_SLOTS || !c->slot[mslot].pixels) return OGPU_ERR_NOSURFACE;
        m = &c->slot[mslot];
        if (!is_colour(m->format) && m->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
    }
    if ((r = paint_init(c, &pt, fl, rd32(a + 4), rd32(a + 8))) != OGPU_OK) return r;
    if (pwords > 0x00FFFFFFUL) return OGPU_ERR_UNSUPPORTED;
    pc.path = pwords ? c->map(c->user, paddr, pwords * 4) : a;
    if (!pc.path) return OGPU_ERR_NOMAP;
    pc.words = (long)pwords; pc.stroke = stroke;
    for (i = 0; i < 6; i++) pc.m[i] = clampl(sgn32(rd32(a + 28 + 4 * i)), 0x3FFFFFFF);
    pc.tol = 16;                                    /* 1/16 pixel */
    if (stroke) {
        ogpu_u32 dcount = rd32(a + 76);
        long sc = (pc.m[0] < 0 ? -pc.m[0] : pc.m[0]) + (pc.m[1] < 0 ? -pc.m[1] : pc.m[1]), s2;
        s2 = (pc.m[3] < 0 ? -pc.m[3] : pc.m[3]) + (pc.m[4] < 0 ? -pc.m[4] : pc.m[4]);
        if (s2 > sc) sc = s2;
        pc.hw = clampl(sgn32(rd32(a + 60)) >> 9, OGPU_COORD_MAX);
        pc.cap = (int)(rd32(a + 64) & 15); pc.join = (int)(rd32(a + 64) & 0xF0);
        pc.miter = clampl(sgn32(rd32(a + 68)), 0xFFFFFF);
        pc.ndash = 0; pc.dash = 0; pc.doff = sgn32(rd32(a + 80)) >> 8;
        if (pc.cap > OGPU_CAP_SQUARE || pc.join > OGPU_JOIN_BEVEL) return OGPU_ERR_UNSUPPORTED;
        if (pc.hw <= 0) return OGPU_OK;
        if (dcount) {
            if (dcount > 0xFFFF) return OGPU_ERR_UNSUPPORTED;
            if (!(pc.dash = c->map(c->user, rd32(a + 72), dcount * 4))) return OGPU_ERR_NOMAP;
            pc.ndash = (int)dcount;
        }
        /* cut in the path's space: the target's 1/16 pixel there */
        pc.tol = sc > 0 ? (long)(((ogpu_s64)16 * 65536) / sc) : 16;
        if (pc.tol < 1) pc.tol = 1;
    }
    /* the path checked, and its control box: (0, 0) too when it doesn't
     * start with MOVE, as it starts there */
    if (pc.words && rd32(pc.path) != OGPU_PATH_MOVE) minx = maxx = miny = maxy = 0;
    for (at = 0; at < pc.words;) {
        ogpu_u32 v = rd32(pc.path + at * 4);
        int np = v == OGPU_PATH_QUAD ? 2 : v == OGPU_PATH_CUBIC ? 3 : v == OGPU_PATH_CLOSE ? 0 : 1;
        if (v > OGPU_PATH_CLOSE || at + 1 + 2 * np > pc.words) return OGPU_ERR_UNSUPPORTED;
        for (at++, k = 0; k < np; k++, at += 2) {
            x = clampl(sgn32(rd32(pc.path + at * 4)), OGPU_COORD_MAX); y = clampl(sgn32(rd32(pc.path + at * 4 + 4)), OGPU_COORD_MAX);
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (minx > maxx && bounded) return OGPU_OK;
    if (stroke && minx <= maxx) {                   /* room for joins and caps */
        ex = clampl((ogpu_s64)(pc.join == OGPU_JOIN_MITER ? (pc.miter >> 16) + 2 : 2) * pc.hw, OGPU_COORD_MAX);
        minx = clampl((ogpu_s64)minx - ex, OGPU_COORD_MAX); miny = clampl((ogpu_s64)miny - ex, OGPU_COORD_MAX);
        maxx = clampl((ogpu_s64)maxx + ex, OGPU_COORD_MAX); maxy = clampl((ogpu_s64)maxy + ex, OGPU_COORD_MAX);
    }
    bounds(c, &rx0, &ry0, &rx1, &ry1);
    if (bounded) {
        long bx0 = 0x7FFFFFFFL, by0 = 0x7FFFFFFFL, bx1 = -0x7FFFFFFFL, by1 = -0x7FFFFFFFL;
        for (i = 0; i < 4; i++) {
            xform(&pc, i & 1 ? maxx : minx, i & 2 ? maxy : miny, &x, &y);
            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
        }
        if ((bx0 >> 8) - 1 > rx0) rx0 = (int)(bx0 >> 8) - 1;
        if ((by0 >> 8) - 1 > ry0) ry0 = (int)(by0 >> 8) - 1;
        if ((bx1 >> 8) + 2 < rx1) rx1 = (int)(bx1 >> 8) + 2;
        if ((by1 >> 8) + 2 < ry1) ry1 = (int)(by1 >> 8) + 2;
    }
    if (rx1 <= rx0 || ry1 <= ry0) return OGPU_OK;
    w = rx1 - rx0; h = ry1 - ry0;
    if (wbytes < (ogpu_u32)w * 8 * OGPU_SUBROWS) return OGPU_ERR_UNSUPPORTED;
    /* The work memory: the walk's edges first, then a band's cells and
     * seen bits (rowb bytes a row). Mapped: what a band of every row takes,
     * and room for OGPU_LIST_BYTES of edges, or all of it if that is less. */
    tw = (w + 31) >> 5;
    rowb = (ogpu_u32)w * 8 * OGPU_SUBROWS + (ogpu_u32)tw * 4;
    bh = (int)(wbytes / rowb < (ogpu_u32)h ? wbytes / rowb : (ogpu_u32)h);
    len = (ogpu_u32)bh * rowb;
    len = wbytes - len > OGPU_LIST_BYTES ? len + OGPU_LIST_BYTES : wbytes;
    if (!(work = c->map(c->user, waddr, len))) return OGPU_ERR_NOMAP;
    if (!aligned4(work)) return OGPU_ERR_UNSUPPORTED;
    rs.w = w; rs.eo = evenodd; rs.tw = tw; rs.seen = 0; rs.list = 0; rs.nlist = 0; rs.collect = 0;
    pc.r = &rs;
    if (len >= rowb + 16) {                         /* walked once, into the list */
        rs.list = (int *)(void *)work; rs.cap = (long)((len - rowb) >> 4); rs.collect = 1;
        rs.ox = rs.oy = 0;
        walk(&pc);
        rs.collect = 0;
        if (rs.nlist > rs.cap) { rs.list = 0; rs.nlist = 0; }     /* too many: walked again for each band */
    }
    cells = work + ((ogpu_u32)rs.nlist << 4);
    len -= (ogpu_u32)rs.nlist << 4;
    if (len >= rowb) {
        bh = (int)(len / rowb < (ogpu_u32)h ? len / rowb : (ogpu_u32)h);
        rs.seen = (ogpu_w32 *)(void *)(cells + (ogpu_u32)bh * w * 8 * OGPU_SUBROWS);
    } else {                                        /* no room for the bits: every cell cleared */
        len /= (ogpu_u32)w * 8 * OGPU_SUBROWS;
        bh = (int)(len < (ogpu_u32)h ? len : (ogpu_u32)h);
    }
    rs.cell = (int *)(void *)cells;
    pr.f = t->format; pr.dbpp = dbpp; pr.op = op; pr.bounded = bounded; pr.mox = mox; pr.m = m; pr.pt = &pt;
    colour = !m && !pt.kind && (t->format == OGPU_FMT_ARGB32 || t->format == OGPU_FMT_XRGB32) && (op == OGPU_PD_OVER || op == OGPU_PD_SRC);
    for (band = ry0; band < ry1; band += bh) {
        rs.h = ry1 - band < bh ? ry1 - band : bh;
        rs.ox = (long)rx0 * 256; rs.oy = (long)band * 256;
        if (rs.seen) for (at = 0; at < (long)tw * rs.h; at++) rs.seen[at] = 0;
        else for (at = 0; at < (long)w * rs.h * 2 * OGPU_SUBROWS; at++) rs.cell[at] = 0;
        if (rs.list) {
            long ylim = rs.oy + ((long)rs.h << 8);
            const int *e = rs.list;
            for (at = 0; at < rs.nlist; at++, e += 4) {
                long ya = e[1], yb = e[3];
                if ((ya > yb ? ya : yb) <= rs.oy || (ya < yb ? ya : yb) >= ylim) continue;   /* not in the band */
                edge_rel(&rs, e[0] - rs.ox, ya - rs.oy, e[2] - rs.ox, yb - rs.oy);
            }
        } else walk(&pc);
        /* the sweep: each pixel the walk wrote, and the runs between them,
         * whose coverage is the sums of cover left of them */
        for (j = 0; j < rs.h; j++) {
            const int *cl = rs.cell + ((long)MUL16(j, w) << 3);
            const ogpu_w32 *sn = rs.seen ? rs.seen + MUL16(j, tw) : 0;
            long acc[OGPU_SUBROWS];
            int q, n;
            unsigned al;
            for (q = 0; q < OGPU_SUBROWS; q++) acc[q] = 0;
            y = band + j;
            pr.mrow = m && y - moy >= 0 && y - moy < m->h ? row_at(m, (int)(y - moy)) : 0;
            pr.dp = px_at(t, rx0, (int)y); pr.x = rx0; pr.y = (int)y;
            paint_row(&pt, rx0, (int)y);
            for (i = 0; i < w; i += n) {
                if (sn && !(sn[i >> 5] & bit_of[i & 31])) {
                    n = unseen(sn, i, w);
                    al = path_cov(acc, 0, evenodd);
                } else {
                    const int *cv = cl + ((long)i << 3);
                    for (q = 0; q < OGPU_SUBROWS; q++) acc[q] += cv[q];
                    al = path_cov(acc, cv + 4, evenodd);
                    n = 1;
                    if (colour) {                   /* a colour's edge pixel, without put_run's call */
                        if (al || !bounded) {
                            struct ch s = pt.col;
                            if (al != 255) { s.a = mul255(s.a, al); s.r = mul255(s.r, al); s.g = mul255(s.g, al); s.b = mul255(s.b, al); }
                            put_src_over(pr.dp, &s, op, t->format == OGPU_FMT_XRGB32);
                        }
                        pr.dp += 4; pr.x++;
                        continue;
                    }
                }
                put_run(&pr, n, al);
            }
        }
    }
    return OGPU_OK;
}

/* ---- v1.6: BLUR ------------------------------------------------------------------------ */

/* One box blur along n pixels of nch bytes: each the mean of lo before it,
 * itself and hi after it (0 outside). */
static void box1(const ogpu_u8 *src, ogpu_u8 *dst, int n, int nch, int lo, int hi) {
    unsigned long d = (unsigned long)(lo + hi + 1), rc = ((1UL << 24) + d / 2) / d, sum;
    int i, k;
    for (k = 0; k < nch; k++) {
        for (sum = 0, i = 0; i < hi && i < n; i++) sum += src[i * nch + k];
        for (i = 0; i < n; i++) {
            if (i + hi < n) sum += src[(i + hi) * nch + k];
            if (i - lo - 1 >= 0) sum -= src[(i - lo - 1) * nch + k];
            dst[i * nch + k] = (ogpu_u8)(((sum * rc + (1UL << 23)) & 0xFFFFFFFFUL) >> 24);
        }
    }
}

/* Three boxes of size d (feGaussianBlur's): odd, three centred; even, two
 * of d either side of the pixel and one of d + 1. a holds the line; the
 * result is in a again (b is scratch). */
static void box3(ogpu_u8 *a, ogpu_u8 *b, int n, int nch, int d) {
    if (d & 1) {
        box1(a, b, n, nch, d / 2, d / 2); box1(b, a, n, nch, d / 2, d / 2);
        box1(a, b, n, nch, d / 2, d / 2);
    } else {
        box1(a, b, n, nch, d / 2, d / 2 - 1); box1(b, a, n, nch, d / 2 - 1, d / 2);
        box1(a, b, n, nch, d / 2, d / 2);
    }
    copy_fwd(a, b, n * nch);
}

/* The box size for a blur radius (12.4): sigma = radius / 2, d = sigma
 * 3 sqrt(2 pi) / 4 + 1/2, rounded down. */
static int box_size(int r) {
    return (int)((((ogpu_s64)r << 11) * 123207 + 0x80000000LL) >> 32);
}

OP_FN op_blur(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), rad = rd32(a + 8), waddr = rd32(a + 12), wbytes = rd32(a + 16);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, bpp = bytes_per_pixel(t->format);
    int dx = box_size(hi_u(rad)), dy = box_size(lo_u(rad)), n;
    ogpu_u8 *work, *b;
    if (t->format != OGPU_FMT_A8 && bpp != 4) return OGPU_ERR_UNSUPPORTED;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy) || (dx < 2 && dy < 2)) return OGPU_OK;
    n = (w > h ? w : h) * bpp;
    if (wbytes < (ogpu_u32)n * 2) return OGPU_ERR_UNSUPPORTED;
    if (!(work = c->map(c->user, waddr, (ogpu_u32)n * 2))) return OGPU_ERR_NOMAP;
    b = work + n;
    if (dx >= 2)
        for (j = 0; j < h; j++) {
            ogpu_u8 *row = px_at(t, x, y + j);
            copy_fwd(work, row, w * bpp);
            box3(work, b, w, bpp, dx);
            copy_fwd(row, work, w * bpp);
        }
    if (dy >= 2)
        for (i = 0; i < w; i++) {
            ogpu_u8 *col = px_at(t, x + i, y), *p;
            for (j = 0, p = col; j < h; j++, p += t->bpr) put_raw(work + j * bpp, bpp, p);
            box3(work, b, h, bpp, dy);
            for (j = 0, p = col; j < h; j++, p += t->bpr) put_raw(p, bpp, work + j * bpp);
        }
    return OGPU_OK;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

/* ---- the run ---------------------------------------------------------------------- */

void ogpu_core_init(struct ogpu_core *c) {
    int i;
    for (i = 0; i < OGPU_MAX_SLOTS; i++) { c->slot[i].pixels = 0; c->slot[i].table = 0; }
    c->target = -1;
    c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0;
    c->last_error = OGPU_OK;
    c->error_word = 0;
    c->d3 = 0;
    c->nfast = 0;
}

/* Words each opcode needs after its header (-1: not a command of the core;
 * -2: the 3D range, whose lengths ogpu_3d.c checks). */
static int args_for(int op) {
    switch (op) {
    case OGPU_OP_NOP: return 0;
    case OGPU_OP_SURFACE: return 5;
    case OGPU_OP_TARGET: return 1;
    case OGPU_OP_CLIP: return 2;
    case OGPU_OP_FILL: case OGPU_OP_INVERT: return 3;
    case OGPU_OP_COPY: return 4;
    case OGPU_OP_TEMPLATE: return 8;
    case OGPU_OP_PATTERN: return 7;
    case OGPU_OP_LINE: return 4;
    case OGPU_OP_PIXELS: return 6;
    case OGPU_OP_MASK: return 5;
    case OGPU_OP_FILL_BLEND: case OGPU_OP_LINES_BLEND: case OGPU_OP_POINTS_BLEND: return 4;
    case OGPU_OP_COMPOSITE: return 7;       /* 9 with OGPU_COMP_MASK, checked in the run */
    case OGPU_OP_COMPOSITE_AFFINE: return 11;
    case OGPU_OP_YUV: return 9;
    case OGPU_OP_YUV_SCALED: return 12;
    case OGPU_OP_COMPOSITE_PD: return 7;
    case OGPU_OP_PATH_FILL: return 15;
    case OGPU_OP_PATH_STROKE: return 21;
    case OGPU_OP_BLUR: return 5;
    case OGPU_OP_PALETTE: return 2;
    case OGPU_OP_FENCE: return 1;
    }
#ifdef OGPU_WITH_3D
    if (op >= 0x0030 && op <= 0x003F) return -2;
#endif
    return -1;
}

long ogpu_core_run(struct ogpu_core *c, const ogpu_u8 *stream, long words) {
    long at = 0, done = 0;
    c->last_error = OGPU_OK;
    while (at < words) {
        ogpu_u32 hdr = rd32(stream + at * 4);
        int op = (int)OGPU_HDR_OP(hdr), len = (int)OGPU_HDR_WORDS(hdr), need = args_for(op), r = OGPU_OK;
        const ogpu_u8 *a = stream + at * 4 + 4;
        if (len < 1 || at + len > words) {
            if (c->last_error == OGPU_OK) { c->last_error = OGPU_ERR_BADLEN; c->error_word = at; }
            break;
        }
        if (need >= 0 && (len - 1 < need
                          || (op == OGPU_OP_COMPOSITE && (rd32(a + 24) & OGPU_COMP_MASK) && len - 1 < 9))) {
            /* Too short for its own arguments: what follows can't be trusted either. */
            if (c->last_error == OGPU_OK) { c->last_error = OGPU_ERR_BADLEN; c->error_word = at; }
            break;
        }
        if (need == -1)
            r = op >= OGPU_OP_EXT_FIRST && op <= OGPU_OP_EXT_LAST && c->ext ? c->ext(c->user, op, stream + at * 4, len)
                                                                           : OGPU_ERR_BADOP;
#ifdef OGPU_WITH_3D
        else if (need == -2)
            r = c->d3 ? ogpu_3d_run(c, op, stream + at * 4, len) : OGPU_ERR_BADOP;
#endif
        else if (op >= OGPU_OP_FILL && op < OGPU_OP_FENCE && op != OGPU_OP_PALETTE && (c->target < 0 || !c->slot[c->target].pixels))
            r = OGPU_ERR_NOSURFACE;
        else switch (op) {
        case OGPU_OP_SURFACE: r = op_surface(c, a); break;
        case OGPU_OP_TARGET: {
            ogpu_u32 slot = rd32(a);
            if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) { c->target = -1; r = OGPU_ERR_NOSURFACE; }
            else { c->target = (int)slot; c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; }
            break;
        }
        case OGPU_OP_CLIP: {
            ogpu_u32 xy = rd32(a), wh = rd32(a + 4);
            if (!hi_u(wh) || !lo_u(wh)) { c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; break; }   /* the whole target */
            c->cx0 = hi_s(xy); c->cy0 = lo_s(xy);
            c->cx1 = c->cx0 + hi_u(wh);
            c->cy1 = c->cy0 + lo_u(wh);
            break;
        }
        case OGPU_OP_FILL: r = op_fill(c, a, 0); break;
        case OGPU_OP_INVERT: r = op_fill(c, a, 1); break;
        case OGPU_OP_COPY: r = op_copy(c, a); break;
        case OGPU_OP_TEMPLATE: r = op_template(c, a); break;
        case OGPU_OP_PATTERN: r = op_pattern(c, a); break;
        case OGPU_OP_LINE: r = op_line(c, a); break;
        case OGPU_OP_PIXELS: r = op_pixels(c, a); break;
        case OGPU_OP_MASK: r = op_mask(c, a); break;
        case OGPU_OP_FILL_BLEND: r = op_fill_blend(c, a); break;
        case OGPU_OP_LINES_BLEND: r = op_lines_blend(c, a); break;
        case OGPU_OP_POINTS_BLEND: r = op_points_blend(c, a); break;
        case OGPU_OP_COMPOSITE: r = op_composite(c, a); break;
        case OGPU_OP_COMPOSITE_AFFINE: r = op_composite_affine(c, a); break;
        case OGPU_OP_YUV: r = op_yuv(c, a); break;
        case OGPU_OP_YUV_SCALED: r = op_yuv_scaled(c, a); break;
        case OGPU_OP_COMPOSITE_PD: r = op_composite_pd(c, a); break;
        case OGPU_OP_PATH_FILL: r = op_path(c, a, 0); break;
        case OGPU_OP_PATH_STROKE: r = op_path(c, a, 1); break;
        case OGPU_OP_BLUR: r = op_blur(c, a); break;
        case OGPU_OP_PALETTE: r = op_palette(c, a); break;
        case OGPU_OP_FENCE: if (c->fence) c->fence(c->user, rd32(a)); break;
        }
        if (r != OGPU_OK && c->last_error == OGPU_OK) { c->last_error = r; c->error_word = at; }
        if (op != OGPU_OP_NOP) done++;
        at += len;
    }
    return done;
}

int ogpu_core_supports(int op, int format) {
    int colour = is_colour(format);
#ifdef OGPU_WITH_3D
    if (op >= 0x0030 && op <= 0x003F) return ogpu3d_supports(op, format);
#endif
    if (format == OGPU_FMT_INDEX8)      /* v1.7: an INDEX8 slot takes indexes, as CLUT8 takes pens */
        return ogpu_core_supports(op, OGPU_FMT_CLUT8);
    if (!colour && format != OGPU_FMT_CLUT8 && format != OGPU_FMT_A8) return OGPU_NONE;
    switch (op) {
    case OGPU_OP_NOP: case OGPU_OP_SURFACE: case OGPU_OP_TARGET: case OGPU_OP_CLIP: case OGPU_OP_FENCE:
    case OGPU_OP_FILL: case OGPU_OP_INVERT: case OGPU_OP_COPY: case OGPU_OP_TEMPLATE:
    case OGPU_OP_PATTERN: case OGPU_OP_LINE: case OGPU_OP_PALETTE:
        return OGPU_FULL;
    case OGPU_OP_PIXELS:    /* pens only on CLUT8, coverage only on A8 */
        return format == OGPU_FMT_CLUT8 || format == OGPU_FMT_A8 ? OGPU_PARTIAL : OGPU_FULL;
    case OGPU_OP_COMPOSITE: case OGPU_OP_MASK: case OGPU_OP_COMPOSITE_PD:
    case OGPU_OP_PATH_FILL: case OGPU_OP_PATH_STROKE:
        return format == OGPU_FMT_CLUT8 ? OGPU_NONE : OGPU_FULL;
    case OGPU_OP_BLUR:      /* coverage, or 32-bit pixels */
        return format == OGPU_FMT_A8 || bytes_per_pixel(format) == 4 ? OGPU_FULL : OGPU_NONE;
    case OGPU_OP_FILL_BLEND: case OGPU_OP_LINES_BLEND: case OGPU_OP_POINTS_BLEND:
    case OGPU_OP_COMPOSITE_AFFINE: case OGPU_OP_YUV: case OGPU_OP_YUV_SCALED:
        return colour ? OGPU_FULL : OGPU_NONE;
    }
    return OGPU_NONE;
}
