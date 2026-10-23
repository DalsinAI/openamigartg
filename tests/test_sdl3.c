/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream v1.7 (what SDL 3.4's renderer needs: stream.h) on the core and the
 * 3D rasteriser's SDL form, against references written here:
 *   - SDL 3's premultiplied blend modes, from SDL_blendmode.h's formulas,
 *     with the core's rounding (each product rounded, the sum saturating):
 *     exact, TRIANGLES too, with a vertex colour and into every 32-bit order;
 *   - PIXELART, from SDL's GPU renderers' GetPixelArtUV() in doubles: within
 *     3 a channel (the weights' rounding) on small pictures, and within 2
 *     across a 2000-pixel row made 5 to 20 times wider, so no drift;
 *   - TRIANGLES' clamping one way: the texel nearest each pixel centre;
 *   - PALETTE: an INDEX8 slot draws exactly as the ARGB32 picture its table
 *     makes, in every command, filter and blend that reads it, and the
 *     table is read when the slot is drawn from;
 *   - RGBA32 and ABGR32 (and BGRA32 and XRGB32 as TRIANGLES targets): the
 *     same pixels as ARGB32, in their own byte order; TRIANGLES reads the
 *     16-bit PC and 555 orders as their ARGB32 copies;
 *   - P010: exactly NV12's pixels from its samples' top bytes, rounded; the
 *     new YUV colour spaces from their matrices in doubles: within 2;
 *   - OGPU_Query's answers, and opengpu.library's batch check
 *     (ogpu_variant.h) for a v1.6 driver and a v1.7 one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../library/opengpu/ogpu_core.h"
#include "../library/opengpu/ogpu_3d.h"
#include "../library/opengpu/ogpu_variant.h"
#include "../include/opengpu/build.h"
#include "../include/opengpu/build3d.h"

#define ARENA (4L << 20)
static ogpu_u8 arena[ARENA];
#define TA    0x000000UL      /* target A, up to 64 x 64 x 4 */
#define TB    0x004000UL      /* target B */
#define SA    0x010000UL      /* source A */
#define SB    0x014000UL      /* source B */
#define MK    0x018000UL      /* an A8 mask */
#define PAL   0x020000UL      /* a palette */
#define VTX   0x021000UL      /* vertices */
#define PTS   0x022000UL      /* points */
#define YP    0x030000UL      /* YUV planes */
#define UVP   0x040000UL
#define VP    0x050000UL
#define YP2   0x060000UL      /* P010's */
#define UVP2  0x070000UL

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= (ogpu_u32)ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures, checks;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { failures++; if (failures < 60) printf("FAIL %s\n", what); }
}

static ogpu_u8 sbuf[1 << 16];
static struct OGPUBatch B;
static struct ogpu_core C;
static struct ogpu3d T;
static int slow_spans;

static void begin(void) { ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4); }
static int run(void) {
    ogpu_core_init(&C);
    C.map = map; C.fence = 0; C.ext = 0; C.user = 0;
    ogpu3d_init(&T, &C);
    T.slow = slow_spans;
    ogpu3d_run(&T, sbuf, B.words);
    return C.last_error;
}

static unsigned long rs = 1234567;
static unsigned rnd8(void) { rs = (rs * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (unsigned)(rs >> 16) & 255; }

static void put32(unsigned long a, unsigned long v) {
    arena[a] = (ogpu_u8)(v >> 24); arena[a + 1] = (ogpu_u8)(v >> 16); arena[a + 2] = (ogpu_u8)(v >> 8); arena[a + 3] = (ogpu_u8)v;
}
static unsigned mul255(unsigned x, unsigned y) { unsigned t = x * y + 128; return (t + (t >> 8)) >> 8; }
static unsigned sat(unsigned v) { return v > 255 ? 255 : v; }

/* channels in and out of a 32-bit format's bytes */
struct px { unsigned a, r, g, b; };
static void to_fmt(int f, struct px c, ogpu_u8 *p) {
    switch (f) {
    case OGPU_FMT_ARGB32: p[0] = (ogpu_u8)c.a; p[1] = (ogpu_u8)c.r; p[2] = (ogpu_u8)c.g; p[3] = (ogpu_u8)c.b; return;
    case OGPU_FMT_XRGB32: p[0] = 0; p[1] = (ogpu_u8)c.r; p[2] = (ogpu_u8)c.g; p[3] = (ogpu_u8)c.b; return;
    case OGPU_FMT_BGRA32: p[0] = (ogpu_u8)c.b; p[1] = (ogpu_u8)c.g; p[2] = (ogpu_u8)c.r; p[3] = (ogpu_u8)c.a; return;
    case OGPU_FMT_RGBA32: p[0] = (ogpu_u8)c.r; p[1] = (ogpu_u8)c.g; p[2] = (ogpu_u8)c.b; p[3] = (ogpu_u8)c.a; return;
    case OGPU_FMT_ABGR32: p[0] = (ogpu_u8)c.a; p[1] = (ogpu_u8)c.b; p[2] = (ogpu_u8)c.g; p[3] = (ogpu_u8)c.r; return;
    }
}
static struct px of_fmt(int f, const ogpu_u8 *p) {
    struct px c = { 0, 0, 0, 0 };
    switch (f) {
    case OGPU_FMT_ARGB32: c.a = p[0]; c.r = p[1]; c.g = p[2]; c.b = p[3]; break;
    case OGPU_FMT_XRGB32: c.a = 255; c.r = p[1]; c.g = p[2]; c.b = p[3]; break;
    case OGPU_FMT_BGRA32: c.b = p[0]; c.g = p[1]; c.r = p[2]; c.a = p[3]; break;
    case OGPU_FMT_RGBA32: c.r = p[0]; c.g = p[1]; c.b = p[2]; c.a = p[3]; break;
    case OGPU_FMT_ABGR32: c.a = p[0]; c.b = p[1]; c.g = p[2]; c.r = p[3]; break;
    }
    return c;
}
static struct px argb_at(unsigned long a) { return of_fmt(OGPU_FMT_ARGB32, arena + a); }
static struct px rnd_px(void) { struct px c; c.a = rnd8(); c.r = rnd8(); c.g = rnd8(); c.b = rnd8(); return c; }
static int same_px(struct px x, struct px y, int tol, int alpha) {
    return abs((int)x.r - (int)y.r) <= tol && abs((int)x.g - (int)y.g) <= tol && abs((int)x.b - (int)y.b) <= tol
           && (!alpha || abs((int)x.a - (int)y.a) <= tol);
}

/* a w x h picture of random pixels in a format; opaque forces alpha 255 */
static void picture(unsigned long a, int w, int h, int f, int opaque) {
    int i;
    for (i = 0; i < w * h; i++) { struct px c = rnd_px(); if (opaque) c.a = 255; to_fmt(f, c, arena + a + (unsigned long)i * 4); }
}
/* the same picture in another format */
static void convert(unsigned long from, int ff, unsigned long to, int tf, int n) {
    int i;
    for (i = 0; i < n; i++) to_fmt(tf, of_fmt(ff, arena + from + (unsigned long)i * 4), arena + to + (unsigned long)i * 4);
}

/* a quad of two triangles at VTX: (x0, y0)-(x1, y1) with u, v from u0, v0 to u1, v1 */
static void quad(double x0, double y0, double x1, double y1, double u0, double v0, double u1, double v1, unsigned long argb) {
    double X[6] = { x0, x1, x1, x0, x1, x0 }, Y[6] = { y0, y0, y1, y0, y1, y1 };
    double U[6] = { u0, u1, u1, u0, u1, u0 }, V[6] = { v0, v0, v1, v0, v1, v1 };
    int i;
    for (i = 0; i < 6; i++) {
        unsigned long a = VTX + (unsigned long)i * 20;
        put32(a, (unsigned long)(long)floor(X[i] * 65536 + 0.5) & 0xFFFFFFFFUL);
        put32(a + 4, (unsigned long)(long)floor(Y[i] * 65536 + 0.5) & 0xFFFFFFFFUL);
        put32(a + 8, argb);
        put32(a + 12, (unsigned long)(long)floor(U[i] * 65536 + 0.5) & 0xFFFFFFFFUL);
        put32(a + 16, (unsigned long)(long)floor(V[i] * 65536 + 0.5) & 0xFFFFFFFFUL);
    }
}

/* ---- the byte orders ---------------------------------------------------------------- */

static void test_formats(void) {
    static const int fm[] = { OGPU_FMT_RGBA32, OGPU_FMT_ABGR32, OGPU_FMT_BGRA32, OGPU_FMT_XRGB32 };
    static const long rot[6] = { 50000, -30000, 20 << 16, 30000, 50000, 3 << 16 };
    int k, op, i;
    char what[120];
    for (k = 0; k < 4; k++) {
        int f = fm[k];
        for (op = 0; op < 9; op++) {
            int r1 = -99, r2 = -99, bad = 0, opaque = f == OGPU_FMT_XRGB32;
            /* the same source and target pictures in ARGB32 and in f */
            picture(SA, 16, 16, OGPU_FMT_ARGB32, opaque); convert(SA, OGPU_FMT_ARGB32, SB, f, 256);
            picture(TA, 32, 32, OGPU_FMT_ARGB32, opaque); convert(TA, OGPU_FMT_ARGB32, TB, f, 1024);
            for (i = 0; i < 2; i++) {
                int fs = i ? f : OGPU_FMT_ARGB32;
                begin();
                ogpu_surface(&B, 1, i ? SB : SA, 64, 16, 16, fs);
                ogpu_surface(&B, 0, i ? TB : TA, 128, 32, 32, fs);
                ogpu_target(&B, 0);
                switch (op) {
                case 0: ogpu_composite(&B, 1, 0, 0, 16, 16, 2, 3, 23, 19, 200, OGPU_COMP_SRCALPHA); break;
                case 1: ogpu_composite(&B, 1, 1, 2, 13, 12, 0, 0, 30, 29, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR); break;
                case 2: ogpu_composite(&B, 1, 0, 0, 16, 16, 4, 4, 16, 16, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(OGPU_BLEND_BLEND)); break;
                case 3: ogpu_composite(&B, 1, 0, 0, 16, 16, 0, 0, 32, 32, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_PIXELART); break;
                case 4: ogpu_composite_affine(&B, 1, 0, 0, 16, 16, rot, 0xC0FF8040UL, OGPU_AFF_SRCALPHA | OGPU_AFF_BILINEAR | OGPU_BLEND_BLEND); break;
                case 5: ogpu_fill_blend(&B, 3, 5, 20, 20, 0x80406080UL, OGPU_BLEND_BLEND_PREMUL); break;
                case 6: quad(1, 2, 31, 30, 0, 0, 1, 1, 0xFFFFFFFFUL);
                        ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_BLEND_BLEND | OGPU_TRI_BILINEAR); break;
                case 7: quad(0, 0, 32, 32, 0, 0, 1, 1, 0xE0FFC080UL);
                        ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_BLEND_PREMUL | OGPU_TRI_PIXELART | OGPU_TRI_CLAMP); break;
                case 8: ogpu_copy(&B, 1, 0, 0, 5, 5, 16, 16); break;
                }
                if (i) r2 = run(); else r1 = run();
            }
            for (i = 0; i < 1024 && !bad; i++) {
                struct px x = argb_at(TA + (unsigned long)i * 4), y = of_fmt(f, arena + TB + (unsigned long)i * 4);
                if (!same_px(x, y, 0, f != OGPU_FMT_XRGB32)) bad = 1;
                if (f == OGPU_FMT_XRGB32 && arena[TB + (unsigned long)i * 4]) bad = 1;
            }
            sprintf(what, "format %d, case %d: the ARGB32 pixels in its own order (%d %d)", f, op, r1, r2);
            check(r1 == OGPU_OK && r2 == OGPU_OK && !bad, what);
        }
    }
    /* FILL takes the raw bytes; INVERT flips them all; COMPLEMENT keeps the alpha byte */
    begin();
    ogpu_surface(&B, 0, TA, 16, 4, 1, OGPU_FMT_RGBA32);
    ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, 4, 1, 0x11223344UL);
    ogpu_invert(&B, 1, 0, 1, 1, 0xFFFFFFFFUL);
    ogpu_line(&B, 2, 0, 2, 0, 0, OGPU_COMPLEMENT);
    check(run() == OGPU_OK && !memcmp(arena + TA, "\x11\x22\x33\x44\xEE\xDD\xCC\xBB\xEE\xDD\xCC\x44", 12),
          "RGBA32: FILL's raw bytes, INVERT, COMPLEMENT leaving A");
    begin();
    ogpu_surface(&B, 0, TA, 16, 4, 1, OGPU_FMT_ABGR32);
    ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, 4, 1, 0x11223344UL);
    ogpu_line(&B, 2, 0, 2, 0, 0, OGPU_COMPLEMENT);
    check(run() == OGPU_OK && !memcmp(arena + TA + 8, "\x11\xDD\xCC\xBB", 4), "ABGR32: COMPLEMENT leaves A, its first byte");
    /* PIXELS between the orders */
    picture(SA, 8, 1, OGPU_FMT_ARGB32, 0);
    begin();
    ogpu_surface(&B, 0, TA, 32, 8, 1, OGPU_FMT_ABGR32);
    ogpu_target(&B, 0);
    ogpu_pixels(&B, SA, 32, OGPU_FMT_ARGB32, 0, 0, 0, 8, 1);
    ogpu_surface(&B, 2, TB, 32, 8, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 2);
    ogpu_pixels(&B, TA, 32, OGPU_FMT_ABGR32, 0, 0, 0, 8, 1);
    check(run() == OGPU_OK && !memcmp(arena + SA, arena + TB, 32), "PIXELS: ARGB32 to ABGR32 and back");
    /* TRIANGLES: the 3D form doesn't take them as targets */
    begin();
    ogpu_surface(&B, 0, TA, 128, 32, 32, OGPU_FMT_XRGB32);
    ogpu_target(&B, 0);
    quad(0, 0, 8, 8, 0, 0, 1, 1, 0xFFFFFFFFUL);
    ogpu_triangles3d(&B, VTX, 6, 20, 0, 0, 1, OGPU_TRI_NOTEX, 0, OGPU_LAY_LIST);
    check(run() == OGPU_ERR_UNSUPPORTED, "TRIANGLES' 3D form into XRGB32: UNSUPPORTED");
    /* TRIANGLES reads the 16-bit PC and 555 orders as the 2D commands do:
     * the same picture as its ARGB32 copy (made by PIXELS). The 3D form's
     * slot textures go through the same reader. */
    {
        static const int f16[3] = { OGPU_FMT_RGB565PC, OGPU_FMT_RGB555, OGPU_FMT_RGB555PC };
        static ogpu_u8 want[16 * 16 * 4];
        int kk, form, pass;
        for (kk = 0; kk < 3; kk++)
            for (form = 0; form < 1; form++) {
                int r1, r2;
                for (i = 0; i < 16 * 16 * 2; i++) arena[SB + (unsigned long)i] = (ogpu_u8)rnd8();
                begin();                                    /* the ARGB32 copy */
                ogpu_surface(&B, 0, SA, 64, 16, 16, OGPU_FMT_ARGB32);
                ogpu_target(&B, 0);
                ogpu_pixels(&B, SB, 32, f16[kk], 0, 0, 0, 16, 16);
                r1 = run();
                quad(0, 0, 16, 16, 0, 0, 1, 1, 0xFFFFFFFFUL);
                for (pass = 0; pass < 2; pass++) {
                    memset(arena + TA, 0, 16 * 16 * 4);
                    begin();
                    ogpu_surface(&B, 0, TA, 64, 16, 16, OGPU_FMT_ARGB32);
                    ogpu_surface(&B, 1, pass ? SB : SA, pass ? 32 : 64, 16, 16, pass ? f16[kk] : OGPU_FMT_ARGB32);
                    ogpu_target(&B, 0);
                    if (form) ogpu_triangles3d(&B, VTX, 6, 20, 0, 0, 1, 1, 0, OGPU_LAY_LIST);
                    else ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, 0);
                    r2 = run();
                    if (!pass) memcpy(want, arena + TA, sizeof want);
                }
                sprintf(what, "TRIANGLES from format %d: its ARGB32 copy's picture", f16[kk]);
                check(r1 == OGPU_OK && r2 == OGPU_OK && !memcmp(want, arena + TA, sizeof want)
                      && memcmp(want, want + 4 * 17, 4) && memcmp(want, want + 4 * 100, 4), what);
            }
    }
}

/* ---- SDL 3's premultiplied blend modes ----------------------------------------------- */

static struct px ref_premul(int mode, struct px d, struct px s, unsigned sa) {
    struct px o;
    if (mode == OGPU_BLEND_BLEND_PREMUL) {
        if (sa == 255) { o = s; o.a = 255; return o; }
        o.r = sat(s.r + mul255(d.r, 255 - sa)); o.g = sat(s.g + mul255(d.g, 255 - sa)); o.b = sat(s.b + mul255(d.b, 255 - sa));
        o.a = sat(sa + mul255(d.a, 255 - sa));
    } else {
        o.r = sat(d.r + s.r); o.g = sat(d.g + s.g); o.b = sat(d.b + s.b); o.a = d.a;
    }
    return o;
}

static void test_premul(void) {
    static const long id[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
    int mode, op, i, bad;
    char what[100];
    for (mode = OGPU_BLEND_BLEND_PREMUL; mode <= OGPU_BLEND_ADD_PREMUL; mode++) {
        for (op = 0; op < 7; op++) {
            struct px col = { 0xA0, 0x60, 0x90, 0xB0 };       /* r and b above a: saturates in the blend */
            unsigned long argb = ((unsigned long)col.a << 24) | (col.r << 16) | (col.g << 8) | col.b;
            static ogpu_u8 before[16 * 16 * 4];
            int r;
            picture(TA, 16, 16, OGPU_FMT_ARGB32, 0);
            picture(SA, 16, 16, OGPU_FMT_ARGB32, 0);
            for (i = 0; i < 256; i++) arena[MK + i] = (ogpu_u8)rnd8();
            memcpy(before, arena + TA, sizeof before);
            for (i = 0; i < 16; i++) put32(PTS + (unsigned long)i * 4, OGPU_XY(i, 7));
            begin();
            ogpu_surface(&B, 0, TA, 64, 16, 16, OGPU_FMT_ARGB32);
            ogpu_surface(&B, 1, SA, 64, 16, 16, OGPU_FMT_ARGB32);
            ogpu_surface(&B, 2, MK, 16, 16, 16, OGPU_FMT_A8);
            ogpu_target(&B, 0);
            switch (op) {
            case 0: ogpu_fill_blend(&B, 0, 0, 16, 16, argb, mode); break;
            case 1: ogpu_points_blend(&B, PTS, 16, argb, mode); break;
            case 2: ogpu_lines_blend(&B, PTS, 2, argb, (unsigned long)mode | OGPU_LINES_LAST); put32(PTS + 4, OGPU_XY(15, 7)); break;
            case 3: ogpu_composite(&B, 1, 0, 0, 16, 16, 0, 0, 16, 16, 180, OGPU_COMP_SRCALPHA | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(mode)); break;
            case 4: ogpu_composite_masked(&B, 1, 0, 0, 16, 16, 0, 0, 16, 16, 255,
                                          OGPU_COMP_SRCALPHA | OGPU_COMP_MASK | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(mode), 2, 0, 0); break;
            case 5: ogpu_composite_affine(&B, 1, 0, 0, 16, 16, id, 0xC0FF8040UL, OGPU_AFF_SRCALPHA | (unsigned long)mode); break;
            case 6: quad(0, 0, 16, 16, 0, 0, 1, 1, 0xC0FF8040UL);    /* a vertex colour: each channel times the texel's */
                    ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, mode == OGPU_BLEND_BLEND_PREMUL ? OGPU_TRI_BLEND_PREMUL : OGPU_TRI_BLEND_ADD_PREMUL); break;
            }
            r = run();
            bad = 0;
            for (i = 0; i < 256; i++) {
                struct px d, s, want, got = argb_at(TA + (unsigned long)i * 4);
                unsigned sa = 0;
                int x = i & 15, y = i >> 4, hit = 1;
                d = of_fmt(OGPU_FMT_ARGB32, before + i * 4);
                s = argb_at(SA + (unsigned long)i * 4);
                switch (op) {
                case 0: s = col; sa = col.a; break;
                case 1: case 2: s = col; sa = col.a; hit = y == 7; break;
                case 3: sa = mul255(s.a, 180); break;
                case 4: { unsigned m = arena[MK + i]; sa = mul255(mul255(s.a, 255), m);
                          s.r = mul255(s.r, m); s.g = mul255(s.g, m); s.b = mul255(s.b, m); } break;
                case 5: sa = mul255(s.a, 0xC0); s.g = mul255(s.g, 0x80); s.b = mul255(s.b, 0x40); break;
                case 6: s.g = mul255(s.g, 0x80); s.b = mul255(s.b, 0x40); s.a = mul255(s.a, 0xC0); sa = s.a; break;
                }
                (void)x;
                if (!hit) want = d;
                else want = ref_premul(mode, d, s, sa);     /* TRIANGLES too: ADD_PREMUL keeps the target's alpha */
                if (!same_px(got, want, 0, 1)) {
                    if (!bad) printf("  op %d mode %d at %d: got %u %u %u %u, want %u %u %u %u\n", op, mode, i,
                                     got.a, got.r, got.g, got.b, want.a, want.r, want.g, want.b);
                    bad++;
                }
            }
            sprintf(what, "premultiplied blend %d, command %d: SDL 3's formula (%d off, error %d)", mode, op, bad, r);
            check(r == OGPU_OK && !bad, what);
        }
    }
    /* TRIANGLES into the other 32-bit orders: the same formula in their own
     * bytes (ADD_PREMUL keeps each order's alpha byte; XRGB32 has none) */
    {
        static const int fmts[4] = { OGPU_FMT_BGRA32, OGPU_FMT_RGBA32, OGPU_FMT_ABGR32, OGPU_FMT_XRGB32 };
        int k;
        for (mode = OGPU_BLEND_BLEND_PREMUL; mode <= OGPU_BLEND_ADD_PREMUL; mode++)
            for (k = 0; k < 4; k++) {
                static ogpu_u8 before[16 * 16 * 4];
                int r;
                picture(TB, 16, 16, fmts[k], 0);
                picture(SA, 16, 16, OGPU_FMT_ARGB32, 0);
                memcpy(before, arena + TB, sizeof before);
                quad(0, 0, 16, 16, 0, 0, 1, 1, 0xC0FF8040UL);
                begin();
                ogpu_surface(&B, 0, TB, 64, 16, 16, fmts[k]);
                ogpu_surface(&B, 1, SA, 64, 16, 16, OGPU_FMT_ARGB32);
                ogpu_target(&B, 0);
                ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, mode == OGPU_BLEND_BLEND_PREMUL ? OGPU_TRI_BLEND_PREMUL : OGPU_TRI_BLEND_ADD_PREMUL);
                r = run();
                bad = 0;
                for (i = 0; i < 256; i++) {
                    struct px d = of_fmt(fmts[k], before + i * 4), s = argb_at(SA + (unsigned long)i * 4), want, got;
                    got = of_fmt(fmts[k], arena + TB + (unsigned long)i * 4);
                    s.g = mul255(s.g, 0x80); s.b = mul255(s.b, 0x40); s.a = mul255(s.a, 0xC0);
                    want = ref_premul(mode, d, s, s.a);
                    if (fmts[k] == OGPU_FMT_XRGB32) want.a = 255;
                    if (!same_px(got, want, 0, 1) || (fmts[k] == OGPU_FMT_XRGB32 && arena[TB + (unsigned long)i * 4])) bad++;
                }
                sprintf(what, "TRIANGLES premultiplied blend %d into format %d: SDL 3's formula in its bytes (%d off)", mode, fmts[k], bad);
                check(r == OGPU_OK && !bad, what);
            }
    }
    /* 7 is no blend mode */
    for (op = 0; op < 6; op++) {
        begin();
        ogpu_surface(&B, 0, TA, 64, 16, 16, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, SA, 64, 16, 16, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        switch (op) {
        case 0: ogpu_fill_blend(&B, 0, 0, 4, 4, 0xFF000000UL, 7); break;
        case 1: ogpu_points_blend(&B, PTS, 4, 0xFF000000UL, 7); break;
        case 2: ogpu_lines_blend(&B, PTS, 2, 0xFF000000UL, 7); break;
        case 3: ogpu_composite(&B, 1, 0, 0, 4, 4, 0, 0, 4, 4, 255, OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(7)); break;
        case 4: ogpu_composite_affine(&B, 1, 0, 0, 4, 4, id, 0xFFFFFFFFUL, 7); break;
        case 5: quad(0, 0, 4, 4, 0, 0, 1, 1, 0xFFFFFFFFUL); ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, 7); break;
        }
        sprintf(what, "blend mode 7 in command %d: UNSUPPORTED", op);
        check(run() == OGPU_ERR_UNSUPPORTED, what);
    }
}

/* ---- PIXELART ------------------------------------------------------------------------ */

static double smooth(double e0, double e1, double x) {
    double t = (x - e0) / (e1 - e0);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}
/* SDL's GetPixelArtUV in texels, then a bilinear sample clamped to the w x h picture at src */
static void ref_pixelart(unsigned long src, int w, int h, double u, double v, double bu, double bv, double *o) {
    double tx, ty, ox, oy, sx, sy, fxw, fyw;
    int x0, y0, x1, y1, k;
    if (bu > 1) bu = 1;
    if (bv > 1) bv = 1;
    if (bu < 1.0 / 256) bu = 1.0 / 256;
    if (bv < 1.0 / 256) bv = 1.0 / 256;
    tx = u - 0.5 * bu; ty = v - 0.5 * bv;
    ox = smooth(1 - bu, 1, tx - floor(tx)); oy = smooth(1 - bv, 1, ty - floor(ty));
    sx = floor(tx) + ox; sy = floor(ty) + oy;          /* the bilinear position, texel corners at integers */
    x0 = (int)floor(sx); y0 = (int)floor(sy); fxw = sx - x0; fyw = sy - y0;
    x1 = x0 + 1; y1 = y0 + 1;
    x0 = x0 < 0 ? 0 : x0 > w - 1 ? w - 1 : x0;
    x1 = x1 < 0 ? 0 : x1 > w - 1 ? w - 1 : x1;
    y0 = y0 < 0 ? 0 : y0 > h - 1 ? h - 1 : y0;
    y1 = y1 < 0 ? 0 : y1 > h - 1 ? h - 1 : y1;
    for (k = 0; k < 4; k++) {
        double p00 = arena[src + ((unsigned long)y0 * w + x0) * 4 + k], p10 = arena[src + ((unsigned long)y0 * w + x1) * 4 + k];
        double p01 = arena[src + ((unsigned long)y1 * w + x0) * 4 + k], p11 = arena[src + ((unsigned long)y1 * w + x1) * 4 + k];
        o[k] = (p00 * (1 - fxw) + p10 * fxw) * (1 - fyw) + (p01 * (1 - fxw) + p11 * fxw) * fyw;
    }
}

static void test_pixelart(void) {
    static const int dw[4] = { 24, 32, 21, 5 }, dh[4] = { 24, 32, 13, 5 };
    double ang = 0.5, sc = 2.5;
    long m[6];
    int k, x, y, bad, worst, exact, inner;
    char what[100];
    /* COMPOSITE: three, four times larger; an odd size; smaller */
    for (k = 0; k < 4; k++) {
        picture(SA, 8, 8, OGPU_FMT_ARGB32, 0);
        memset(arena + TA, 0, 64 * 64 * 4);
        begin();
        ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_composite(&B, 1, 0, 0, 8, 8, 0, 0, dw[k], dh[k], 255, OGPU_COMP_PIXELART);     /* NONE of SRCALPHA: a copy */
        check(run() == OGPU_OK, "COMPOSITE with PIXELART runs");
        bad = worst = exact = inner = 0;
        for (y = 0; y < dh[k]; y++)
            for (x = 0; x < dw[k]; x++) {
                double bu = 8.0 / dw[k], bv = 8.0 / dh[k], o[4], u = (x + 0.5) * bu, v = (y + 0.5) * bv;
                const ogpu_u8 *p = arena + TA + ((unsigned long)y * 64 + x) * 4;
                int c, e = 0;
                ref_pixelart(SA, 8, 8, u, v, bu, bv, o);
                for (c = 1; c < 4; c++) { int d = abs((int)p[c] - (int)floor(o[c] + 0.5)); if (d > e) e = d; }
                if (e > worst) worst = e;
                if (e > 3) bad++;                      /* the weights' rounding, 1/256ths twice */
                /* inside a texel, at whole magnifications: that texel exactly */
                if (k < 2) {
                    double tx = u - 0.5 * bu, ty = v - 0.5 * bv;
                    if (tx - floor(tx) <= 1 - bu - 0.01 && ty - floor(ty) <= 1 - bv - 0.01 && tx >= 0 && ty >= 0) {
                        const ogpu_u8 *q = arena + SA + ((unsigned long)floor(ty) * 8 + (unsigned long)floor(tx)) * 4;
                        inner++;
                        if (p[1] == q[1] && p[2] == q[2] && p[3] == q[3]) exact++;
                    }
                }
            }
        sprintf(what, "COMPOSITE PIXELART %dx%d: SDL's GetPixelArtUV within 3 (worst %d, %d off)", dw[k], dh[k], worst, bad);
        check(!bad, what);
        if (k < 2) { sprintf(what, "COMPOSITE PIXELART x%d: inside each texel, exactly the texel (%d of %d)", dw[k] / 8, exact, inner); check(exact == inner && inner > 0, what); }
    }
    /* COMPOSITE_AFFINE: rotated and enlarged; the box from the inverse matrix */
    picture(SA, 8, 8, OGPU_FMT_ARGB32, 0);
    memset(arena + TA, 0, 64 * 64 * 4);
    m[0] = (long)floor(sc * cos(ang) * 65536); m[1] = (long)floor(-sc * sin(ang) * 65536); m[2] = 20 << 16;
    m[3] = (long)floor(sc * sin(ang) * 65536); m[4] = (long)floor(sc * cos(ang) * 65536); m[5] = 4 << 16;
    begin();
    ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 8, 8, m, 0xFFFFFFFFUL, OGPU_AFF_PIXELART);
    check(run() == OGPU_OK, "COMPOSITE_AFFINE with PIXELART runs");
    {
        double a = m[0] / 65536.0, b = m[1] / 65536.0, c = m[3] / 65536.0, d = m[4] / 65536.0, det = a * d - b * c;
        double i00 = d / det, i01 = -b / det, i10 = -c / det, i11 = a / det;
        double bu = fabs(i00) + fabs(i01), bv = fabs(i10) + fabs(i11);
        bad = worst = 0;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                double dx = x + 0.5 - m[2] / 65536.0, dy = y + 0.5 - m[5] / 65536.0, u = i00 * dx + i01 * dy, v = i10 * dx + i11 * dy, o[4];
                const ogpu_u8 *p = arena + TA + ((unsigned long)y * 64 + x) * 4;
                int cc, e = 0;
                if (u < 0.02 || v < 0.02 || u > 7.98 || v > 7.98) continue;     /* at the edge, inside or not is rounding's */
                ref_pixelart(SA, 8, 8, u, v, bu, bv, o);
                for (cc = 1; cc < 4; cc++) { int dd = abs((int)p[cc] - (int)floor(o[cc] + 0.5)); if (dd > e) e = dd; }   /* NONE: A is 255 */
                if (e > worst) worst = e;
                if (e > 3) bad++;
            }
        sprintf(what, "COMPOSITE_AFFINE PIXELART, rotated: within 3 (worst %d, %d off)", worst, bad);
        check(!bad, what);
    }
    /* TRIANGLES (SDL's form), three times larger, clamped */
    for (slow_spans = 0; slow_spans < 2; slow_spans++) {
        picture(SA, 8, 8, OGPU_FMT_ARGB32, 0);
        memset(arena + TA, 0, 64 * 64 * 4);
        quad(0, 0, 24, 24, 0, 0, 1, 1, 0xFFFFFFFFUL);
        begin();
        ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_PIXELART | OGPU_TRI_CLAMP);
        check(run() == OGPU_OK, "TRIANGLES with PIXELART runs");
        bad = worst = 0;
        for (y = 0; y < 24; y++)
            for (x = 0; x < 24; x++) {
                /* PIXELART's coordinates are exact (16.16, steps with remainders) */
                const ogpu_u8 *p = arena + TA + ((unsigned long)y * 64 + x) * 4;
                double o[4];
                int cc, e = 0;
                ref_pixelart(SA, 8, 8, (x + 0.5) / 3, (y + 0.5) / 3, 1.0 / 3, 1.0 / 3, o);
                for (cc = 0; cc < 4; cc++) { int dd = abs((int)p[cc] - (int)floor(o[cc] + 0.5)); if (dd > e) e = dd; }
                if (e > worst) worst = e;
                if (e > 2) bad++;
            }
        sprintf(what, "TRIANGLES PIXELART x3 (%s spans): within 2 (worst %d, %d off)", slow_spans ? "general" : "fast", worst, bad);
        check(!bad, what);
    }
    slow_spans = 0;
    /* not with BILINEAR */
    for (k = 0; k < 3; k++) {
        begin();
        ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        if (k == 0) ogpu_composite(&B, 1, 0, 0, 8, 8, 0, 0, 16, 16, 255, OGPU_COMP_PIXELART | OGPU_COMP_BILINEAR);
        else if (k == 1) ogpu_composite_affine(&B, 1, 0, 0, 8, 8, m, 0xFFFFFFFFUL, OGPU_AFF_PIXELART | OGPU_AFF_BILINEAR);
        else ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_PIXELART | OGPU_TRI_BILINEAR);
        sprintf(what, "PIXELART with BILINEAR (command %d): UNSUPPORTED", k);
        check(run() == OGPU_ERR_UNSUPPORTED, what);
    }
}

/* PIXELART at real sizes: a picture made 5 to 20 times wider across a
 * screen, with the left part clipped away; its steep edges show any drift
 * in the coordinates. COMPOSITE steps its centres exactly; TRIANGLES in
 * 16.16. Two rows at the same height: one way only. */
#define WT 0x100000UL         /* a 2000 x 2 target */
#define WS 0x110000UL         /* the source, up to 400 x 2 */
static int wide_worst(int tri, int sw, int dw, int clip, int tw) {
    int x, y, worst = 0, c;
    picture(WS, sw, 2, OGPU_FMT_ARGB32, 1);
    memset(arena + WT, 0, 2000 * 2 * 4);
    begin();
    ogpu_surface(&B, 0, WT, 8000, (unsigned)tw, 2, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, WS, (unsigned long)sw * 4, (unsigned)sw, 2, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    if (tri) {
        quad(-clip, 0, dw - clip, 2, 0, 0, 1, 1, 0xFFFFFFFFUL);
        ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_PIXELART | OGPU_TRI_CLAMP);
    } else ogpu_composite(&B, 1, 0, 0, (unsigned)sw, 2, -clip, 0, (unsigned)dw, 2, 255, OGPU_COMP_PIXELART);
    if (run() != OGPU_OK) return 999;
    for (y = 0; y < 2; y++)
        for (x = 0; x < tw && x < dw - clip; x++) {
            double o[4], bu = (double)sw / dw;
            const ogpu_u8 *p = arena + WT + ((unsigned long)y * 2000 + x) * 4;
            ref_pixelart(WS, sw, 2, (x + clip + 0.5) * bu, y + 0.5, bu, 1, o);
            for (c = 1; c < 4; c++) { int d = abs((int)p[c] - (int)floor(o[c] + 0.5)); if (d > worst) worst = d; }
        }
    return worst;
}

static void test_pixelart_wide(void) {
    static const int sw[5] = { 320, 100, 333, 80, 400 }, dw[5] = { 1920, 1900, 1777, 1600, 1999 }, clip[5] = { 0, 0, 0, 600, 1 };
    int k, tri, w;
    char what[120];
    for (tri = 0; tri < 2; tri++)
        for (k = 0; k < 5; k++) {
            w = wide_worst(tri, sw[k], dw[k], clip[k], 2000 - (clip[k] ? 700 : 0));
            sprintf(what, "%s PIXELART %d to %d texels wide (%d clipped): within 2 (worst %d)", tri ? "TRIANGLES" : "COMPOSITE", sw[k], dw[k], clip[k], w);
            printf("  %s\n", what);
            check(w <= 2, what);
        }
}

/* ---- TRIANGLES: clamping one way ----------------------------------------------------- */

static void test_clamp(void) {
    static const unsigned long fl[5] = { 0, OGPU_TRI_CLAMP, OGPU_TRI_CLAMP_U, OGPU_TRI_CLAMP_V, OGPU_TRI_CLAMP_U | OGPU_TRI_CLAMP_V };
    int k, x, y, bad, seen;
    char what[100];
    picture(SA, 4, 4, OGPU_FMT_ARGB32, 1);
    for (k = 0; k < 5; k++) {
        int cu = (fl[k] & (OGPU_TRI_CLAMP | OGPU_TRI_CLAMP_U)) != 0, cv = (fl[k] & (OGPU_TRI_CLAMP | OGPU_TRI_CLAMP_V)) != 0;
        memset(arena + TA, 0, 64 * 64 * 4);
        quad(0, 0, 32, 32, -1, -1, 2, 2, 0xFFFFFFFFUL);
        begin();
        ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, SA, 16, 4, 4, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, fl[k]);
        check(run() == OGPU_OK, "TRIANGLES clamped one way runs");
        bad = seen = 0;
        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++) {
                double u = (-1 + 3 * (x + 0.5) / 32) * 4, v = (-1 + 3 * (y + 0.5) / 32) * 4;
                int tu, tv;
                if (fabs(u - floor(u + 0.5)) < 0.05 || fabs(v - floor(v + 0.5)) < 0.05) continue;   /* on a texel's border */
                tu = (int)floor(u); tv = (int)floor(v);
                tu = cu ? (tu < 0 ? 0 : tu > 3 ? 3 : tu) : ((tu % 4) + 4) % 4;
                tv = cv ? (tv < 0 ? 0 : tv > 3 ? 3 : tv) : ((tv % 4) + 4) % 4;
                seen++;
                if (memcmp(arena + TA + ((unsigned long)y * 64 + x) * 4 + 1, arena + SA + ((unsigned long)tv * 4 + tu) * 4 + 1, 3)) bad++;
            }
        sprintf(what, "TRIANGLES flags %lu: each axis clamped or repeated (%d of %d off)", fl[k], bad, seen);
        check(!bad && seen > 500, what);
    }
}

/* ---- PALETTE ------------------------------------------------------------------------- */

static void test_palette(void) {
    static const long rot[6] = { 50000, -30000, 20 << 16, 30000, 50000, 3 << 16 };
    int op, i, r1 = -99, r2 = -99, bad;
    char what[100];
    for (op = 0; op < 9; op++) {
        /* an INDEX8 picture, its palette, and the ARGB32 picture they make */
        for (i = 0; i < 256; i++) { arena[SB + i] = (ogpu_u8)rnd8(); }
        for (i = 0; i < 256; i++) { struct px c = rnd_px(); to_fmt(OGPU_FMT_ARGB32, c, arena + PAL + (unsigned long)i * 4); }
        for (i = 0; i < 256; i++) memcpy(arena + SA + (unsigned long)i * 4, arena + PAL + (unsigned long)arena[SB + i] * 4, 4);
        picture(TA, 32, 32, OGPU_FMT_ARGB32, 0);
        memcpy(arena + TB, arena + TA, 4096);
        for (i = 0; i < 2; i++) {
            begin();
            ogpu_surface(&B, 0, i ? TB : TA, 128, 32, 32, OGPU_FMT_ARGB32);
            if (i) { ogpu_surface(&B, 1, SB, 16, 16, 16, OGPU_FMT_INDEX8); ogpu_palette(&B, 1, PAL); }
            else ogpu_surface(&B, 1, SA, 64, 16, 16, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0);
            switch (op) {
            case 0: ogpu_composite(&B, 1, 0, 0, 16, 16, 2, 3, 23, 19, 200, OGPU_COMP_SRCALPHA); break;
            case 1: ogpu_composite(&B, 1, 1, 2, 13, 12, 0, 0, 30, 29, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR); break;
            case 2: ogpu_composite(&B, 1, 0, 0, 16, 16, 0, 0, 32, 32, 150, OGPU_COMP_SRCALPHA | OGPU_COMP_PIXELART
                                   | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(OGPU_BLEND_BLEND_PREMUL)); break;
            case 3: ogpu_composite(&B, 1, 0, 0, 16, 16, 4, 4, 16, 16, 255, OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(OGPU_BLEND_ADD)); break;
            case 4: ogpu_composite_affine(&B, 1, 0, 0, 16, 16, rot, 0xC0FF8040UL, OGPU_AFF_SRCALPHA | OGPU_AFF_BILINEAR | OGPU_BLEND_BLEND); break;
            case 5: ogpu_composite_affine(&B, 1, 0, 0, 16, 16, rot, 0xFFFFFFFFUL, OGPU_AFF_SRCALPHA | OGPU_AFF_PIXELART | OGPU_BLEND_MUL); break;
            case 6: quad(1, 2, 31, 30, 0, 0, 1, 1, 0xFFC0A080UL);
                    ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_BLEND_BLEND | OGPU_TRI_BILINEAR); break;
            case 7: quad(0, 0, 32, 32, -0.5, 0, 1.5, 2, 0xFFFFFFFFUL);
                    ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_BLEND_PREMUL | OGPU_TRI_CLAMP_U); break;
            case 8: quad(0, 0, 32, 32, 0, 0, 1, 1, 0xFFFFFFFFUL);
                    ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, OGPU_TRI_PIXELART | OGPU_TRI_CLAMP); break;
            }
            if (i) r2 = run(); else r1 = run();
        }
        bad = memcmp(arena + TA, arena + TB, 4096);
        sprintf(what, "PALETTE, case %d: an INDEX8 slot draws as its ARGB32 picture (%d %d)", op, r1, r2);
        check(r1 == OGPU_OK && r2 == OGPU_OK && !bad, what);
    }
    /* cycling: the table is read whenever the slot is drawn from, not copied
     * by PALETTE. One batch draws the top half, changes entry 2 by FILLing
     * the table's own memory (a 256 x 1 ARGB32 slot), then draws the bottom
     * half: only the bottom has the new colour. */
    for (i = 0; i < 256; i++) arena[SB + i] = (ogpu_u8)(i & 3);
    for (i = 0; i < 4; i++) put32(PAL + (unsigned long)i * 4, 0xFF000000UL | (unsigned long)(0x10 * (i + 1)) << 16);
    memset(arena + TA, 0, 16 * 16 * 4);
    begin();
    ogpu_surface(&B, 0, TA, 64, 16, 16, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, SB, 16, 16, 16, OGPU_FMT_INDEX8);
    ogpu_palette(&B, 1, PAL);
    ogpu_surface(&B, 2, PAL, 1024, 256, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 16, 8, 0, 0, 16, 8, 255, 0);
    ogpu_target(&B, 2);
    ogpu_fill(&B, 2, 0, 1, 1, 0xFF00FF00UL);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 8, 16, 8, 0, 8, 16, 8, 255, 0);
    r1 = run();
    check(r1 == OGPU_OK && arena[TA + 2 * 4 + 1] == 0x30 && arena[TA + 2 * 4 + 2] == 0
          && arena[TA + 8 * 64 + 2 * 4 + 1] == 0 && arena[TA + 8 * 64 + 2 * 4 + 2] == 0xFF
          && arena[TA + 1 * 4 + 1] == 0x20 && arena[TA + 8 * 64 + 1 * 4 + 1] == 0x20,
          "PALETTE: the table is read when the slot is drawn from (changed mid-batch, the next draw has it)");
    /* the errors */
    {
        static const char *what_err[] = {
            "INDEX8 with no PALETTE: UNSUPPORTED (COMPOSITE)", "INDEX8 with no PALETTE: UNSUPPORTED (COMPOSITE_AFFINE)",
            "INDEX8 with no PALETTE: UNSUPPORTED (TRIANGLES)", "a SURFACE forgets the PALETTE",
            "PALETTE on an ARGB32 slot: UNSUPPORTED", "PALETTE on an empty slot: NOSURFACE", "PALETTE at memory nobody maps: NOMAP",
            "COMPOSITE into an INDEX8 slot: UNSUPPORTED", "MASK into an INDEX8 slot: UNSUPPORTED",
            "COMPOSITE_PD from an INDEX8 slot: UNSUPPORTED", "PIXELS of ARGB32 into an INDEX8 slot: UNSUPPORTED",
        };
        static const int want[] = { OGPU_ERR_UNSUPPORTED, OGPU_ERR_UNSUPPORTED, OGPU_ERR_UNSUPPORTED, OGPU_ERR_UNSUPPORTED,
                                    OGPU_ERR_UNSUPPORTED, OGPU_ERR_NOSURFACE, OGPU_ERR_NOMAP, OGPU_ERR_UNSUPPORTED,
                                    OGPU_ERR_UNSUPPORTED, OGPU_ERR_UNSUPPORTED, OGPU_ERR_UNSUPPORTED };
        int e;
        for (e = 0; e < 11; e++) {
            begin();
            ogpu_surface(&B, 0, TA, 64, 16, 16, OGPU_FMT_ARGB32);
            ogpu_surface(&B, 1, SB, 16, 16, 16, OGPU_FMT_INDEX8);
            ogpu_target(&B, 0);
            switch (e) {
            case 0: ogpu_composite(&B, 1, 0, 0, 4, 4, 0, 0, 4, 4, 255, 0); break;
            case 1: ogpu_composite_affine(&B, 1, 0, 0, 4, 4, rot, 0xFFFFFFFFUL, 0); break;
            case 2: quad(0, 0, 4, 4, 0, 0, 1, 1, 0xFFFFFFFFUL); ogpu_triangles(&B, VTX, 6, 20, 0, 0, 1, 1, 0); break;
            case 3: ogpu_palette(&B, 1, PAL); ogpu_surface(&B, 1, SB, 16, 16, 16, OGPU_FMT_INDEX8);
                    ogpu_composite(&B, 1, 0, 0, 4, 4, 0, 0, 4, 4, 255, 0); break;
            case 4: ogpu_palette(&B, 0, PAL); break;
            case 5: ogpu_palette(&B, 5, PAL); break;
            case 6: ogpu_palette(&B, 1, 0x7FFFFF00UL); break;
            case 7: ogpu_palette(&B, 1, PAL); ogpu_surface(&B, 2, SA, 64, 16, 16, OGPU_FMT_ARGB32); ogpu_target(&B, 1);
                    ogpu_composite(&B, 2, 0, 0, 4, 4, 0, 0, 4, 4, 255, 0); break;
            case 8: ogpu_target(&B, 1); ogpu_mask(&B, MK, 16, 0, 0, 4, 4, 0xFFFFFFFFUL); break;
            case 9: ogpu_palette(&B, 1, PAL); ogpu_composite_pd(&B, OGPU_PD_OVER, 1, 0, 0, 0, 0, 0, 0, 0, 4, 4); break;
            case 10: ogpu_target(&B, 1); ogpu_pixels(&B, SA, 64, OGPU_FMT_ARGB32, 0, 0, 0, 4, 4); break;
            }
            check(run() == want[e], what_err[e]);
        }
    }
    /* indexes move and fill as pens do; a PALETTE needs no target */
    for (i = 0; i < 256; i++) arena[SB + i] = (ogpu_u8)i;
    memset(arena + SB + 256, 0, 256);
    begin();
    ogpu_surface(&B, 1, SB, 16, 16, 16, OGPU_FMT_INDEX8);
    ogpu_surface(&B, 2, SB + 256, 16, 16, 16, OGPU_FMT_INDEX8);
    ogpu_palette(&B, 1, PAL);
    ogpu_target(&B, 2);
    ogpu_copy(&B, 1, 0, 0, 0, 0, 16, 8);
    ogpu_fill(&B, 0, 8, 16, 8, 0x77);
    check(run() == OGPU_OK && !memcmp(arena + SB, arena + SB + 256, 128) && arena[SB + 256 + 200] == 0x77,
          "INDEX8: COPY moves indexes, FILL writes one; PALETTE runs with no target");
}

/* ---- YUV ----------------------------------------------------------------------------- */

static void ref_yuv(int space, int Y, int U, int V, double *o) {
    double kr, kb, y, pb, pr, kg;
    int video = space == OGPU_YUV_BT709 || space == OGPU_YUV_BT2020 || space == OGPU_YUV_BT601;
    switch (space) {
    case OGPU_YUV_BT709: case OGPU_YUV_BT709_FULL: kr = 0.2126; kb = 0.0722; break;
    case OGPU_YUV_BT2020: case OGPU_YUV_BT2020_FULL: kr = 0.2627; kb = 0.0593; break;
    default: kr = 0.299; kb = 0.114;
    }
    kg = 1 - kr - kb;
    y = video ? (Y - 16) * 255.0 / 219 : Y;
    pb = video ? (U - 128) * 255.0 / 224 : U - 128;
    pr = video ? (V - 128) * 255.0 / 224 : V - 128;
    o[0] = y + 2 * (1 - kr) * pr;
    o[2] = y + 2 * (1 - kb) * pb;
    o[1] = (y - kr * o[0] - kb * o[2]) / kg;
}

static void test_yuv(void) {
    static const int spaces[3] = { OGPU_YUV_BT709_FULL, OGPU_YUV_BT2020, OGPU_YUV_BT2020_FULL };
    int k, x, y, bad, worst, r1, r2;
    char what[100];
    /* an NV12 picture, 16 x 8 */
    for (k = 0; k < 16 * 8; k++) arena[YP + k] = (ogpu_u8)rnd8();
    for (k = 0; k < 16 * 4; k++) arena[UVP + k] = (ogpu_u8)rnd8();
    for (k = 0; k < 3; k++) {
        begin();
        ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_yuv(&B, YP, 16, UVP, 16, 0, 0, 16, 8, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_NV12, spaces[k]));
        r1 = run();
        bad = worst = 0;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 16; x++) {
                double o[3];
                int c, e = 0;
                ref_yuv(spaces[k], arena[YP + y * 16 + x], arena[UVP + (y >> 1) * 16 + (x & ~1)], arena[UVP + (y >> 1) * 16 + (x | 1)], o);
                for (c = 0; c < 3; c++) {
                    double v = o[c] < 0 ? 0 : o[c] > 255 ? 255 : o[c];
                    int d = abs((int)arena[TA + ((unsigned long)y * 16 + x) * 4 + 1 + c] - (int)floor(v + 0.5));
                    if (d > e) e = d;
                }
                if (e > worst) worst = e;
                if (e > 2) bad++;
            }
        sprintf(what, "YUV colour space %d: its matrix within 2 (worst %d)", spaces[k], worst);
        check(r1 == OGPU_OK && !bad, what);
    }
    /* P010: the same picture in 16-bit samples, low bits below a half, then above */
    for (k = 0; k < 2; k++) {
        int i;
        for (i = 0; i < 16 * 8; i++) {
            int v = arena[YP + i];
            unsigned w16 = (unsigned)v << 8 | 0x40;                 /* rounds down to v */
            if (k) w16 = v ? (unsigned)(v - 1) << 8 | 0xA0 : 0x10;   /* rounds up to v */
            arena[YP2 + i * 2] = (ogpu_u8)(w16 >> 8); arena[YP2 + i * 2 + 1] = (ogpu_u8)w16;
        }
        for (i = 0; i < 16 * 4; i++) {
            int v = arena[UVP + i];
            unsigned w16 = (unsigned)v << 8 | 0x3F;
            if (k && v > 0) w16 = (unsigned)(v - 1) << 8 | 0x80;
            arena[UVP2 + i * 2] = (ogpu_u8)(w16 >> 8); arena[UVP2 + i * 2 + 1] = (ogpu_u8)w16;
        }
        begin();
        ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, TB, 64, 16, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_yuv(&B, YP, 16, UVP, 16, 0, 0, 16, 8, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_NV12, OGPU_YUV_BT709));
        ogpu_target(&B, 1);
        ogpu_yuv(&B, YP2, 32, UVP2, 32, 0, 0, 16, 8, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_P010, OGPU_YUV_BT709));
        r1 = run();
        sprintf(what, "P010 (%s): NV12's pixels from its samples' top bytes, rounded", k ? "rounding up" : "rounding down");
        check(r1 == OGPU_OK && !memcmp(arena + TA, arena + TB, 16 * 8 * 4), what);
        begin();
        ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, TB, 64, 16, 8, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_yuv_scaled(&B, YP, 16, UVP, 16, 0, 0, 16, 8, OGPU_YUV_FORMAT(OGPU_YUV_NV12, OGPU_YUV_BT2020), 2, 2, 12, 6, 0, 0, 16, 8);
        ogpu_target(&B, 1);
        ogpu_yuv_scaled(&B, YP2, 32, UVP2, 32, 0, 0, 16, 8, OGPU_YUV_FORMAT(OGPU_YUV_P010, OGPU_YUV_BT2020), 2, 2, 12, 6, 0, 0, 16, 8);
        r2 = run();
        check(r2 == OGPU_OK && !memcmp(arena + TA, arena + TB, 16 * 8 * 4), "YUV_SCALED of P010: as of NV12");
    }
    begin();
    ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_yuv(&B, YP, 16, UVP, 16, 0, 0, 16, 8, 0, 0, OGPU_YUV_FORMAT(9, OGPU_YUV_BT601));
    check(run() == OGPU_ERR_UNSUPPORTED, "YUV layout 9: UNSUPPORTED");
    begin();
    ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_yuv_scaled(&B, YP, 16, UVP, 16, 0, 0, 16, 8, OGPU_YUV_FORMAT(OGPU_YUV_NV12, 6), 0, 0, 16, 8, 0, 0, 8, 4);
    check(run() == OGPU_ERR_UNSUPPORTED, "YUV colour space 6: UNSUPPORTED");
    begin();
    ogpu_surface(&B, 0, TA, 64, 16, 8, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_yuv(&B, YP2, 16, UVP2, 32, 0, 0, 16, 8, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_P010, OGPU_YUV_BT601));
    check(run() == OGPU_ERR_UNSUPPORTED, "P010 with NV12's bytes per row: UNSUPPORTED");
}

/* ---- OGPU_Query and the library's variant check -------------------------------------- */

static unsigned needs_of(void) {
    unsigned long at = 0, n = 0;
    while (at < (unsigned long)B.words) {
        const ogpu_u8 *p = sbuf + at * 4;
        unsigned op = (unsigned)(p[0] << 8 | p[1]), len = (unsigned)(p[2] << 8 | p[3]), need = ogpu_variant_needs(sbuf, at, op, len);
        if (need) n = need;
        at += len;
    }
    return (unsigned)n;
}

static void test_query(void) {
    static const long id[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
    int k;
    char what[80];
    check(ogpu_core_supports(OGPU_OP_PALETTE, OGPU_FMT_ARGB32) == OGPU_FULL && ogpu_core_supports(OGPU_OP_PALETTE, OGPU_FMT_INDEX8) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_COMPOSITE, OGPU_FMT_RGBA32) == OGPU_FULL && ogpu_core_supports(OGPU_OP_COMPOSITE_AFFINE, OGPU_FMT_ABGR32) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_YUV, OGPU_FMT_RGBA32) == OGPU_FULL && ogpu_core_supports(OGPU_OP_BLUR, OGPU_FMT_ABGR32) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_FILL, OGPU_FMT_INDEX8) == OGPU_FULL && ogpu_core_supports(OGPU_OP_COMPOSITE, OGPU_FMT_INDEX8) == OGPU_NONE
          && ogpu_core_supports(OGPU_OP_MASK, OGPU_FMT_INDEX8) == OGPU_NONE && ogpu_core_supports(OGPU_OP_PIXELS, OGPU_FMT_INDEX8) == OGPU_PARTIAL
          && ogpu_core_supports(OGPU_OP_TRIANGLES, OGPU_FMT_XRGB32) == OGPU_PARTIAL
          && ogpu_core_supports(OGPU_OP_TRIANGLES, OGPU_FMT_RGB565PC) == OGPU_NONE, "OGPU_Query's answers for v1.7");
    for (k = 0; k < 14; k++) {
        unsigned want = OGPU_OP_PALETTE;
        begin();
        switch (k) {
        case 0: ogpu_fill_blend(&B, 0, 0, 1, 1, 0, OGPU_BLEND_BLEND); want = 0; break;
        case 1: ogpu_fill_blend(&B, 0, 0, 1, 1, 0, OGPU_BLEND_BLEND_PREMUL); break;
        case 2: ogpu_lines_blend(&B, PTS, 2, 0, OGPU_LINES_STRIP | OGPU_BLEND_ADD_PREMUL); break;
        case 3: ogpu_points_blend(&B, PTS, 2, 0, OGPU_BLEND_MUL); want = 0; break;
        case 4: ogpu_composite(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, OGPU_COMP_BILINEAR); want = 0; break;
        case 5: ogpu_composite(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, OGPU_COMP_PIXELART); break;
        case 6: ogpu_composite_masked(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, OGPU_COMP_MASK | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(OGPU_BLEND_BLEND_PREMUL), 2, 0, 0); break;
        case 7: ogpu_composite_affine(&B, 1, 0, 0, 1, 1, id, 0, OGPU_AFF_PIXELART); break;
        case 8: ogpu_yuv(&B, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_NV12, OGPU_YUV_JPEG)); want = 0; break;
        case 9: ogpu_yuv_scaled(&B, 0, 0, 0, 0, 0, 0, 2, 2, OGPU_YUV_FORMAT(OGPU_YUV_NV12, OGPU_YUV_BT2020), 0, 0, 2, 2, 0, 0, 2, 2); break;
        case 10: ogpu_yuv(&B, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_P010, OGPU_YUV_BT601)); break;
        case 11: ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, OGPU_TRI_CLAMP_V); break;
        case 12: ogpu_triangles3d(&B, VTX, 3, 20, 0, 0, 1, 1, OGPU_TRI_CLAMP_V, OGPU_LAY_LIST); want = 0; break;
        case 13: ogpu_composite_pd(&B, OGPU_BLEND_SCREEN, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1); want = OGPU_OP_PATH_FILL; break;
        }
        sprintf(what, "the variant check, case %d: a driver must also take %u", k, want);
        check(needs_of() == want, what);
    }
}

/* opengpu.library 0.19's whole-batch check (ogpu_batch_takes), for a v1.6
 * driver and a v1.7 one: the tables the library keeps (opcodes it carries
 * out; formats it takes SURFACE in, as each core answers). */
static void test_routing(void) {
    static unsigned char ok16[256], ok17[256], fmt16[OGPU_FMT_COUNT], fmt17[OGPU_FMT_COUNT];
    int k, f, op;
    char what[120];
    for (op = 0; op < 256; op++) {
        int real = op <= OGPU_OP_PALETTE || (op >= OGPU_OP_3D_FIRST && op <= OGPU_OP_3D_LAST);
        ok17[op] = (unsigned char)real;
        ok16[op] = (unsigned char)(real && op != OGPU_OP_PALETTE);
    }
    for (f = 0; f < OGPU_FMT_COUNT; f++) {
        fmt17[f] = (unsigned char)(f && ogpu_core_supports(OGPU_OP_SURFACE, f) != OGPU_NONE);
        fmt16[f] = (unsigned char)(fmt17[f] && f != OGPU_FMT_INDEX8 && f != OGPU_FMT_RGBA32 && f != OGPU_FMT_ABGR32);
    }
    check(fmt17[OGPU_FMT_INDEX8] && fmt17[OGPU_FMT_RGBA32] && fmt17[OGPU_FMT_ABGR32] && fmt17[OGPU_FMT_XRGB32],
          "the core takes SURFACE in INDEX8, RGBA32 and ABGR32");
    for (k = 0; k < 11; k++) {
        int want16 = 0;
        const char *name = "";
        begin();
        switch (k) {
        case 0: name = "COMPOSITE from an RGBA32 slot";
            ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_RGBA32); ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0); ogpu_composite(&B, 1, 0, 0, 8, 8, 0, 0, 8, 8, 255, 0); break;
        case 1: name = "FILL into an INDEX8 slot, no PALETTE";
            ogpu_surface(&B, 0, TA, 64, 64, 64, OGPU_FMT_INDEX8); ogpu_target(&B, 0); ogpu_fill(&B, 0, 0, 4, 4, 1); break;
        case 2: name = "TRIANGLES into XRGB32";
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_XRGB32); ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0); ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, 0); break;
        case 3: name = "TRIANGLES from a BGRA32 slot";
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32); ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_BGRA32);
            ogpu_target(&B, 0); ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, 0); break;
        case 4: name = "the 3D form from an RGB565PC slot";
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32); ogpu_surface(&B, 1, SA, 16, 8, 8, OGPU_FMT_RGB565PC);
            ogpu_target(&B, 0); ogpu_triangles3d(&B, VTX, 3, 20, 0, 0, 1, 1, 0, OGPU_LAY_LIST); break;
        case 5: name = "COMPOSITE with PIXELART";
            ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32); ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0); ogpu_composite(&B, 1, 0, 0, 8, 8, 0, 0, 16, 16, 255, OGPU_COMP_PIXELART); break;
        case 6: name = "TRIANGLES, ARGB32 from ARGB32"; want16 = 1;
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32); ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0); ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, OGPU_TRI_BLEND_BLEND); break;
        case 7: name = "TRIANGLES, RGB565 from CLUT8"; want16 = 1;
            ogpu_surface(&B, 0, TA, 128, 64, 64, OGPU_FMT_RGB565); ogpu_surface(&B, 1, SA, 8, 8, 8, OGPU_FMT_CLUT8);
            ogpu_target(&B, 0); ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, 0); break;
        case 8: name = "FILL into XRGB32"; want16 = 1;
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_XRGB32); ogpu_target(&B, 0); ogpu_fill(&B, 0, 0, 4, 4, 1); break;
        case 9: name = "a SURFACE in RGBA32 alone";
            ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_RGBA32); break;
        case 10: name = "a slot set twice: XRGB32, then ARGB32 before TRIANGLES"; want16 = 1;
            ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_XRGB32); ogpu_surface(&B, 0, TA, 256, 64, 64, OGPU_FMT_ARGB32);
            ogpu_surface(&B, 1, SA, 32, 8, 8, OGPU_FMT_ARGB32); ogpu_target(&B, 0); ogpu_triangles(&B, VTX, 3, 20, 0, 0, 1, 1, 0); break;
        }
        sprintf(what, "whole batch, %s: a v1.6 driver %s it", name, want16 ? "takes" : "doesn't take");
        check(ogpu_batch_takes(sbuf, (unsigned long)B.words, ok16, fmt16) == want16, what);
        sprintf(what, "whole batch, %s: a v1.7 driver takes it", name);
        check(ogpu_batch_takes(sbuf, (unsigned long)B.words, ok17, fmt17) == 1, what);
    }
}

int main(void) {
    test_formats();
    test_premul();
    test_pixelart();
    test_pixelart_wide();
    test_clamp();
    test_palette();
    test_yuv();
    test_query();
    test_routing();
    printf("sdl3 (stream v1.7): %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
