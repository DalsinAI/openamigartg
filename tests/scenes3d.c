/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * 3D scenes for comparing back ends: see scenes3d.h. Host only (it places
 * vertices with the C library's floating point). */
#include <math.h>
#include <string.h>
#include "scenes3d.h"
#include "../include/opengpu/build3d.h"

static unsigned char sbuf[1 << 16];
static struct OGPUBatch B;
static struct ogpu_scene3d_env *E;

static void wr8(int r, long a, unsigned long v) { E->mem[r][a] = (ogpu_u8)v; }
static void wr32(int r, long a, unsigned long v) {
    ogpu_u8 *p = E->mem[r] + a;
    p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
}
static unsigned long AD(int r, long a) { return E->addr[r] + (unsigned long)a; }
static unsigned long fbits(float f) { unsigned int u; memcpy(&u, &f, 4); return (unsigned long)u; }
static unsigned long fx16(double v) { return (unsigned long)(long)floor(v * 65536.0 + 0.5) & 0xFFFFFFFFUL; }

/* ---- random scenes ------------------------------------------------------------------- */

static unsigned long long rs;
static unsigned long rn(void) {
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (unsigned long)(rs >> 16) & 0xFFFFFFFFUL;
}
static int ri(int lo, int hi) { return lo + (int)(rn() % (unsigned long)(hi - lo + 1)); }   /* lo..hi */
static int pc(int percent) { return (int)(rn() % 100) < percent; }
static double rd(double lo, double hi) { return lo + (hi - lo) * (double)(rn() & 0xFFFFFF) / 16777216.0; }

/* the random scene's places (offsets in each region) */
#define RT_TGT  0x000000L
#define RT_Z    0x040000L
#define RT_S    0x070000L
#define RT_TEX  0x080000L           /* 4 textures, 0x8000 each */
#define RT_TAB  0x0C0000L           /* INDEX8 tables, 4 x 1 KB */
#define RT_PAL  0x0C1000L
#define RT_STIP 0x0C1400L
#define RT_PEN  0x0C2000L
#define RT_VB   0x0D0000L           /* each draw's vertices, 0x1000 apart */
#define RT_IB   0x0E0000L           /* each draw's indices, 0x400 apart */
#define RT_SLOT 0x0F0000L           /* a surface the SDL form samples */

static void fill_random(int r, long at, long n) { long i; for (i = 0; i < n; i++) wr8(r, at + i, rn() & 255); }

struct rtex { int r, fmt, base, w, h, bpp, levels, min, mag, ws, wt; long bpr; unsigned long table, border; unsigned long lad[OGPU_TEX_LEVELS]; };

static void random_texture(struct rtex *t, int i) {
    static const int fmts[] = { OGPU_FMT_ARGB32, OGPU_FMT_ARGB32, OGPU_FMT_RGB565, OGPU_FMT_A8, OGPU_FMT_INDEX8, OGPU_FMT_CLUT8 };
    long at = RT_TEX + i * 0x8000L, a;
    int lv, maxlv;
    t->r = pc(50);
    t->fmt = fmts[ri(0, 5)];
    t->bpp = t->fmt == OGPU_FMT_ARGB32 ? 4 : t->fmt == OGPU_FMT_RGB565 ? 2 : 1;
    t->base = ri(0, 5);
    if (pc(70)) { t->w = 1 << ri(0, 6); t->h = 1 << ri(0, 6); }
    else { t->w = ri(3, 50); t->h = ri(3, 50); }
    for (maxlv = 1; (t->w >> maxlv) || (t->h >> maxlv); maxlv++) {}
    if (maxlv > OGPU_TEX_LEVELS) maxlv = OGPU_TEX_LEVELS;
    t->levels = pc(50) ? ri(1, maxlv) : 1;
    t->bpr = (long)t->w * t->bpp + (pc(50) ? ri(0, 5) : 0);
    t->min = ri(1, 6); t->mag = ri(1, 2);
    t->ws = ri(1, 3); t->wt = ri(1, 3);
    t->border = rn();
    t->table = 0;
    if ((t->fmt == OGPU_FMT_INDEX8 || t->fmt == OGPU_FMT_CLUT8) && pc(70)) {
        t->table = AD(t->r, RT_TAB + i * 1024L);
        fill_random(t->r, RT_TAB + i * 1024L, 1024);
    }
    /* level 0 with a pattern and noise, then the smaller levels */
    a = at;
    for (lv = 0; lv < t->levels; lv++) {
        int w = t->w >> lv, h = t->h >> lv, x, y;
        long bpr = lv ? (long)(w < 1 ? 1 : w) * t->bpp : t->bpr;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (lv) t->lad[lv - 1] = AD(t->r, a);
        for (y = 0; y < h; y++)
            for (x = 0; x < (int)bpr; x++)
                wr8(t->r, a + (long)y * bpr + x, pc(70) ? (unsigned long)(((x / t->bpp + y) * 37 + lv * 50) & 255) : rn() & 255);
        if (lv == 0) t->lad[OGPU_TEX_LEVELS - 1] = AD(t->r, a);   /* level 0's address, kept here */
        a += bpr * h;
        a = (a + 3) & ~3L;
    }
}

static void emit_texture(const struct rtex *t) {
    ogpu_texture(&B, 0, t->lad[OGPU_TEX_LEVELS - 1], (unsigned long)t->bpr, t->w, t->h, OGPU_TEX_FORMAT(t->fmt, t->base),
                 t->table, rn() & 255, t->min, t->mag, t->ws, t->wt, t->border, t->levels, t->levels > 1 ? t->lad : 0);
}

static void random_render3d(int clut, int rtab) {
    unsigned long R[OGPU_R3D_ARGS], en = 0;
    memset(R, 0, sizeof R);
    if (pc(70)) en |= OGPU_R3D_GOURAUD;
    if (pc(50)) en |= OGPU_R3D_ZTEST;
    if (pc(50)) en |= OGPU_R3D_ZWRITE;
    if (pc(35)) en |= OGPU_R3D_BLEND;
    if (pc(30)) en |= OGPU_R3D_FOG;
    if (pc(20)) en |= OGPU_R3D_ALPHATEST;
    if (pc(20)) en |= OGPU_R3D_STENCIL;
    if (pc(12)) en |= OGPU_R3D_LOGICOP;
    if (pc(15)) en |= OGPU_R3D_CHROMA;
    if (pc(30)) en |= OGPU_R3D_SPECULAR;
    if (pc(10)) en |= OGPU_R3D_DITHER;
    if (pc(10)) en |= OGPU_R3D_STIPPLE;
    if (pc(70)) en |= OGPU_R3D_TEXTURE;
    if (pc(50)) en |= OGPU_R3D_PERSPECTIVE;
    R[0] = en;
    R[1] = (unsigned long)ri(1, 8);
    R[2] = (unsigned long)ri(1, 8) | ((unsigned long)ri(0, 255) << 16);
    if (pc(40)) R[3] = 7 | (8UL << 16);
    else if (pc(20)) R[3] = 2 | (2UL << 16);
    else R[3] = (unsigned long)ri(1, 15) | ((unsigned long)ri(1, 15) << 16);
    R[4] = rn();
    R[5] = pc(70) ? 0xFF0F : (rn() & 0xFF0F);
    R[6] = pc(50) ? 4 : (unsigned long)ri(1, 16);
    R[7] = (unsigned long)ri(0, 4);
    R[8] = rn();
    R[9] = fbits((float)rd(-0.2, 1.2)); R[10] = fbits((float)rd(-0.2, 1.2)); R[11] = fbits((float)rd(0, 5));
    R[12] = (unsigned long)ri(1, 8) | ((unsigned long)ri(0, 255) << 8) | ((unsigned long)ri(0, 255) << 16);
    R[13] = (unsigned long)ri(1, 8) | ((unsigned long)ri(1, 8) << 8) | ((unsigned long)ri(1, 8) << 16) | ((unsigned long)ri(0, 255) << 24);
    R[14] = (unsigned long)ri(1, 3);
    R[15] = rn() & 0xFF7F7F7FUL; R[16] = R[15] | (rn() & 0x00808080UL);
    R[17] = rn();
    R[18] = clut && pc(70) ? AD(rtab, RT_PEN) : 0;
    R[19] = pc(80) ? AD(rtab, RT_STIP) : 0;
    R[20] = clut && pc(70) ? AD(rtab, RT_PAL) : 0;
    ogpu_render3d(&B, R);
}

/* A draw's vertices (and indices) at draw k: the 3D form or SDL's. */
static void random_triangles(int k, int tw, int th, int sdl) {
    int rv = pc(50), n = ri(3, 12), i, prim = ri(0, 2);
    unsigned long layout = 0;
    int vsize = 20, stride;
    long vb = RT_VB + k * 0x1000L, ib = RT_IB + k * 0x400L;
    unsigned long iaddr = 0, icount = 0;
    int isize = 0, tex;
    if (!sdl) {
        if (pc(60)) { layout |= OGPU_LAY_Z; vsize += 4; }
        if (pc(60)) { layout |= OGPU_LAY_W; vsize += 4; }
        if (pc(40)) { layout |= OGPU_LAY_SPEC; vsize += 4; }
        if (pc(5)) { layout |= OGPU_LAY_UV1; vsize += 8; }
        layout |= (unsigned long)prim;
    }
    stride = vsize + (pc(30) ? ri(1, 8) : 0);
    for (i = 0; i < n; i++) {
        long at = vb + (long)i * stride, f = 20;
        wr32(rv, at, fx16(rd(-0.3 * tw, 1.3 * tw)));
        wr32(rv, at + 4, fx16(rd(-0.3 * th, 1.3 * th)));
        wr32(rv, at + 8, pc(30) ? 0xFF000000UL | rn() : rn());
        wr32(rv, at + 12, fx16(rd(-1.5, 2.5)));
        wr32(rv, at + 16, fx16(rd(-1.5, 2.5)));
        if (layout & OGPU_LAY_Z) { wr32(rv, at + f, rn() ^ (rn() << 8)); f += 4; }
        if (layout & OGPU_LAY_W) { wr32(rv, at + f, fbits(pc(5) ? (float)rd(-1, 0.01) : (float)rd(0.05, 3))); f += 4; }
        if (layout & OGPU_LAY_SPEC) { wr32(rv, at + f, rn()); f += 4; }
        if (layout & OGPU_LAY_UV1) { wr32(rv, at + f, rn()); wr32(rv, at + f + 4, rn()); }
    }
    if (pc(30)) {
        int ri_ = pc(50);
        isize = pc(40) ? 2 : pc(50) ? 1 : 4;
        icount = (unsigned long)ri(3, 15);
        for (i = 0; i < (int)icount; i++) {
            unsigned long v = (unsigned long)(pc(97) ? ri(0, n - 1) : ri(0, n + 3));
            if (isize == 1) wr8(ri_, ib + i, v);
            else if (isize == 2) { wr8(ri_, ib + i * 2, v >> 8); wr8(ri_, ib + i * 2 + 1, v); }
            else wr32(ri_, ib + i * 4, v);
        }
        iaddr = AD(ri_, ib);
    }
    tex = pc(70) ? (sdl ? 1 : OGPU_TRI_UNIT0) : pc(50) ? OGPU_TRI_NOTEX : 1;
    if (sdl)
        ogpu_triangles(&B, AD(rv, vb), (unsigned long)n, (unsigned long)stride, iaddr, icount, isize, tex,
                       (unsigned long)ri(0, 4) | (pc(50) ? OGPU_TRI_BILINEAR : 0) | (pc(50) ? OGPU_TRI_CLAMP : 0));
    else
        ogpu_triangles3d(&B, AD(rv, vb), (unsigned long)n, (unsigned long)stride, iaddr, icount, isize, tex,
                         (pc(50) ? OGPU_TRI_BILINEAR : 0) | (pc(50) ? OGPU_TRI_CLAMP : 0), layout);
}

const unsigned char *ogpu_scene3d_random(struct ogpu_scene3d_env *e, unsigned long seed, long *words) {
    static const int tfmts[] = { OGPU_FMT_ARGB32, OGPU_FMT_ARGB32, OGPU_FMT_RGB565, OGPU_FMT_RGB565, OGPU_FMT_CLUT8 };
    static const int sfmts[] = { OGPU_FMT_ARGB32, OGPU_FMT_RGB565, OGPU_FMT_A8, OGPU_FMT_CLUT8 };
    struct rtex tx[4];
    int fmt, bpp, w, h, k, draws, rt = 0, rz, rsb, rtab, i;
    long bpr, mis, zbpr = 0;
    int zw = 0, zh = 0, zf = OGPU_FMT_Z16;
    E = e;
    rs = 0x9E3779B97F4A7C15ULL ^ ((unsigned long long)seed * 0x2545F4914F6CDD1DULL);
    if (!rs) rs = 1;
    for (i = 0; i < 4; i++) rn();
    fmt = tfmts[ri(0, 4)];
    bpp = fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 1;
    w = ri(8, 200); h = ri(8, 150);
    bpr = (long)w * bpp + (pc(30) ? ri(1, 7) : 0);
    mis = pc(25) ? ri(1, 3) : 0;
    rt = pc(60) ? 0 : 1;
    rz = pc(50); rsb = pc(50); rtab = pc(50);
    fill_random(rt, RT_TGT, bpr * h + 8);
    fill_random(rz, RT_Z, 0x30000);
    fill_random(rsb, RT_S, 0x8000);
    fill_random(rtab, RT_PAL, 1024 + 128);
    fill_random(rtab, RT_PEN, 32768);
    for (i = 0; i < 4; i++) random_texture(&tx[i], i);
    {
        int sf = sfmts[ri(0, 3)], sb = sf == OGPU_FMT_ARGB32 ? 4 : sf == OGPU_FMT_RGB565 ? 2 : 1, sw = ri(1, 64), sh = ri(1, 64);
        int rsl = pc(50);
        fill_random(rsl, RT_SLOT, (long)sw * sb * sh);
        ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
        ogpu_surface(&B, 1, AD(rsl, RT_SLOT), (unsigned long)sw * sb, sw, sh, sf);
    }
    ogpu_surface(&B, 0, AD(rt, RT_TGT + mis), (unsigned long)bpr, w, h, fmt);
    ogpu_target(&B, 0);
    if (pc(30)) ogpu_clip(&B, ri(-10, w), ri(-10, h), ri(0, w), ri(0, h));
    draws = ri(1, 5);
    for (k = 0; k < draws; k++) {
        if (k == 0 || pc(70)) random_render3d(fmt == OGPU_FMT_CLUT8, rtab);
        if (pc(50)) ogpu_texenv(&B, 0, ri(1, 7), rn());
        if (pc(60)) {
            if (pc(5)) ogpu_texture(&B, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0);
            else emit_texture(&tx[ri(0, 3)]);
        }
        if (k == 0 || pc(30)) {
            if (pc(85)) {
                zf = pc(50) ? OGPU_FMT_Z16 : OGPU_FMT_Z32;
                zw = pc(80) ? w : ri(1, w + 8); zh = pc(80) ? h : ri(1, h + 8);
                zbpr = (long)zw * (zf == OGPU_FMT_Z32 ? 4 : 2) + (pc(30) ? ri(1, 6) : 0);
                ogpu_depth(&B, OGPU_KIND_DEPTH, AD(rz, RT_Z + (pc(20) ? ri(1, 3) : 0)), (unsigned long)zbpr, zw, zh, zf);
            } else ogpu_depth(&B, OGPU_KIND_DEPTH, 0, 0, 0, 0, 0);
        }
        if (k == 0 || pc(20)) {
            if (pc(80)) {
                int sw = pc(80) ? w : ri(1, w + 8), sh = pc(80) ? h : ri(1, h + 8);
                ogpu_depth(&B, OGPU_KIND_STENCIL, AD(rsb, RT_S), (unsigned long)(sw + (pc(30) ? ri(1, 5) : 0)), sw, sh, OGPU_FMT_S8);
            } else ogpu_depth(&B, OGPU_KIND_STENCIL, 0, 0, 0, 0, 0);
        }
        if (pc(30)) ogpu_clear3d(&B, pc(70) ? OGPU_KIND_DEPTH : OGPU_KIND_STENCIL, ri(-5, w), ri(-5, h), ri(0, w + 5), ri(0, h + 5), rn());
        if (pc(5)) ogpu_clip(&B, ri(-10, w), ri(-10, h), ri(0, w), ri(0, h));
        if (pc(4)) ogpu_fill(&B, ri(0, w), ri(0, h), ri(0, w), ri(0, h), rn());
        random_triangles(k, w, h, pc(20));
    }
    (void)zw; (void)zh; (void)zbpr;
    *words = B.words;
    return sbuf;
}

/* ---- bench scenes -------------------------------------------------------------------- */

#define LAY_FULL (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC)
#define VS 32
struct V { double x, y, u, v, z, w; unsigned long c, s; };

static void put_vertex(int r, long at, const struct V *v) {
    wr32(r, at, fx16(v->x)); wr32(r, at + 4, fx16(v->y));
    wr32(r, at + 8, v->c); wr32(r, at + 12, fx16(v->u)); wr32(r, at + 16, fx16(v->v));
    wr32(r, at + 20, v->z >= 1.0 ? 0xFFFFFFFFUL : v->z <= 0 ? 0 : (unsigned long)(v->z * 4294967296.0));
    wr32(r, at + 24, fbits((float)v->w)); wr32(r, at + 28, v->s);
}

static void r3d(unsigned long *R, unsigned long en) {
    memset(R, 0, sizeof(unsigned long) * OGPU_R3D_ARGS);
    R[0] = en; R[1] = 4; R[2] = 8; R[3] = 2 | (1UL << 16); R[5] = 0xFF0F; R[6] = 4;
    R[12] = 2 | (0xFFUL << 16); R[13] = 1 | (1UL << 8) | (1UL << 16) | (0xFFUL << 24); R[14] = 1; R[17] = 0xFFFFFFFFUL;
}

/* A texture with its smaller levels (box-filtered), ARGB32, at region r. */
static void bench_texture(int r, long at, int w, int h, int levels, unsigned long *lad) {
    int lv, x, y;
    long a = at;
    for (lv = 0; lv < levels; lv++) {
        int lw = w >> lv, lh = h >> lv;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        if (lv) lad[lv - 1] = AD(r, a);
        for (y = 0; y < lh; y++)
            for (x = 0; x < lw; x++) {
                int X = x << lv, Y = y << lv, check = ((X / 16) + (Y / 16)) & 1;
                unsigned long c = check ? 0xFFE0C090UL : 0xFF4060A0UL;
                c ^= (unsigned long)(((X * 3 + Y * 5) & 31) << 2) * 0x010101UL;
                if (lv) c = (c & 0xFF000000UL) | (((c & 0xFEFEFE) >> 1) + 0x202020UL);
                if (((X ^ Y) & 63) < 3) c = 0x80FFFFFFUL;
                wr32(r, a + ((long)y * lw + x) * 4, c);
            }
        a += (long)lw * lh * 4;
    }
}

struct bench { const char *name; int w, h, fmt, region; long tgt; };
static const struct bench benches[OGPU_SCENE3D_BENCHES] = {
    { "w3dtest", 320, 240, OGPU_FMT_ARGB32, 0, 0 },
    { "cow3d", 320, 240, OGPU_FMT_ARGB32, 0, 0 },
    { "mgltest", 320, 240, OGPU_FMT_RGB565, 0, 0 },
    { "tiny", 320, 240, OGPU_FMT_ARGB32, 0, 0 },
    { "fill640", 640, 480, OGPU_FMT_ARGB32, 0, 0 },
    { "w3dtest-fastram", 320, 240, OGPU_FMT_ARGB32, 1, 0x40000L },
    { "fill640-affine", 640, 480, OGPU_FMT_ARGB32, 0, 0 },
    { "fill640-gouraud", 640, 480, OGPU_FMT_ARGB32, 0, 0 },
};

void ogpu_scene3d_bench_target(int which, int *region, long *offset, int *w, int *h, int *format) {
    const struct bench *b = &benches[which];
    *region = b->region; *offset = b->tgt; *w = b->w; *h = b->h; *format = b->fmt;
}

#define BZ   0x000000L          /* region 1: the Z buffer */
#define BTEX 0x100000L          /* region 1: textures */
#define BVB  0x180000L          /* region 1: vertices, then indices */

/* A point in the camera's space to the screen: x, y, w = 1/z, z for the Z buffer. */
static void project(struct V *v, double X, double Y, double Z, int sw, int sh) {
    double f = sh * 1.1;
    v->w = 1.0 / Z;
    v->x = sw / 2.0 + X * f / Z;
    v->y = sh / 2.0 - Y * f / Z;
    v->z = 1.0 - 1.0 / Z;
}

static long nv;
static void tri3(int r, const struct V *a, const struct V *b, const struct V *c) {
    put_vertex(r, BVB + nv * VS, a); put_vertex(r, BVB + (nv + 1) * VS, b); put_vertex(r, BVB + (nv + 2) * VS, c);
    nv += 3;
}

static void floor_quad(int sw, int sh, double y, double z0, double z1, double xs, double rep, unsigned long c0, unsigned long c1) {
    struct V v[4];
    int i;
    memset(v, 0, sizeof v);
    project(&v[0], -xs, y, z0, sw, sh); v[0].u = 0; v[0].v = 0;
    project(&v[1], xs, y, z0, sw, sh); v[1].u = rep; v[1].v = 0;
    project(&v[2], -xs, y, z1, sw, sh); v[2].u = 0; v[2].v = rep * (z1 - z0) / (2 * xs);
    project(&v[3], xs, y, z1, sw, sh); v[3].u = rep; v[3].v = v[2].v;
    for (i = 0; i < 4; i++) { v[i].c = i < 2 ? c0 : c1; v[i].s = 0xFF000000UL | (unsigned long)(i * 0x101010); }
    tri3(1, &v[0], &v[1], &v[2]);
    tri3(1, &v[1], &v[3], &v[2]);
}

/* A cube of side s at (cx, cy, cz), turned by a about y and b about x. */
static void cube(int sw, int sh, double cx, double cy, double cz, double s, double a, double b) {
    static const int face[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,4,5,1}, {2,3,7,6}, {0,2,6,4}, {1,5,7,3} };
    struct V p[8], q[4];
    int i, f;
    for (i = 0; i < 8; i++) {
        double x = (i & 1 ? s : -s) / 2, y = (i & 2 ? s : -s) / 2, z = (i & 4 ? s : -s) / 2, t;
        t = x * cos(a) + z * sin(a); z = -x * sin(a) + z * cos(a); x = t;
        t = y * cos(b) - z * sin(b); z = y * sin(b) + z * cos(b); y = t;
        memset(&p[i], 0, sizeof p[i]);
        project(&p[i], cx + x, cy + y, cz + z, sw, sh);
    }
    for (f = 0; f < 6; f++) {
        for (i = 0; i < 4; i++) {
            q[i] = p[face[f][i]];
            q[i].u = (i == 1 || i == 2) ? 1 : 0; q[i].v = i >= 2 ? 1 : 0;
            q[i].c = 0xFF000000UL | (unsigned long)((200 + f * 9) << 16) | (unsigned long)((150 + i * 25) << 8) | (unsigned long)(255 - f * 20);
            q[i].s = 0xFF000000UL;
        }
        tri3(1, &q[0], &q[1], &q[2]);
        tri3(1, &q[0], &q[2], &q[3]);
    }
}

const unsigned char *ogpu_scene3d_bench(struct ogpu_scene3d_env *e, int which, long *words, const char **name) {
    const struct bench *b = &benches[which];
    unsigned long R[OGPU_R3D_ARGS], lad[OGPU_TEX_LEVELS];
    int bpp = b->fmt == OGPU_FMT_ARGB32 ? 4 : 2, sw = b->w, sh = b->h, i, j;
    long first;
    E = e;
    *name = b->name;
    nv = 0;
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_surface(&B, 0, AD(b->region, b->tgt), (unsigned long)sw * bpp, sw, sh, b->fmt);
    ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, sw, sh, b->fmt == OGPU_FMT_ARGB32 ? 0xFF102030UL : 0x1086);
    ogpu_depth(&B, OGPU_KIND_DEPTH, AD(1, BZ), (unsigned long)sw * 2, sw, sh, OGPU_FMT_Z16);
    ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, sw, sh, 0xFFFFFFFFUL);
    switch (which) {
    case 0: case 5:                     /* W3DTest: a turning textured cube over a textured floor */
        bench_texture(1, BTEX, 128, 128, 1, lad);
        r3d(R, OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE);
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, AD(1, BTEX), 128 * 4, 128, 128, OGPU_FMT_ARGB32, 0, 1, 2, 2, 1, 1, 0, 1, 0);
        floor_quad(sw, sh, -1.0, 1.2, 12.0, 4.0, 4.0, 0xFFFFFFFFUL, 0xFF808080UL);
        cube(sw, sh, 0, 0.1, 4.0, 1.6, 0.6, 0.45);
        ogpu_triangles3d(&B, AD(1, BVB), (unsigned long)nv, VS, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_LIST);
        break;
    case 1: {                           /* Cow3D: a model of a couple of thousand small triangles */
        int nu = 48, nl = 24;
        long ib;
        bench_texture(1, BTEX, 64, 64, 1, lad);
        r3d(R, OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE);
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, AD(1, BTEX), 64 * 4, 64, 64, OGPU_FMT_ARGB32, 0, 1, 1, 1, 1, 1, 0, 1, 0);
        for (j = 0; j <= nl; j++)
            for (i = 0; i <= nu; i++) {
                double th = 3.14159265 * j / nl, ph = 2 * 3.14159265 * i / nu;
                double rr = 1.0 + 0.12 * sin(5 * ph) * sin(3 * th), X = rr * sin(th) * cos(ph) * 1.4, Y = rr * cos(th), Z = rr * sin(th) * sin(ph);
                double light = 0.35 + 0.65 * fabs(sin(th) * cos(ph - 0.7));
                struct V v;
                memset(&v, 0, sizeof v);
                project(&v, X, Y, 4.2 + Z, sw, sh);
                v.u = 4.0 * i / nu; v.v = 2.0 * j / nl;
                v.c = 0xFF000000UL | (unsigned long)(int)(255 * light) * 0x010101UL;
                v.s = 0xFF000000UL;
                put_vertex(1, BVB + nv * VS, &v);
                nv++;
            }
        ib = BVB + nv * VS;
        first = 0;
        for (j = 0; j < nl; j++)
            for (i = 0; i < nu; i++) {
                int a = j * (nu + 1) + i, idx[6], k;
                idx[0] = a; idx[1] = a + 1; idx[2] = a + nu + 1; idx[3] = a + 1; idx[4] = a + nu + 2; idx[5] = a + nu + 1;
                for (k = 0; k < 6; k++) { wr8(1, ib + first * 2, (unsigned long)idx[k] >> 8); wr8(1, ib + first * 2 + 1, (unsigned long)idx[k]); first++; }
            }
        ogpu_triangles3d(&B, AD(1, BVB), (unsigned long)nv, VS, AD(1, ib), (unsigned long)first, 2, OGPU_TRI_UNIT0, 0,
                         LAY_FULL | OGPU_LAY_LIST);
        break;
    }
    case 2: {                           /* MGLTest: a fogged mipmapped floor, blended panes over it */
        bench_texture(1, BTEX, 64, 64, 7, lad);
        r3d(R, OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE | OGPU_R3D_FOG);
        R[7] = OGPU_FOG_EXP; R[8] = 0xFF8090A0UL; R[11] = fbits(1.5f);
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, AD(1, BTEX), 64 * 4, 64, 64, OGPU_FMT_ARGB32, 0, 1, 6, 2, 1, 1, 0, 7, lad);
        floor_quad(sw, sh, -1.0, 1.0, 20.0, 6.0, 12.0, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
        ogpu_triangles3d(&B, AD(1, BVB), (unsigned long)nv, VS, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_LIST);
        first = nv;
        r3d(R, OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE | OGPU_R3D_BLEND);
        R[3] = 7 | (8UL << 16);
        ogpu_render3d(&B, R);
        for (i = 0; i < 6; i++) {
            struct V v[4];
            double x0 = -2.4 + i * 0.8, z = 2.0 + i * 0.6;
            memset(v, 0, sizeof v);
            project(&v[0], x0, -0.6, z, sw, sh); project(&v[1], x0 + 0.7, -0.6, z + 0.3, sw, sh);
            project(&v[2], x0, 0.6, z, sw, sh); project(&v[3], x0 + 0.7, 0.6, z + 0.3, sw, sh);
            for (j = 0; j < 4; j++) {
                v[j].u = j & 1; v[j].v = j >> 1;
                v[j].c = (unsigned long)(0x60 + 0x20 * j) << 24 | 0xFFC080UL;
                v[j].s = 0xFF000000UL;
            }
            tri3(1, &v[0], &v[1], &v[2]);
            tri3(1, &v[1], &v[3], &v[2]);
        }
        ogpu_triangles3d(&B, AD(1, BVB + first * VS), (unsigned long)(nv - first), VS, 0, 0, 0, OGPU_TRI_UNIT0, 0,
                         LAY_FULL | OGPU_LAY_LIST);
        break;
    }
    case 3:                             /* a few tiny triangles: quicker on the core */
        r3d(R, OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE);
        ogpu_render3d(&B, R);
        for (i = 0; i < 4; i++) {
            struct V v[3];
            memset(v, 0, sizeof v);
            v[0].x = 40 + i * 60; v[0].y = 100; v[1].x = 48 + i * 60; v[1].y = 101; v[2].x = 43 + i * 60; v[2].y = 108;
            for (j = 0; j < 3; j++) { v[j].z = 0.5; v[j].w = 1; v[j].c = 0xFF2080F0UL + (unsigned long)j * 0x300000UL; v[j].s = 0xFF000000UL; }
            tri3(1, &v[0], &v[1], &v[2]);
        }
        ogpu_triangles3d(&B, AD(1, BVB), (unsigned long)nv, VS, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL | OGPU_LAY_LIST);
        break;
    case 4: case 6: case 7:             /* 640x480, the screen covered: bilinear, fog, perspective (6: neither, 7: no texture) */
        bench_texture(1, BTEX, 256, 256, 1, lad);
        r3d(R, which == 7 ? OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE
                          : which == 6 ? OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE
                          : OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE | OGPU_R3D_FOG);
        R[7] = OGPU_FOG_LINEAR; R[8] = 0xFF303848UL; R[9] = fbits(0.9f); R[10] = fbits(0.05f);
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, AD(1, BTEX), 256 * 4, 256, 256, OGPU_FMT_ARGB32, 0, 1, 2, 2, 1, 1, 0, 1, 0);
        {
            struct V v[4];
            memset(v, 0, sizeof v);
            v[0].x = 0; v[0].y = 0; v[0].w = 0.08; v[0].u = 0; v[0].v = 0;
            v[1].x = sw; v[1].y = 0; v[1].w = 0.08; v[1].u = 6; v[1].v = 0;
            v[2].x = 0; v[2].y = sh; v[2].w = 1.0; v[2].u = 0; v[2].v = 6;
            v[3].x = sw; v[3].y = sh; v[3].w = 1.0; v[3].u = 6; v[3].v = 6;
            for (j = 0; j < 4; j++) { v[j].z = 1 - v[j].w; v[j].c = 0xFFFFFFFFUL - (unsigned long)j * 0x101000UL; v[j].s = 0xFF000000UL; }
            tri3(1, &v[0], &v[1], &v[2]);
            tri3(1, &v[1], &v[3], &v[2]);
        }
        ogpu_triangles3d(&B, AD(1, BVB), (unsigned long)nv, VS, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_LIST);
        break;
    }
    *words = B.words;
    return sbuf;
}
