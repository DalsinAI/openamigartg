/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * COMPOSITE_PD (stream v1.4) against a plain per-pixel reference written
 * from stream.h's description: every operator, solid and slot sources and
 * masks, repeat, sources and masks partly outside, the clip, and each
 * format (ARGB32, XRGB32, RGB565, A8) as source, mask and target.
 * pixman.library's own test (openamigapixman tests/pixmantest.c) also
 * checks the same command against upstream Pixman. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

#define ARENA (1L << 20)
static ogpu_u8 arena[ARENA], expect[ARENA];

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static unsigned rnd_state = 12345;
static unsigned rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return (rnd_state >> 8) & 0xFFFFFFu; }

struct px { unsigned a, r, g, b; };
static const int fmts[4] = { OGPU_FMT_ARGB32, OGPU_FMT_XRGB32, OGPU_FMT_RGB565, OGPU_FMT_A8 };
static int bpp_of(int f) { return f == OGPU_FMT_RGB565 ? 2 : f == OGPU_FMT_A8 ? 1 : 4; }

static unsigned m255(unsigned x, unsigned y) { unsigned t = x * y + 128; return (t + (t >> 8)) >> 8; }
static unsigned sat(unsigned v) { return v > 255 ? 255 : v; }

static struct px load(const ogpu_u8 *p, int f) {
    struct px o;
    unsigned v;
    switch (f) {
    case OGPU_FMT_ARGB32: o.a = p[0]; o.r = p[1]; o.g = p[2]; o.b = p[3]; break;
    case OGPU_FMT_XRGB32: o.a = 255; o.r = p[1]; o.g = p[2]; o.b = p[3]; break;
    case OGPU_FMT_RGB565:
        v = (unsigned)p[0] << 8 | p[1];
        o.a = 255;
        o.r = ((v >> 11) << 3) | ((v >> 11) >> 2);
        o.g = (((v >> 5) & 63) << 2) | (((v >> 5) & 63) >> 4);
        o.b = ((v & 31) << 3) | ((v & 31) >> 2);
        break;
    default: o.a = p[0]; o.r = o.g = o.b = 0; break;
    }
    return o;
}

static void store(ogpu_u8 *p, int f, struct px c) {
    unsigned v;
    switch (f) {
    case OGPU_FMT_ARGB32: p[0] = (ogpu_u8)c.a; p[1] = (ogpu_u8)c.r; p[2] = (ogpu_u8)c.g; p[3] = (ogpu_u8)c.b; break;
    case OGPU_FMT_XRGB32: p[0] = 0; p[1] = (ogpu_u8)c.r; p[2] = (ogpu_u8)c.g; p[3] = (ogpu_u8)c.b; break;
    case OGPU_FMT_RGB565:
        v = ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
        p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; break;
    default: p[0] = (ogpu_u8)c.a; break;
    }
}

/* the operator, from stream.h's table */
static struct px combine(int op, struct px s, struct px d) {
    unsigned fa = 0, fb = 0;
    struct px o;
    if (op == OGPU_PD_ADD) {
        o.a = sat(s.a + d.a); o.r = sat(s.r + d.r); o.g = sat(s.g + d.g); o.b = sat(s.b + d.b);
        return o;
    }
    switch (op) {
    case OGPU_PD_CLEAR: fa = 0; fb = 0; break;
    case OGPU_PD_SRC: fa = 255; fb = 0; break;
    case OGPU_PD_DST: fa = 0; fb = 255; break;
    case OGPU_PD_OVER: fa = 255; fb = 255 - s.a; break;
    case OGPU_PD_OVER_REVERSE: fa = 255 - d.a; fb = 255; break;
    case OGPU_PD_IN: fa = d.a; fb = 0; break;
    case OGPU_PD_IN_REVERSE: fa = 0; fb = s.a; break;
    case OGPU_PD_OUT: fa = 255 - d.a; fb = 0; break;
    case OGPU_PD_OUT_REVERSE: fa = 0; fb = 255 - s.a; break;
    case OGPU_PD_ATOP: fa = d.a; fb = 255 - s.a; break;
    case OGPU_PD_ATOP_REVERSE: fa = 255 - d.a; fb = s.a; break;
    case OGPU_PD_XOR: fa = 255 - d.a; fb = 255 - s.a; break;
    }
    o.a = sat(m255(s.a, fa) + m255(d.a, fb));
    o.r = sat(m255(s.r, fa) + m255(d.r, fb));
    o.g = sat(m255(s.g, fa) + m255(d.g, fb));
    o.b = sat(m255(s.b, fa) + m255(d.b, fb));
    return o;
}

/* premultiplied random pixels: channels never above alpha */
static void fill_random(ogpu_u8 *p, int f, int w, int h) {
    int i, n = w * h;
    for (i = 0; i < n; i++) {
        struct px c;
        unsigned k = rnd() % 4;
        c.a = k == 0 ? 0 : k == 1 ? 255 : rnd() & 255;
        c.r = c.a ? rnd() % (c.a + 1) : 0; c.g = c.a ? rnd() % (c.a + 1) : 0; c.b = c.a ? rnd() % (c.a + 1) : 0;
        if (f == OGPU_FMT_XRGB32 || f == OGPU_FMT_RGB565) { c.r = rnd() & 255; c.g = rnd() & 255; c.b = rnd() & 255; }
        store(p + i * bpp_of(f), f, c);
    }
}

static int wrapi(int v, int n) { v %= n; return v < 0 ? v + n : v; }

int main(void) {
    struct ogpu_core C;
    static ogpu_u8 stream[4096];
    int iter, failures = 0, cases = 0;
    const unsigned long SRC = 0x10000, MSK = 0x40000, DST = 0x80000;
    for (iter = 0; iter < 4000; iter++) {
        int df = fmts[rnd() % 4], sf = fmts[rnd() % 4], mf = rnd() % 2 ? OGPU_FMT_A8 : OGPU_FMT_ARGB32;
        int dw = 1 + rnd() % 40, dh = 1 + rnd() % 30, sw = 1 + rnd() % 24, sh = 1 + rnd() % 20, mw = 1 + rnd() % 24, mh = 1 + rnd() % 20;
        int op = (int)(rnd() % OGPU_PD_OPS), kind = (int)(rnd() % 3), mkind = (int)(rnd() % 3);
        int srep = rnd() % 3 == 0, mrep = rnd() % 3 == 0;
        int x = (int)(rnd() % 50) - 10, y = (int)(rnd() % 40) - 10, w = (int)(rnd() % 50), h = (int)(rnd() % 40);
        int sx = (int)(rnd() % 30) - 8, sy = (int)(rnd() % 30) - 8, mx = (int)(rnd() % 30) - 8, my = (int)(rnd() % 30) - 8;
        int clip = rnd() % 4 == 0, cx = (int)(rnd() % 20), cy = (int)(rnd() % 20), cw = 1 + (int)(rnd() % 30), ch = 1 + (int)(rnd() % 30);
        unsigned long solid, malpha = rnd() & 255, of = (unsigned long)op;
        struct px sc;
        struct OGPUBatch b;
        int i, j, dbpr = dw * bpp_of(df), sbpr = sw * bpp_of(sf), mbpr = mw * bpp_of(mf);
        sc.a = rnd() & 255; sc.r = sc.a ? rnd() % (sc.a + 1) : 0; sc.g = sc.a ? rnd() % (sc.a + 1) : 0; sc.b = sc.a ? rnd() % (sc.a + 1) : 0;
        solid = (unsigned long)sc.a << 24 | sc.r << 16 | sc.g << 8 | sc.b;
        if (kind == 0) of |= OGPU_PD_SOLID;
        if (mkind == 1) of |= OGPU_PD_MASK;
        if (mkind == 2) of |= OGPU_PD_MASK_SOLID;
        if (srep) of |= OGPU_PD_SRC_REPEAT;
        if (mrep) of |= OGPU_PD_MASK_REPEAT;
        fill_random(arena + SRC, sf, sw, sh);
        fill_random(arena + MSK, mf, mw, mh);
        fill_random(arena + DST, df, dw, dh);
        memcpy(expect + DST, arena + DST, (size_t)(dbpr * dh));
        /* the reference */
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++) {
                int px = x + i, py = y + j, u, v;
                struct px s, d;
                unsigned ma = mkind == 2 ? (unsigned)malpha : 255;
                if (px < 0 || py < 0 || px >= dw || py >= dh) continue;
                if (clip && (px < cx || py < cy || px >= cx + cw || py >= cy + ch)) continue;
                if (kind == 0) s = sc;
                else {
                    u = sx + i; v = sy + j;
                    if (srep) { u = wrapi(u, sw); v = wrapi(v, sh); }
                    if (u >= 0 && v >= 0 && u < sw && v < sh) s = load(arena + SRC + v * sbpr + u * bpp_of(sf), sf);
                    else s.a = s.r = s.g = s.b = 0;
                }
                if (mkind == 1) {
                    u = mx + i; v = my + j;
                    if (mrep) { u = wrapi(u, mw); v = wrapi(v, mh); }
                    ma = (u >= 0 && v >= 0 && u < mw && v < mh) ? load(arena + MSK + v * mbpr + u * bpp_of(mf), mf).a : 0;
                }
                s.a = m255(s.a, ma); s.r = m255(s.r, ma); s.g = m255(s.g, ma); s.b = m255(s.b, ma);
                d = load(expect + DST + py * dbpr + px * bpp_of(df), df);
                store(expect + DST + py * dbpr + px * bpp_of(df), df, combine(op, s, d));
            }
        ogpu_batch_init(&b, stream, (long)sizeof stream / 4);
        ogpu_surface(&b, 0, DST, (unsigned long)dbpr, dw, dh, df);
        ogpu_surface(&b, 1, SRC, (unsigned long)sbpr, sw, sh, sf);
        ogpu_surface(&b, 2, MSK, (unsigned long)mbpr, mw, mh, mf);
        ogpu_target(&b, 0);
        if (clip) ogpu_clip(&b, cx, cy, cw, ch);
        ogpu_composite_pd(&b, of, kind == 0 ? solid : 1, sx, sy, mkind == 1 ? 2 : malpha, mx, my, x, y, w, h);
        ogpu_core_init(&C);
        C.map = map; C.user = 0;
        ogpu_core_run(&C, stream, b.words);
        cases++;
        if (C.last_error != OGPU_OK || memcmp(expect + DST, arena + DST, (size_t)(dbpr * dh))) {
            if (++failures <= 10)
                printf("FAIL case %d: op %d flags %04lx dst %d src %d mask %d, error %d\n", iter, op, of & 0xFF00UL, df, sf, mf, C.last_error);
        }
    }
    /* what the core says it does */
    if (ogpu_core_supports(OGPU_OP_COMPOSITE_PD, OGPU_FMT_ARGB32) != OGPU_FULL ||
        ogpu_core_supports(OGPU_OP_COMPOSITE_PD, OGPU_FMT_CLUT8) != OGPU_NONE) { failures++; printf("FAIL supports\n"); }
    printf("composite_pd: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
