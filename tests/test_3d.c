/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU 3D core tests (stream v1.2): coverage against an exact
 * reference with the top-left rule, shared edges drawn once, Gouraud, Z,
 * perspective texturing, fog, every blend factor, alpha, stencil, logic ops,
 * the colour mask, chroma, strips, fans and indices, SDL's form, random
 * streams that must stay inside their memory, and a golden scene whose
 * checksum every back end must reproduce (tests/golden/opengpu-3d.txt).
 *   test_ogpu3d [golden-file] [scene.ppm]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_3d.h"
#include "../include/opengpu/build3d.h"

#ifdef __amigaos__
/* On the Amiga the test keeps to the FPU instructions a 68040 has
 * (libm's would need 68040.library), with these small stand-ins. */
static double t_floor(double v) { long i = (long)v; return (double)(i - (v < (double)i)); }
static double t_fabs(double v) { return v < 0 ? -v : v; }
static double t_sqrt(double v) { double r = v > 1 ? v : 1; int i; if (v <= 0) return 0; for (i = 0; i < 40; i++) r = 0.5 * (r + v / r); return r; }
static double t_hypot(double a, double b) { return t_sqrt(a * a + b * b); }
static double t_exp(double x) { double r = 1, term = 1; int i, k = 0; while (x > 0.5 || x < -0.5) { x /= 2; k++; }
    for (i = 1; i < 20; i++) { term *= x / i; r += term; } while (k--) r *= r; return r; }
static double t_sin(double x) { double r = 0, term; int i; while (x > 3.14159265358979) x -= 6.28318530717959; while (x < -3.14159265358979) x += 6.28318530717959;
    term = x; for (i = 1; i < 20; i++) { r += term; term *= -x * x / ((2 * i) * (2 * i + 1)); } return r; }
static double t_cos(double x) { return t_sin(x + 1.5707963267949); }
#define floor t_floor
#define fabs t_fabs
#define hypot t_hypot
#define exp t_exp
#define sin t_sin
#define cos t_cos
#endif

#define ARENA (4L << 20)
static ogpu_u8 arena[ARENA];

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= (ogpu_u32)ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; if (failures < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static unsigned char sbuf[1 << 18];
static struct OGPUBatch B;
static struct ogpu_core C;
static struct ogpu3d T;

static int slow_spans;          /* 1: the general spans only, to compare with the fast ones */
static void begin(void) { ogpu_batch_init(&B, sbuf, sizeof sbuf / 4); }
static long run(void) {
    ogpu_core_init(&C);
    C.map = map; C.fence = 0; C.ext = 0; C.user = 0;
    ogpu3d_init(&T, &C);
    T.slow = slow_spans;
    return ogpu3d_run(&T, sbuf, B.words);
}

static unsigned long rnd_state = 4242;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }

#define W 96
#define H 64
#define TGT 0x1000L         /* target, ARGB32 */
#define ZB 0x20000L         /* depth */
#define SB 0x40000L         /* stencil */
#define TEX 0x50000L        /* textures */
#define VB 0x100000L        /* vertices */
#define IB 0x180000L        /* indices */
#define R3 0x1C0000L        /* tables */
#define BPR (W * 4)

static unsigned long rd32(long a) { const ogpu_u8 *p = arena + a; return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }
static void wr32(long a, unsigned long v) { ogpu_u8 *p = arena + a; p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }
static unsigned long pix(int x, int y) { return rd32(TGT + (long)y * BPR + x * 4); }
static unsigned long fbits(float f) { unsigned long b = 0; unsigned int u; memcpy(&u, &f, 4); b = u; return b; }

static void clear_target(unsigned long v) { int i; for (i = 0; i < W * H; i++) wr32(TGT + i * 4, v); }

/* A 3D vertex record: x, y, ARGB, u, v, z, w, spec (the full layout). */
#define LAY_FULL (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC)
#define VSIZE 32
struct V { double x, y, u, v, z, w; unsigned long c, s; };
static long fx16(double v) { return (long)floor(v * 65536.0 + 0.5); }
static void put_vertex(long at, const struct V *v) {
    wr32(at, (unsigned long)fx16(v->x) & 0xFFFFFFFFUL); wr32(at + 4, (unsigned long)fx16(v->y) & 0xFFFFFFFFUL);
    wr32(at + 8, v->c); wr32(at + 12, (unsigned long)fx16(v->u) & 0xFFFFFFFFUL); wr32(at + 16, (unsigned long)fx16(v->v) & 0xFFFFFFFFUL);
    wr32(at + 20, v->z >= 1.0 ? 0xFFFFFFFFUL : (unsigned long)(v->z * 4294967296.0));
    wr32(at + 24, fbits((float)v->w)); wr32(at + 28, v->s);
}

/* RENDER3D arguments with the defaults the tests start from. */
static unsigned long R[OGPU_R3D_ARGS];
static void r3d_defaults(void) {
    memset(R, 0, sizeof R);
    R[0] = OGPU_R3D_GOURAUD;
    R[1] = 2;                               /* Z LESS */
    R[2] = 8;                               /* alpha ALWAYS */
    R[3] = 2 | (1UL << 16);                 /* ONE, ZERO */
    R[5] = 0xFF0F;                          /* every channel, every pen bit */
    R[6] = 4;                               /* COPY */
    R[12] = 2 | (0xFFUL << 16);             /* stencil ALWAYS, mask 255 */
    R[13] = 1 | (1UL << 8) | (1UL << 16) | (0xFFUL << 24);
    R[14] = 1;                              /* chroma NONE */
    R[17] = 0xFFFFFFFFUL;                   /* current colour */
}
static void setup(void) {
    begin();
    ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
}

/* ---- exact reference coverage --------------------------------------------------------- */

/* Is the pixel centre (px+.5, py+.5) inside the triangle, by the top-left rule?
 * Positions in 28.4 as the core rounds them. */
static long r28(double v) { return (long)floor(v * 16.0 + 0.5); }
static int top_left(long ax, long ay, long bx, long by, int ccw) {
    /* with y down, for the winding that makes the area positive */
    long dx = bx - ax, dy = by - ay;
    if (!ccw) { dx = -dx; dy = -dy; }
    return (dy < 0) || (dy == 0 && dx > 0);
}
static int covered(const struct V *a, const struct V *b, const struct V *c, int px, int py) {
    long x0 = r28(a->x), y0 = r28(a->y), x1 = r28(b->x), y1 = r28(b->y), x2 = r28(c->x), y2 = r28(c->y);
    long cx = px * 16 + 8, cy = py * 16 + 8;
    long long area = (long long)(x1 - x0) * (y2 - y0) - (long long)(x2 - x0) * (y1 - y0);
    long long e0, e1, e2;
    int pos;
    if (!area) return 0;
    pos = area > 0;
    e0 = (long long)(x1 - x0) * (cy - y0) - (long long)(y1 - y0) * (cx - x0);
    e1 = (long long)(x2 - x1) * (cy - y1) - (long long)(y2 - y1) * (cx - x1);
    e2 = (long long)(x0 - x2) * (cy - y2) - (long long)(y0 - y2) * (cx - x2);
    if (!pos) { e0 = -e0; e1 = -e1; e2 = -e2; }
    /* inside: every edge function positive, or zero on a top or left edge */
    if (e0 < 0 || e1 < 0 || e2 < 0) return 0;
    if (e0 == 0 && !top_left(x0, y0, x1, y1, pos)) return 0;
    if (e1 == 0 && !top_left(x1, y1, x2, y2, pos)) return 0;
    if (e2 == 0 && !top_left(x2, y2, x0, y0, pos)) return 0;
    return 1;
}

static void test_coverage(void) {
    int n, x, y, bad = 0;
    for (n = 0; n < 400; n++) {
        struct V v[3];
        int k;
        memset(v, 0, sizeof v);
        for (k = 0; k < 3; k++) {
            /* some on whole and half pixels, to hit the edge cases */
            int mode = (int)(rnd() % 3);
            double s = mode == 0 ? 1.0 : mode == 1 ? 0.5 : 1.0 / 16;
            v[k].x = floor(((double)(rnd() % 1400) / 10.0 - 20.0) / s) * s;
            v[k].y = floor(((double)(rnd() % 1000) / 10.0 - 15.0) / s) * s;
            v[k].c = 0xFF00FF00UL; v[k].w = 1;
        }
        clear_target(0);
        setup();
        r3d_defaults();
        R[0] = 0;
        R[17] = 0xFFFFFFFFUL;
        ogpu_render3d(&B, R);
        put_vertex(VB, &v[0]); put_vertex(VB + VSIZE, &v[1]); put_vertex(VB + 2 * VSIZE, &v[2]);
        ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        run();
        CHECK(C.last_error == OGPU_OK, "coverage run %d", C.last_error);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int want = covered(&v[0], &v[1], &v[2], x, y), got = pix(x, y) == 0xFFFFFFFFUL;
                if (want != got && bad++ < 5)
                    CHECK(0, "triangle %d pixel %d,%d: core %d, reference %d (%.4f,%.4f %.4f,%.4f %.4f,%.4f)", n, x, y, got, want,
                          v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y);
            }
    }
    CHECK(bad == 0, "%d coverage differences", bad);
}

/* A fan of triangles around a point: every pixel inside drawn exactly once. */
static void test_shared_edges(void) {
    int n, x, y, over = 0, holes = 0;
    for (n = 0; n < 50; n++) {
        struct V v[13];
        double cx = 30 + (double)(rnd() % 3000) / 100.0, cy = 20 + (double)(rnd() % 2000) / 100.0;
        int k, m = 12;
        memset(v, 0, sizeof v);
        v[0].x = cx; v[0].y = cy;
        for (k = 1; k <= m; k++) {
            double a = (k - 1) * 2 * 3.14159265358979 / m + (double)(rnd() % 100) / 400.0;
            double r = 15 + (double)(rnd() % 1500) / 100.0;
            v[k].x = cx + r * cos(a); v[k].y = cy + r * sin(a);
        }
        for (k = 0; k <= m; k++) { v[k].c = 0xFF010101UL; v[k].w = 1; put_vertex(VB + k * VSIZE, &v[k]); }
        /* indices close the fan: 0 1 2 .. m 1 */
        for (k = 0; k <= m; k++) arena[IB + k] = (ogpu_u8)k;
        arena[IB + m + 1] = 1;
        clear_target(0xFF000000UL);
        setup();
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_BLEND;
        R[3] = 2 | (2UL << 16);                 /* ONE, ONE: overlaps would add up */
        ogpu_render3d(&B, R);
        ogpu_triangles3d(&B, VB, (unsigned long)m + 1, VSIZE, IB, (unsigned long)m + 2, 1, OGPU_TRI_NOTEX, 0, LAY_FULL | OGPU_LAY_FAN);
        run();
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long p = pix(x, y) & 0xFFFFFF;
                if (p > 0x010101) over++;
                /* a pixel well inside the polygon must be drawn */
                if (p == 0 && hypot(x + 0.5 - cx, y + 0.5 - cy) < 13) holes++;
            }
    }
    CHECK(over == 0, "%d pixels drawn twice along shared edges", over);
    CHECK(holes == 0, "%d holes inside fans", holes);
}

/* ---- values across a triangle ---------------------------------------------------------- */

static double bary(const struct V *a, const struct V *b, const struct V *c, double px, double py, double *l1, double *l2) {
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    *l1 = ((px - a->x) * (c->y - a->y) - (c->x - a->x) * (py - a->y)) / area;
    *l2 = ((b->x - a->x) * (py - a->y) - (px - a->x) * (b->y - a->y)) / area;
    return area;
}

static void test_gouraud_and_z(void) {
    struct V v[3];
    int x, y, worst = 0, zbad = 0;
    memset(v, 0, sizeof v);
    v[0].x = 3.3125; v[0].y = 2.6875; v[0].c = 0xFFFF0000UL; v[0].z = 0.2; v[0].w = 1;
    v[1].x = 90.125; v[1].y = 10.625; v[1].c = 0x8000FF00UL; v[1].z = 0.9; v[1].w = 1;
    v[2].x = 20.5; v[2].y = 61.1875; v[2].c = 0x200000FFUL; v[2].z = 0.5; v[2].w = 1;
    clear_target(0);
    memset(arena + ZB, 0, W * H * 4);
    setup();
    ogpu_depth(&B, OGPU_KIND_DEPTH, ZB, W * 4, W, H, OGPU_FMT_Z32);
    ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, W, H, 0xFFFFFFFFUL);
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE;
    ogpu_render3d(&B, R);
    put_vertex(VB, &v[0]); put_vertex(VB + VSIZE, &v[1]); put_vertex(VB + 2 * VSIZE, &v[2]);
    ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
    run();
    CHECK(C.last_error == OGPU_OK, "gouraud run %d", C.last_error);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double l1, l2, l0, ch[4], z;
            unsigned long p = pix(x, y), zz = rd32(ZB + (long)(y * W + x) * 4);
            int k, sh;
            if (!covered(&v[0], &v[1], &v[2], x, y)) { CHECK(zz == 0xFFFFFFFFUL, "Z written outside at %d,%d", x, y); continue; }
            bary(&v[0], &v[1], &v[2], x + 0.5, y + 0.5, &l1, &l2);
            l0 = 1 - l1 - l2;
            for (k = 0, sh = 24; k < 4; k++, sh -= 8) {
                ch[k] = l0 * ((v[0].c >> sh) & 255) + l1 * ((v[1].c >> sh) & 255) + l2 * ((v[2].c >> sh) & 255);
                {
                    int d = abs((int)((p >> sh) & 255) - (int)floor(ch[k]));
                    if (d > worst) worst = d;
                }
            }
            z = l0 * v[0].z + l1 * v[1].z + l2 * v[2].z;
            if (fabs((double)zz / 4294967296.0 - z) > 1e-5) { if (!zbad) printf("z at %d,%d: %.7f want %.7f\n", x, y, (double)zz / 4294967296.0, z); zbad++; }
        }
    CHECK(worst <= 1, "Gouraud off by %d", worst);
    CHECK(zbad == 0, "%d Z values off", zbad);
}

/* Two triangles crossing in depth: each pixel shows the nearer one. */
static void test_depth_order(void) {
    struct V a[3], b[3];
    int x, y, bad = 0, i;
    memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    a[0].x = 0; a[0].y = 0; a[1].x = 96; a[1].y = 0; a[2].x = 0; a[2].y = 64;
    b[0].x = 96; b[0].y = 64; b[1].x = 96; b[1].y = 0; b[2].x = 0; b[2].y = 64;
    for (i = 0; i < 3; i++) { a[i].c = 0xFFFF0000UL; b[i].c = 0xFF0000FFUL; a[i].w = b[i].w = 1; }
    a[0].z = 0.1; a[1].z = 0.9; a[2].z = 0.9;       /* near at the top left */
    b[0].z = 0.1; b[1].z = 0.5; b[2].z = 0.5;
    for (i = 0; i < 2; i++) {
        int fmt = i ? OGPU_FMT_Z16 : OGPU_FMT_Z32;
        clear_target(0);
        setup();
        ogpu_depth(&B, OGPU_KIND_DEPTH, ZB, W * 4, W, H, fmt);
        ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, W, H, 0xFFFFFFFFUL);
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE;
        ogpu_render3d(&B, R);
        put_vertex(VB, &a[0]); put_vertex(VB + VSIZE, &a[1]); put_vertex(VB + 2 * VSIZE, &a[2]);
        put_vertex(VB + 3 * VSIZE, &b[0]); put_vertex(VB + 4 * VSIZE, &b[1]); put_vertex(VB + 5 * VSIZE, &b[2]);
        ogpu_triangles3d(&B, VB, 6, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        run();
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                double l1, l2, za = 2, zb = 2;
                if (covered(&a[0], &a[1], &a[2], x, y)) { bary(&a[0], &a[1], &a[2], x + .5, y + .5, &l1, &l2); za = (1 - l1 - l2) * a[0].z + l1 * a[1].z + l2 * a[2].z; }
                if (covered(&b[0], &b[1], &b[2], x, y)) { bary(&b[0], &b[1], &b[2], x + .5, y + .5, &l1, &l2); zb = (1 - l1 - l2) * b[0].z + l1 * b[1].z + l2 * b[2].z; }
                if (fabs(za - zb) < 0.002) continue;    /* too close to call at 16 bits */
                if (za == 2 && zb == 2) continue;
                if ((za < zb ? 0xFFFF0000UL : 0xFF0000FFUL) != pix(x, y)) bad++;
            }
    }
    CHECK(bad == 0, "%d pixels show the farther triangle", bad);
}

/* ---- textures ---------------------------------------------------------------------- */

static void make_texture(long at, int w, int h) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            wr32(at + (long)(y * w + x) * 4, 0xFF000000UL | ((unsigned long)(x * 255 / (w - 1)) << 16)
                 | ((unsigned long)(y * 255 / (h - 1)) << 8) | (((x / 4 + y / 4) & 1) ? 0xFF : 0));
}

static void quad(long at, double x0, double y0, double x1, double y1, double u0, double v0, double u1, double v1, double w0, double w1) {
    struct V v[4];
    int k;
    memset(v, 0, sizeof v);
    v[0].x = x0; v[0].y = y0; v[0].u = u0; v[0].v = v0; v[0].w = w0;
    v[1].x = x1; v[1].y = y0; v[1].u = u1; v[1].v = v0; v[1].w = w0;
    v[2].x = x0; v[2].y = y1; v[2].u = u0; v[2].v = v1; v[2].w = w1;
    v[3].x = x1; v[3].y = y1; v[3].u = u1; v[3].v = v1; v[3].w = w1;
    for (k = 0; k < 4; k++) { v[k].c = 0xFFFFFFFFUL; v[k].s = 0xFF000000UL; put_vertex(at + k * VSIZE, &v[k]); }
}

static void test_texture_copy(void) {
    int x, y, bad = 0;
    make_texture(TEX, 32, 32);
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 32 * 4, 32, 32, OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, OGPU_TEXBASE_RGBA), 0, 1, 1, 1,
                 OGPU_WRAP_REPEAT, OGPU_WRAP_REPEAT, 0, 1, 0);
    quad(VB, 10, 5, 42, 37, 0, 0, 1, 1, 1, 1);
    ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    run();
    CHECK(C.last_error == OGPU_OK, "texture run %d", C.last_error);
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            if (pix(10 + x, 5 + y) != rd32(TEX + (long)(y * 32 + x) * 4)) bad++;
    CHECK(bad == 0, "%d texels not copied 1:1", bad);
    CHECK(pix(9, 5) == 0 && pix(42, 5) == 0 && pix(10, 4) == 0 && pix(10, 37) == 0, "drawn outside the quad");
}

/* A floor going into the distance: perspective-correct texture coordinates
 * against a floating-point reference. */
static void test_perspective(void) {
    struct V v[3];
    int x, y, n = 0, bad = 0;
    make_texture(TEX, 64, 64);
    memset(v, 0, sizeof v);
    v[0].x = 5; v[0].y = 60; v[0].w = 1.0; v[0].u = 0; v[0].v = 0;
    v[1].x = 90; v[1].y = 60; v[1].w = 1.0; v[1].u = 4; v[1].v = 0;
    v[2].x = 48; v[2].y = 4; v[2].w = 0.05; v[2].u = 2; v[2].v = 6;
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 64 * 4, 64, 64, OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, OGPU_TEXBASE_RGBA), 0, 1, 1, 1,
                 OGPU_WRAP_REPEAT, OGPU_WRAP_REPEAT, 0, 1, 0);
    for (x = 0; x < 3; x++) { v[x].c = 0xFFFFFFFFUL; put_vertex(VB + x * VSIZE, &v[x]); }
    ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL);
    run();
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double l1, l2, l0, q, u, vv;
            int tx, ty, ok = 0, dx, dy;
            if (!covered(&v[0], &v[1], &v[2], x, y)) continue;
            bary(&v[0], &v[1], &v[2], x + .5, y + .5, &l1, &l2);
            l0 = 1 - l1 - l2;
            q = l0 * v[0].w + l1 * v[1].w + l2 * v[2].w;
            u = (l0 * v[0].u * v[0].w + l1 * v[1].u * v[1].w + l2 * v[2].u * v[2].w) / q * 64;
            vv = (l0 * v[0].v * v[0].w + l1 * v[1].v * v[1].w + l2 * v[2].v * v[2].w) / q * 64;
            n++;
            /* within a texel of the exact place (the core divides every 8 pixels) */
            for (dy = -1; dy <= 1 && !ok; dy++)
                for (dx = -1; dx <= 1 && !ok; dx++) {
                    tx = ((int)floor(u) + dx) & 63; ty = ((int)floor(vv) + dy) & 63;
                    if (pix(x, y) == rd32(TEX + (long)(ty * 64 + tx) * 4)) ok = 1;
                }
            if (!ok) bad++;
        }
    CHECK(n > 1000 && bad * 100 <= n, "perspective: %d of %d pixels more than a texel away", bad, n);
}

static void test_mipmaps_and_bilinear(void) {
    unsigned long lv[5];
    int i, x, y, s;
    /* a 16x16 texture whose levels are flat colours, to see which level is used */
    for (i = 0, s = 16; i < 5; i++, s >>= 1) {
        long at = TEX + 0x2000L * i;
        unsigned long col = i == 0 ? 0xFFFF0000UL : i == 1 ? 0xFF00FF00UL : i == 2 ? 0xFF0000FFUL : 0xFFFFFFFFUL;
        for (y = 0; y < s * s; y++) wr32(at + y * 4, col);
        if (i) lv[i - 1] = (unsigned long)at;
    }
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 16 * 4, 16, 16, OGPU_FMT_ARGB32, 0, 1, 3, 1, OGPU_WRAP_REPEAT, OGPU_WRAP_REPEAT, 0, 5, lv);
    quad(VB, 0, 0, 16, 16, 0, 0, 1, 1, 1, 1);          /* 1:1: level 0 */
    quad(VB + 4 * VSIZE, 20, 0, 28, 8, 0, 0, 1, 1, 1, 1);  /* half size: level 1 */
    quad(VB + 8 * VSIZE, 30, 0, 34, 4, 0, 0, 1, 1, 1, 1);  /* quarter: level 2 */
    ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    ogpu_triangles3d(&B, VB + 4 * VSIZE, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    ogpu_triangles3d(&B, VB + 8 * VSIZE, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    run();
    CHECK(pix(8, 8) == 0xFFFF0000UL, "level 0 at 1:1, got %08lx", pix(8, 8));
    CHECK(pix(24, 4) == 0xFF00FF00UL, "level 1 at 1:2, got %08lx", pix(24, 4));
    CHECK(pix(32, 2) == 0xFF0000FFUL, "level 2 at 1:4, got %08lx", pix(32, 2));
    /* bilinear on a 2x1 black and white texture stretched: a ramp */
    wr32(TEX, 0xFF000000UL); wr32(TEX + 4, 0xFFFFFFFFUL);
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 8, 2, 1, OGPU_FMT_ARGB32, 0, 1, 2, 2, OGPU_WRAP_CLAMP, OGPU_WRAP_CLAMP, 0, 1, 0);
    quad(VB, 0, 0, 64, 4, 0, 0, 1, 1, 1, 1);
    ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    run();
    {
        int prev = -1, mono = 1;
        for (x = 0; x < 64; x++) { int g = (int)((pix(x, 1) >> 8) & 255); if (g < prev) mono = 0; prev = g; }
        CHECK(mono && (pix(0, 1) & 255) == 0 && (pix(63, 1) & 255) == 255 && ((pix(32, 1) & 255) > 100 && (pix(32, 1) & 255) < 155),
              "bilinear ramp %08lx %08lx %08lx", pix(0, 1), pix(32, 1), pix(63, 1));
    }
    /* Mipmapped triangles whose areas are powers of two (in 28.4 units, 2^4 to
     * 2^15): the level of detail takes log2 of them. GCC 6.5 built the old
     * loop version of that for the 68k so that it never ended for these. */
    for (i = 0, s = 16; i < 5; i++, s >>= 1) {
        long at = TEX + 0x2000L * i;
        for (y = 0; y < s * s; y++) wr32(at + y * 4, 0xFF808080UL);
        if (i) lv[i - 1] = (unsigned long)at;
    }
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 16 * 4, 16, 16, OGPU_FMT_ARGB32, 0, 1, 5, 2, OGPU_WRAP_REPEAT, OGPU_WRAP_REPEAT, 0, 5, lv);
    for (i = 0; i < 12; i++) {
        struct V v[3];
        int k;
        memset(v, 0, sizeof v);
        v[0].x = 1; v[0].y = 1 + i * 4;
        v[1].x = 1 + (double)(1 << i) / 16.0; v[1].y = v[0].y;
        v[2].x = 1; v[2].y = v[0].y + 1;
        v[1].u = 1; v[2].v = 1;
        for (k = 0; k < 3; k++) { v[k].c = 0xFFFFFFFFUL; v[k].w = 0.5 + k * 0.25; v[k].s = 0xFF000000UL; put_vertex(VB + (i * 3 + k) * VSIZE, &v[k]); }
    }
    ogpu_triangles3d(&B, VB, 36, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL);
    run();
    CHECK(pix(1, 1 + 11 * 4) == 0xFF808080UL, "the widest power-of-two triangle drawn, got %08lx", pix(1, 1 + 11 * 4));
}

/* ---- the fragment tests, one pixel each, against floating-point sums --------------- */

static unsigned long one_pixel(unsigned long dst, unsigned long src_colour) {
    struct V v[3];
    int k;
    memset(v, 0, sizeof v);
    v[0].x = 0; v[0].y = 0; v[1].x = 4; v[1].y = 0; v[2].x = 0; v[2].y = 4;
    for (k = 0; k < 3; k++) { v[k].c = src_colour; v[k].w = 1; v[k].s = 0xFF000000UL; put_vertex(VB + k * VSIZE, &v[k]); }
    clear_target(dst);
    setup();
    ogpu_render3d(&B, R);
    ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
    run();
    return pix(0, 0);
}

static double fac(int f, int ch, const double *s, const double *d, const double *k) {
    switch (f) {
    case 1: return 0;
    case 2: return 1;
    case 3: return s[ch];
    case 4: return d[ch];
    case 5: return 1 - s[ch];
    case 6: return 1 - d[ch];
    case 7: return s[3];
    case 8: return 1 - s[3];
    case 9: return d[3];
    case 10: return 1 - d[3];
    case 11: return ch == 3 ? 1 : (s[3] < 1 - d[3] ? s[3] : 1 - d[3]);
    case 12: return k[ch];
    case 13: return 1 - k[ch];
    case 14: return k[3];
    case 15: return 1 - k[3];
    }
    return 0;
}

static void test_blend_factors(void) {
    unsigned long src = 0x80C04020UL, dst = 0x40306090UL, con = 0x60A0B0C0UL;
    double s[4], d[4], k[4];
    int fs, fd, ch, worst = 0;
    for (ch = 0; ch < 4; ch++) {
        int sh = ch == 3 ? 24 : 16 - ch * 8;
        s[ch] = ((src >> sh) & 255) / 255.0; d[ch] = ((dst >> sh) & 255) / 255.0; k[ch] = ((con >> sh) & 255) / 255.0;
    }
    for (fs = 1; fs <= 15; fs++)
        for (fd = 1; fd <= 15; fd++) {
            unsigned long p;
            r3d_defaults();
            R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_BLEND;
            R[3] = (unsigned long)fs | ((unsigned long)fd << 16);
            R[4] = con;
            p = one_pixel(dst, src);
            for (ch = 0; ch < 4; ch++) {
                int sh = ch == 3 ? 24 : 16 - ch * 8;
                double want = s[ch] * fac(fs, ch, s, d, k) + d[ch] * fac(fd, ch, s, d, k);
                int w = (int)floor((want > 1 ? 1 : want) * 255 + 0.5), got = (int)((p >> sh) & 255), e = abs(w - got);
                if (e > worst) worst = e;
                if (e > 2) CHECK(0, "blend %d,%d channel %d: %d, want %d", fs, fd, ch, got, w);
            }
        }
    CHECK(worst <= 2, "blend factors off by %d", worst);
}

static void test_alpha_stencil_logic_mask(void) {
    int f, i;
    /* alpha test: incoming alpha 0x80 against ref 0x80 */
    for (f = 1; f <= 8; f++) {
        static const int want[9] = { 0, 0, 0, 1, 1, 0, 0, 1, 1 };   /* NEVER LESS GEQUAL LEQUAL GREATER NOTEQUAL EQUAL ALWAYS */
        unsigned long p;
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ALPHATEST;
        R[2] = (unsigned long)f | (0x80UL << 16);
        p = one_pixel(0, 0x80FF0000UL);
        CHECK((p != 0) == want[f], "alpha function %d: %08lx", f, p);
    }
    /* stencil: ref 5 against 7, then the ops */
    for (f = 1; f <= 8; f++) {
        static const int want[9] = { 0, 0, 1, 1, 1, 0, 0, 0, 1 };   /* NEVER ALWAYS LESS LEQUAL EQUAL GEQUAL GREATER NOTEQUAL */
        unsigned long p;
        memset(arena + SB, 7, W * H);
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_STENCIL;
        R[12] = (unsigned long)f | (5UL << 8) | (0xFFUL << 16);
        R[13] = 2 | (6UL << 8) | (3UL << 16) | (0xFFUL << 24);  /* fail ZERO, zfail INVERT, zpass REPLACE */
        {
            struct V v[3];
            int k;
            memset(v, 0, sizeof v);
            v[1].x = 4; v[2].y = 4;
            for (k = 0; k < 3; k++) { v[k].c = 0xFFFF0000UL; v[k].w = 1; put_vertex(VB + k * VSIZE, &v[k]); }
            clear_target(0);
            setup();
            ogpu_depth(&B, OGPU_KIND_STENCIL, SB, W, W, H, OGPU_FMT_S8);
            ogpu_render3d(&B, R);
            ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
            run();
        }
        p = pix(0, 0);
        CHECK((p != 0) == want[f], "stencil function %d: %08lx", f, p);
        CHECK(arena[SB] == (want[f] ? 5 : 0), "stencil op after function %d: %d", f, arena[SB]);
    }
    /* stencil ops on their own */
    {
        static const int start = 200, ops[9] = { 0, 200, 0, 5, 201, 199, 55, 201, 199 };
        for (i = 1; i <= 8; i++) {
            struct V v[3];
            int k;
            memset(arena + SB, start, W * H);
            r3d_defaults();
            R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_STENCIL;
            R[12] = 2 | (5UL << 8) | (0xFFUL << 16);
            R[13] = 1 | (1UL << 8) | ((unsigned long)i << 16) | (0xFFUL << 24);
            memset(v, 0, sizeof v);
            v[1].x = 4; v[2].y = 4;
            for (k = 0; k < 3; k++) { v[k].c = 0xFFFF0000UL; v[k].w = 1; put_vertex(VB + k * VSIZE, &v[k]); }
            setup();
            ogpu_depth(&B, OGPU_KIND_STENCIL, SB, W, W, H, OGPU_FMT_S8);
            ogpu_render3d(&B, R);
            ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
            run();
            CHECK(arena[SB] == ops[i], "stencil op %d: %d, want %d", i, arena[SB], ops[i]);
        }
    }
    /* logic ops on raw ARGB32 pixels */
    for (f = 1; f <= 16; f++) {
        unsigned long s = 0xF0CC33AAUL, d = 0x0F55AA0FUL, want, p;
        switch (f) {
        case 1: want = 0; break;
        case 2: want = s & d; break;
        case 3: want = s & ~d; break;
        case 4: want = s; break;
        case 5: want = ~s & d; break;
        case 6: want = d; break;
        case 7: want = s ^ d; break;
        case 8: want = s | d; break;
        case 9: want = ~(s | d); break;
        case 10: want = ~(s ^ d); break;
        case 11: want = ~d; break;
        case 12: want = s | ~d; break;
        case 13: want = ~s; break;
        case 14: want = ~s | d; break;
        case 15: want = ~(s & d); break;
        default: want = 0xFFFFFFFFUL;
        }
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_LOGICOP;
        R[6] = (unsigned long)f;
        p = one_pixel(d, s);
        CHECK(p == (want & 0xFFFFFFFFUL), "logic op %d: %08lx, want %08lx", f, p, want & 0xFFFFFFFFUL);
    }
    /* the colour mask: green and alpha only */
    r3d_defaults();
    R[5] = 2 | 8;
    CHECK(one_pixel(0x11223344UL, 0xAABBCCDDUL) == 0xAA22CC44UL, "colour mask: %08lx", pix(0, 0));
}

static void test_fog_and_env(void) {
    struct V v[3];
    int k, x, worst = 0;
    /* linear fog from w = 1 (left, clear) to w = 0 (right, all fog) */
    memset(v, 0, sizeof v);
    v[0].x = 0; v[0].y = 0; v[0].w = 1;
    v[1].x = 96; v[1].y = 0; v[1].w = 0;
    v[2].x = 0; v[2].y = 64; v[2].w = 1;
    for (k = 0; k < 3; k++) { v[k].c = 0xFFFF0000UL; v[k].s = 0xFF000000UL; }
    v[1].c = 0xFFFF0000UL;
    for (k = 0; k < 3; k++) put_vertex(VB + k * VSIZE, &v[k]);
    for (k = 1; k <= 3; k++) {
        clear_target(0);
        setup();
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_FOG;
        R[7] = (unsigned long)k;
        R[8] = 0xFF0000FFUL;                                  /* blue fog */
        R[9] = fbits(0.75f); R[10] = fbits(0.25f); R[11] = fbits(1.5f);
        ogpu_render3d(&B, R);
        ogpu_triangles3d(&B, VB, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        run();
        for (x = 0; x < 90; x++) {
            double w = 1 - (x + 0.5) / 96.0 - 0.5 / 64.0 * 0;   /* along row 0: w falls with x */
            double l1, l2, f, d;
            int got;
            bary(&v[0], &v[1], &v[2], x + .5, .5, &l1, &l2);
            w = (1 - l1 - l2) * 1 + l1 * 0 + l2 * 1;
            d = 1 - w;
            f = k == 1 ? (w - 0.25) / (0.75 - 0.25) : k == 2 ? exp(-1.5 * d) : exp(-(1.5 * d) * (1.5 * d));
            if (f < 0) f = 0;
            if (f > 1) f = 1;
            got = (int)((pix(x, 0) >> 16) & 255);
            if (abs(got - (int)(f * 255 + 0.5)) > worst) worst = abs(got - (int)(f * 255 + 0.5));
        }
    }
    CHECK(worst <= 3, "fog off by %d", worst);
    /* environments on one texel: texture 0x80 40 C0 (alpha 0x60), colour 0xFF 80 80 (alpha 0xC0) */
    {
        static const struct { int mode, base; unsigned long want; } e[] = {
            { OGPU_ENV_REPLACE, OGPU_TEXBASE_RGBA, 0x608040C0UL },
            { OGPU_ENV_MODULATE, OGPU_TEXBASE_RGBA, 0x48802060UL },
            { OGPU_ENV_DECAL, OGPU_TEXBASE_RGBA, 0xC0CF6898UL },
            { OGPU_ENV_REPLACE, OGPU_TEXBASE_RGB, 0xC08040C0UL },
            { OGPU_ENV_REPLACE, OGPU_TEXBASE_ALPHA, 0x60FF8080UL },
            { OGPU_ENV_MODULATE, OGPU_TEXBASE_LUMINANCE, 0xC0804040UL },
            { OGPU_ENV_REPLACE, OGPU_TEXBASE_INTENSITY, 0x80808080UL },
            { OGPU_ENV_ADD, OGPU_TEXBASE_RGB, 0xC0FFC0FFUL },
            { OGPU_ENV_SUB, OGPU_TEXBASE_RGB, 0xC07F4000UL },
            { OGPU_ENV_OFF, OGPU_TEXBASE_RGBA, 0xC0FF8080UL },
        };
        for (k = 0; k < (int)(sizeof e / sizeof e[0]); k++) {
            unsigned long p;
            int ch, bad = 0;
            wr32(TEX, 0x608040C0UL);
            clear_target(0);
            setup();
            r3d_defaults();
            R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE;
            ogpu_render3d(&B, R);
            ogpu_texenv(&B, 0, e[k].mode, 0);
            ogpu_texture(&B, 0, TEX, 4, 1, 1, OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, e[k].base), 0, 1, 1, 1, 1, 1, 0, 1, 0);
            quad(VB, 0, 0, 4, 4, 0, 0, 1, 1, 1, 1);
            for (ch = 0; ch < 4; ch++) wr32(VB + ch * VSIZE + 8, 0xC0FF8080UL);
            ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
            run();
            p = pix(1, 1);
            for (ch = 0; ch < 32; ch += 8) if (abs((int)((p >> ch) & 255) - (int)((e[k].want >> ch) & 255)) > 1) bad = 1;
            CHECK(!bad, "environment %d base %d: %08lx, want %08lx", e[k].mode, e[k].base, p, e[k].want);
        }
    }
    /* chroma: texels inside 0x70..0x90 red are dropped (EXCLUSIVE) */
    wr32(TEX, 0xFF804040UL); wr32(TEX + 4, 0xFF204040UL);
    clear_target(0);
    setup();
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_TEXTURE | OGPU_R3D_CHROMA;
    R[14] = 3; R[15] = 0x00700000UL; R[16] = 0x0090FFFFUL;
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_REPLACE, 0);
    ogpu_texture(&B, 0, TEX, 8, 2, 1, OGPU_FMT_ARGB32, 0, 1, 1, 1, 2, 2, 0, 1, 0);
    quad(VB, 0, 0, 8, 2, 0, 0, 1, 1, 1, 1);
    ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
    run();
    CHECK(pix(1, 0) == 0 && pix(6, 0) == 0xFF204040UL, "chroma: %08lx %08lx", pix(1, 0), pix(6, 0));
}

/* The same mesh as a list, a strip, a fan and through 1, 2 and 4-byte indices. */
static void test_primitives(void) {
    struct V v[6];
    unsigned long sums[6];
    int k, form;
    memset(v, 0, sizeof v);
    for (k = 0; k < 6; k++) {
        v[k].x = 10 + (k / 2) * 30 + (k & 1) * 5; v[k].y = (k & 1) ? 55 : 5;
        v[k].c = 0xFF000000UL | (unsigned long)(k * 40) << 8 | (unsigned long)(255 - k * 40); v[k].w = 1;
    }
    for (form = 0; form < 6; form++) {
        long i;
        unsigned long h = 2166136261UL;
        clear_target(0);
        setup();
        r3d_defaults();
        ogpu_render3d(&B, R);
        if (form == 0) {                        /* list: (0 1 2) (1 3 2)... as the strip draws them */
            static const int L[12] = { 0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5 };
            for (k = 0; k < 12; k++) put_vertex(VB + k * VSIZE, &v[L[k]]);
            ogpu_triangles3d(&B, VB, 12, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        } else if (form == 1) {
            for (k = 0; k < 6; k++) put_vertex(VB + k * VSIZE, &v[k]);
            ogpu_triangles3d(&B, VB, 6, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL | OGPU_LAY_STRIP);
        } else if (form == 2) {                 /* a fan around 2 with 0 1 .. drawn as the strip's triangles: compare to its own list */
            static const int L[5] = { 2, 0, 1, 3, 4 };
            for (k = 0; k < 5; k++) put_vertex(VB + k * VSIZE, &v[L[k]]);
            ogpu_triangles3d(&B, VB, 5, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL | OGPU_LAY_FAN);
        } else {
            int isz = form == 3 ? 1 : form == 4 ? 2 : 4;
            for (k = 0; k < 6; k++) put_vertex(VB + k * VSIZE, &v[k]);
            for (k = 0; k < 6; k++) {
                if (isz == 1) arena[IB + k] = (ogpu_u8)k;
                else if (isz == 2) { arena[IB + k * 2] = 0; arena[IB + k * 2 + 1] = (ogpu_u8)k; }
                else wr32(IB + k * 4, (unsigned long)k);
            }
            ogpu_triangles3d(&B, VB, 6, VSIZE, IB, 6, isz, OGPU_TRI_NOTEX, 0, LAY_FULL | OGPU_LAY_STRIP);
        }
        run();
        CHECK(C.last_error == OGPU_OK, "primitive form %d: %d", form, C.last_error);
        for (i = 0; i < (long)W * H * 4; i++) h = ((h ^ arena[TGT + i]) * 16777619UL) & 0xFFFFFFFFUL;
        sums[form] = h;
    }
    CHECK(sums[0] == sums[1], "strip differs from list");
    CHECK(sums[3] == sums[1] && sums[4] == sums[1] && sums[5] == sums[1], "indexed strips differ");
    {
        /* the fan's own list */
        static const int L[9] = { 2, 0, 1, 2, 1, 3, 2, 3, 4 };
        long i;
        unsigned long h = 2166136261UL;
        clear_target(0);
        setup();
        r3d_defaults();
        ogpu_render3d(&B, R);
        for (k = 0; k < 9; k++) put_vertex(VB + k * VSIZE, &v[L[k]]);
        ogpu_triangles3d(&B, VB, 9, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        run();
        for (i = 0; i < (long)W * H * 4; i++) h = ((h ^ arena[TGT + i]) * 16777619UL) & 0xFFFFFFFFUL;
        CHECK(h == sums[2], "fan differs from its list");
    }
}

/* SDL's 9-word form: a textured, blended quad from a SURFACE slot. */
static void test_sdl_form(void) {
    long at = VB;
    int k;
    static const double q[4][4] = { { 0, 0, 0, 0 }, { 32, 0, 1, 0 }, { 0, 32, 0, 1 }, { 32, 32, 1, 1 } };
    make_texture(TEX, 32, 32);
    clear_target(0xFF000000UL);
    begin();
    ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, TEX, 32 * 4, 32, 32, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    for (k = 0; k < 4; k++) {
        wr32(at + k * 20, (unsigned long)fx16(q[k][0])); wr32(at + k * 20 + 4, (unsigned long)fx16(q[k][1]));
        wr32(at + k * 20 + 8, 0x80FFFFFFUL);
        wr32(at + k * 20 + 12, (unsigned long)fx16(q[k][2])); wr32(at + k * 20 + 16, (unsigned long)fx16(q[k][3]));
    }
    arena[IB] = 0; arena[IB + 1] = 1; arena[IB + 2] = 2; arena[IB + 3] = 1; arena[IB + 4] = 3; arena[IB + 5] = 2;
    ogpu_triangles(&B, VB, 4, 20, IB, 6, 1, 1, OGPU_TRI_BLEND_BLEND);
    run();
    CHECK(C.last_error == OGPU_OK, "SDL form: %d", C.last_error);
    {
        unsigned long t = rd32(TEX + (5 * 32 + 7) * 4), p = pix(7, 5);
        int ch, bad = 0;
        for (ch = 0; ch < 24; ch += 8) {
            int want = (int)(((t >> ch) & 255) * 128 / 255);
            if (abs((int)((p >> ch) & 255) - want) > 1) bad = 1;
        }
        CHECK(!bad, "SDL blend: %08lx from texel %08lx", p, t);
    }
}

/* Random streams of 3D commands: the core must stay inside the memory it is given
 * (AddressSanitizer catches any slip) and never hang. */
static void test_random_streams(void) {
    int n;
    for (n = 0; n < 3000; n++) {
        int k, words = (int)(rnd() % 200) + 1;
        for (k = 0; k < words; k++) {
            unsigned long w = rnd() | (rnd() << 24);
            if (k == 0 || rnd() % 6 == 0) {
                static const int ops[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x01, 0x02 };
                w = OGPU_HDR(ops[rnd() % 9], rnd() % 24 + 1);
            } else if (rnd() % 3 == 0) {
                w = rnd() % (unsigned long)ARENA;            /* addresses that land in the arena */
            } else if (rnd() % 3 == 0) {
                w = rnd() % 64;
            }
            wr32(0x200000L + k * 4, w & 0xFFFFFFFFUL);
        }
        ogpu_core_init(&C);
        C.map = map; C.user = 0; C.fence = 0; C.ext = 0;
        ogpu3d_init(&T, &C);
        ogpu3d_run(&T, arena + 0x200000L, words);
    }
}

/* ---- the golden scene ------------------------------------------------------------- */

#define GW 160
#define GH 120
#define GT 0x210000L
#define GZ 0x300000L

/* The golden scene's batch for one target format, in sbuf. */
static void golden_batch(int fmt) {
    int k, x;
    int f = fmt ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32, bpp = fmt ? 2 : 4;
    long n = 0;
    {
        begin();
        ogpu_surface(&B, 0, GT, (unsigned long)GW * bpp, GW, GH, f);
        ogpu_target(&B, 0);
        ogpu_fill(&B, 0, 0, GW, GH, fmt ? 0x18C3 : 0xFF202830UL);
        ogpu_depth(&B, OGPU_KIND_DEPTH, GZ, GW * 2, GW, GH, OGPU_FMT_Z16);
        ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, GW, GH, 0xFFFFFFFFUL);
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE | OGPU_R3D_FOG;
        R[7] = OGPU_FOG_LINEAR; R[8] = 0xFF202830UL; R[9] = fbits(0.9f); R[10] = fbits(0.15f);
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, TEX, 64 * 4, 64, 64, OGPU_FMT_ARGB32, 0, 1, 2, 2, 1, 1, 0, 1, 0);
        /* a floor into the distance and a row of slanted walls */
        for (k = 0; k < 8; k++) {
            double z0 = 1 + k * 1.5, z1 = z0 + 1.5;
            double xl = 80 - 120 / z0, xr = 80 + 120 / z0, yl = 60 + 60 / z0;
            double xl1 = 80 - 120 / z1, xr1 = 80 + 120 / z1, yl1 = 60 + 60 / z1;
            struct V v[4];
            memset(v, 0, sizeof v);
            v[0].x = xl; v[0].y = yl; v[0].w = 1 / z0; v[0].u = 0; v[0].v = k;
            v[1].x = xr; v[1].y = yl; v[1].w = 1 / z0; v[1].u = 3; v[1].v = k;
            v[2].x = xl1; v[2].y = yl1; v[2].w = 1 / z1; v[2].u = 0; v[2].v = k + 1;
            v[3].x = xr1; v[3].y = yl1; v[3].w = 1 / z1; v[3].u = 3; v[3].v = k + 1;
            for (x = 0; x < 4; x++) {
                v[x].z = 1 - v[x].w; v[x].c = x & 1 ? 0xFFFFE0C0UL : 0xFFC0E0FFUL; v[x].s = 0xFF000000UL;
                put_vertex(VB + (n + x) * VSIZE, &v[x]);
            }
            ogpu_triangles3d(&B, VB + n * VSIZE, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
            n += 4;
        }
        for (k = 0; k < 6; k++) {
            double z0 = 1.5 + k * 1.2, z1 = z0 + 0.9, xo = (k & 1) ? 1 : -1;
            struct V v[4];
            memset(v, 0, sizeof v);
            v[0].x = 80 + xo * 70 / z0; v[0].y = 60 - 50 / z0; v[0].w = 1 / z0; v[0].u = 0; v[0].v = 0;
            v[1].x = 80 + xo * 20 / z1; v[1].y = 60 - 50 / z1; v[1].w = 1 / z1; v[1].u = 1; v[1].v = 0;
            v[2].x = 80 + xo * 70 / z0; v[2].y = 60 + 60 / z0; v[2].w = 1 / z0; v[2].u = 0; v[2].v = 1;
            v[3].x = 80 + xo * 20 / z1; v[3].y = 60 + 60 / z1; v[3].w = 1 / z1; v[3].u = 1; v[3].v = 1;
            for (x = 0; x < 4; x++) {
                v[x].z = 1 - v[x].w; v[x].c = 0xFFFFFFFFUL; v[x].s = 0xFF000000UL;
                put_vertex(VB + (n + x) * VSIZE, &v[x]);
            }
            ogpu_triangles3d(&B, VB + n * VSIZE, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
            n += 4;
        }
        /* a see-through Gouraud triangle on top */
        r3d_defaults();
        R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_BLEND | OGPU_R3D_ZTEST;
        R[3] = 7 | (8UL << 16);
        ogpu_render3d(&B, R);
        {
            struct V v[3];
            memset(v, 0, sizeof v);
            v[0].x = 20; v[0].y = 10; v[0].c = 0xC0FF4040UL;
            v[1].x = 140; v[1].y = 30; v[1].c = 0x6040FF40UL;
            v[2].x = 70; v[2].y = 110; v[2].c = 0xA04040FFUL;
            for (x = 0; x < 3; x++) { v[x].z = 0.3; v[x].w = 1; put_vertex(VB + (n + x) * VSIZE, &v[x]); }
            ogpu_triangles3d(&B, VB + n * VSIZE, 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
        }
        CHECK(!B.overflow, "golden batch overflow");
    }
}

static unsigned long golden_scene(const char *ppm) {
    int k, x, y, fmt;
    unsigned long h = 2166136261UL;
    make_texture(TEX, 64, 64);
    for (fmt = 0; fmt < 2; fmt++) {
        int f = fmt ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32, bpp = fmt ? 2 : 4;
        golden_batch(fmt);
        run();
        CHECK(C.last_error == OGPU_OK, "golden scene format %d: %d", f, C.last_error);
        for (k = 0; k < GW * GH * bpp; k++) h = ((h ^ arena[GT + k]) * 16777619UL) & 0xFFFFFFFFUL;
        if (!fmt && ppm) {
            FILE *o = fopen(ppm, "wb");
            if (o) {
                fprintf(o, "P6\n%d %d\n255\n", GW, GH);
                for (y = 0; y < GH; y++)
                    for (x = 0; x < GW; x++) {
                        const ogpu_u8 *p = arena + GT + (long)(y * GW + x) * 4;
                        fputc(p[1], o); fputc(p[2], o); fputc(p[3], o);
                    }
                fclose(o);
            }
        }
    }
    return h;
}

#include <time.h>
#ifdef __amigaos__
/* libnix's clock() is not to be trusted here: DOS ticks (1/50 s) instead. */
#include <proto/dos.h>
static double now_s(void) {
    struct DateStamp d;
    long ticks;
    DateStamp(&d);
    ticks = (d.ds_Days % 1000) * 4320000L + d.ds_Minute * 3000L + d.ds_Tick;
    return (double)ticks / 50.0;
}
#else
static double now_s(void) { return (double)clock() / CLOCKS_PER_SEC; }
#endif

/* test_3d bench N: the golden scene N times, timed (for the 68k). */
static void bench(int n) {
    int fmt;
    make_texture(TEX, 64, 64);
    for (fmt = 0; fmt < 2; fmt++) {
        double t0, t1;
        int i;
        long us;
        golden_batch(fmt);
        run();
        t0 = now_s();
        for (i = 0; i < n; i++) run();
        t1 = now_s();
        us = (long)((t1 - t0) * 1000000.0 / n);
        printf("golden scene 160x120 %s: %ld.%02ld ms a frame, %ld frames a second; %ld triangles, %ld pixels a frame\n",
               fmt ? "RGB565" : "ARGB32", us / 1000, (us % 1000) / 10, us ? 1000000L / us : 0L, T.triangles, T.pixels);
    }
}

/* test_3d rates N: fill rates of single features, ns a pixel, on a 320x240 ARGB32 target. */
static void rates(int n) {
    static const char *name[] = { "flat colour", "Gouraud", "texture nearest", "texture bilinear",
                                  "texture, Gouraud, Z16", "texture, Z16, fog, perspective",
                                  "the golden scene's set (bilinear, Z, fog, perspective)", "alpha blended Gouraud" };
    int k;
    make_texture(TEX, 64, 64);
    for (k = 0; k < 8; k++) {
        double t0, t1;
        int i;
        long ns;
        begin();
        ogpu_surface(&B, 0, GT, 320 * 4, 320, 240, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_depth(&B, OGPU_KIND_DEPTH, GZ, 320 * 2, 320, 240, OGPU_FMT_Z16);
        ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, 320, 240, 0xFFFFFFFFUL);
        r3d_defaults();
        R[0] = k == 0 ? 0 : OGPU_R3D_GOURAUD;
        if (k >= 2 && k <= 6) R[0] |= OGPU_R3D_TEXTURE;
        if (k >= 4 && k <= 6) R[0] |= OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE;
        if (k >= 5 && k <= 6) { R[0] |= OGPU_R3D_FOG | OGPU_R3D_PERSPECTIVE; R[7] = 1; R[9] = fbits(0.9f); R[10] = fbits(0.1f); }
        if (k == 7) { R[0] |= OGPU_R3D_BLEND; R[3] = 7 | (8UL << 16); }
        ogpu_render3d(&B, R);
        ogpu_texenv(&B, 0, k == 2 || k == 3 ? OGPU_ENV_REPLACE : OGPU_ENV_MODULATE, 0);
        ogpu_texture(&B, 0, TEX, 64 * 4, 64, 64, OGPU_FMT_ARGB32, 0, 1, k == 2 || k == 4 ? 1 : 2, k == 2 || k == 4 ? 1 : 2,
                     1, 1, 0, 1, 0);
        quad(VB, 0, 0, 320, 240, 0, 0, 4, 3, 1.0, k >= 5 ? 0.5 : 1.0);
        {
            int j;
            for (j = 0; j < 4; j++) { wr32(VB + j * VSIZE + 8, 0x80FFC080UL + j * 0x1010); wr32(VB + j * VSIZE + 20, 0x40000000UL); }
        }
        ogpu_triangles3d(&B, VB, 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
        run();
        t0 = now_s();
        for (i = 0; i < n; i++) run();
        t1 = now_s();
        ns = (long)((t1 - t0) * 1e9 / n / (320.0 * 240.0));
        printf("%-56s %6ld ns a pixel (%ld pixels)\n", name[k], ns, T.pixels);
    }
}

int main(int argc, char **argv) {
    unsigned long g;
    if (argc > 2 && !strcmp(argv[1], "rates")) { rates(atoi(argv[2])); return 0; }
    if (argc > 2 && !strcmp(argv[1], "bench")) { bench(atoi(argv[2])); return 0; }
    if (argc > 2 && !strcmp(argv[1], "golden")) {   /* only the golden scene: for slow machines */
        g = golden_scene(0);
        {
            FILE *f = fopen(argv[2], "r");
            unsigned long want = 0;
            CHECK(f && fscanf(f, "%lx", &want) == 1, "golden file %s", argv[2]);
            if (f) fclose(f);
            CHECK(g == want, "golden scene %08lx, golden file says %08lx", g, want);
        }
        printf("opengpu 3D core: golden scene %08lx; %s\n", g, failures ? "FAILED" : "right");
        return failures ? 1 : 0;
    }
    /* everything twice: with the fast spans, then with the general ones only */
    for (slow_spans = 0; slow_spans < 2; slow_spans++) {
        test_coverage();
        test_shared_edges();
        test_gouraud_and_z();
        test_depth_order();
        test_texture_copy();
        test_perspective();
        test_mipmaps_and_bilinear();
        test_blend_factors();
        test_alpha_stencil_logic_mask();
        test_fog_and_env();
        test_primitives();
        test_sdl_form();
    }
    test_random_streams();
    slow_spans = 1;
    g = golden_scene(0);
    slow_spans = 0;
    CHECK(golden_scene(argc > 2 ? argv[2] : 0) == g, "the fast spans draw the golden scene differently from the general ones");
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        unsigned long want = 0;
        CHECK(f && fscanf(f, "%lx", &want) == 1, "golden file %s", argv[1]);
        if (f) fclose(f);
        CHECK(g == want, "golden scene %08lx, golden file says %08lx", g, want);
    }
    printf("opengpu 3D core: golden scene %08lx; %s\n", g, failures ? "FAILED" : "all tests passed");
    return failures ? 1 : 0;
}
