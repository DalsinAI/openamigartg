/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream v1.6 (PATH_FILL, PATH_STROKE, gradients, BLUR, the W3C blend
 * modes) against plain references written here in doubles from stream.h's
 * description and the specs it names:
 *   - coverage: each pixel's area inside the shape, from 256 scanlines a
 *     pixel, each exact along x (crossings and winding for fills; the union
 *     of the stroke's convex pieces, as canvas defines a stroke, for strokes);
 *   - gradients: t and the premultiplied stops' colour at each pixel centre;
 *   - blur: the three boxes in doubles, and a Gaussian of radius / 2;
 *   - blend modes: Compositing and Blending Level 1's formulas.
 * The tolerances are stated where each is checked. */
#define _DEFAULT_SOURCE  /* M_PI under -std=c99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

#define ARENA (4L << 20)
static ogpu_u8 arena[ARENA];
#define TGT   0x000000UL      /* the target, up to 128 x 128 ARGB32 */
#define MSK   0x010000UL      /* a mask */
#define SRCS  0x020000UL      /* a slot to paint with */
#define PATH  0x040000UL      /* path words */
#define GRAD  0x0C0000UL      /* a gradient */
#define DASH  0x0C8000UL      /* dashes */
#define WORK  0x100000UL      /* work memory, up to 3 MB */

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= (ogpu_u32)ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures, checks;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { failures++; printf("FAIL %s\n", what); }
}

static struct ogpu_core C;
static ogpu_u8 sbuf[4096];
static struct OGPUBatch B, P;

static void begin(void) {
    ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
    ogpu_batch_init(&P, arena + PATH, 0x20000);
}
static int run(void) {
    ogpu_core_init(&C);
    C.map = map; C.fence = 0; C.ext = 0; C.user = 0;
    ogpu_core_run(&C, sbuf, B.words);
    return C.last_error;
}

static const long ID[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
static long fx(double v) { return (long)floor(v * 256.0 + 0.5); }        /* 24.8 */
static long f16(double v) { return (long)floor(v * 65536.0 + 0.5); }     /* 16.16 */

/* ---- the reference shapes ---------------------------------------------------------- */

#define MAXE 20000
static double ex0[MAXE], ey0[MAXE], ex1[MAXE], ey1[MAXE];
static int ne;
static void ref_edge(double a, double b, double c, double d) {
    if (ne < MAXE) { ex0[ne] = a; ey0[ne] = b; ex1[ne] = c; ey1[ne] = d; ne++; }
}

/* the same path in doubles (curves cut into 400 lines), and into P (24.8) */
static double cx_, cy_, sx_, sy_;
static double mtx[6] = { 1, 0, 0, 0, 1, 0 };
static void tp(double x, double y, double *ox, double *oy) {
    *ox = mtx[0] * x + mtx[1] * y + mtx[2]; *oy = mtx[3] * x + mtx[4] * y + mtx[5];
}
static void r_line(double x, double y) {
    double a, b, c, d;
    tp(cx_, cy_, &a, &b); tp(x, y, &c, &d);
    ref_edge(a, b, c, d);
    cx_ = x; cy_ = y;
}
static void p_move(double x, double y) {
    if (cx_ != sx_ || cy_ != sy_) r_line(sx_, sy_);
    cx_ = sx_ = fx(x) / 256.0; cy_ = sy_ = fx(y) / 256.0;
    ogpu_path_move(&P, fx(x), fx(y));
}
static void p_line(double x, double y) { r_line(fx(x) / 256.0, fx(y) / 256.0); ogpu_path_line(&P, fx(x), fx(y)); }
static void p_cubic(double x1, double y1, double x2, double y2, double x, double y) {
    double ax = cx_, ay = cy_, bx = fx(x1) / 256.0, by = fx(y1) / 256.0, ccx = fx(x2) / 256.0, ccy = fx(y2) / 256.0;
    double dx = fx(x) / 256.0, dy = fx(y) / 256.0;
    int k;
    for (k = 1; k <= 400; k++) {
        double t = k / 400.0, u = 1 - t;
        r_line(u * u * u * ax + 3 * u * u * t * bx + 3 * u * t * t * ccx + t * t * t * dx,
               u * u * u * ay + 3 * u * u * t * by + 3 * u * t * t * ccy + t * t * t * dy);
    }
    ogpu_path_cubic(&P, fx(x1), fx(y1), fx(x2), fx(y2), fx(x), fx(y));
}
static void p_quad(double x1, double y1, double x, double y) {
    double ax = cx_, ay = cy_, bx = fx(x1) / 256.0, by = fx(y1) / 256.0, dx = fx(x) / 256.0, dy = fx(y) / 256.0;
    int k;
    for (k = 1; k <= 400; k++) {
        double t = k / 400.0, u = 1 - t;
        r_line(u * u * ax + 2 * u * t * bx + t * t * dx, u * u * ay + 2 * u * t * by + t * t * dy);
    }
    ogpu_path_quad(&P, fx(x1), fx(y1), fx(x), fx(y));
}
static void p_close(void) {
    if (cx_ != sx_ || cy_ != sy_) r_line(sx_, sy_);
    cx_ = sx_; cy_ = sy_;
    ogpu_path_close(&P);
}
static void p_start(void) { ne = 0; cx_ = cy_ = sx_ = sy_ = 0; }
static void p_end(void) { if (cx_ != sx_ || cy_ != sy_) r_line(sx_, sy_); }

/* convex pieces (a stroke's): polygons or discs */
#define MAXP 4000
struct piece { int n, disc; double x[8], y[8], r; };
static struct piece pcs[MAXP];
static int npc;

/* coverage, 0..1, of W x H pixels: by the edges (rule 0 non-zero, 1
 * even-odd) or, with pieces, by their union; S scanlines a pixel */
#define S 256
static double cov[128 * 128];
struct iv { double a, b; int d; };
static int ivcmp(const void *p, const void *q) {
    double a = ((const struct iv *)p)->a, b = ((const struct iv *)q)->a;
    return a < b ? -1 : a > b;
}
static struct iv ivs[MAXE + MAXP];
static void add_span(int W, int py, double a, double b) {
    int i;
    if (a < 0) a = 0;
    if (b > W) b = W;
    for (i = (int)floor(a); i < W && i < b; i++) {
        double l = a > i ? a : i, r = b < i + 1 ? b : i + 1;
        if (r > l) cov[py * W + i] += (r - l) / S;
    }
}
static void reference(int W, int H, int rule, int pieces) {
    int py, s, k, n;
    memset(cov, 0, sizeof cov);
    for (py = 0; py < H; py++)
        for (s = 0; s < S; s++) {
            double y = py + (s + 0.5) / S;
            n = 0;
            if (!pieces) {
                int wn = 0;
                double start = 0;
                for (k = 0; k < ne; k++) {
                    double a = ey0[k], b = ey1[k];
                    if ((a <= y && b > y) || (b <= y && a > y)) {
                        ivs[n].a = ex0[k] + (y - a) * (ex1[k] - ex0[k]) / (b - a);
                        ivs[n].d = b > a ? 1 : -1;
                        n++;
                    }
                }
                qsort(ivs, (size_t)n, sizeof ivs[0], ivcmp);
                for (k = 0; k < n; k++) {
                    int was = rule ? (wn & 1) : wn != 0, is;
                    wn += ivs[k].d;
                    is = rule ? (wn & 1) : wn != 0;
                    if (!was && is) start = ivs[k].a;
                    if (was && !is) add_span(W, py, start, ivs[k].a);
                }
            } else {
                double end = -1e30;
                for (k = 0; k < npc; k++) {
                    struct piece *p = &pcs[k];
                    double lo = 1e30, hi = -1e30;
                    if (p->disc) {
                        double dy = y - p->y[0];
                        if (fabs(dy) >= p->r) continue;
                        lo = p->x[0] - sqrt(p->r * p->r - dy * dy); hi = p->x[0] + sqrt(p->r * p->r - dy * dy);
                    } else {
                        int i;
                        for (i = 0; i < p->n; i++) {
                            int j = (i + 1) % p->n;
                            double a = p->y[i], b = p->y[j], x;
                            if ((a <= y && b > y) || (b <= y && a > y)) {
                                x = p->x[i] + (y - a) * (p->x[j] - p->x[i]) / (b - a);
                                if (x < lo) lo = x;
                                if (x > hi) hi = x;
                            }
                        }
                        if (lo > hi) continue;
                    }
                    ivs[n].a = lo; ivs[n].b = hi; n++;
                }
                qsort(ivs, (size_t)n, sizeof ivs[0], ivcmp);
                for (k = 0; k < n; k++) {               /* the union */
                    double a = ivs[k].a > end ? ivs[k].a : end;
                    if (ivs[k].b > a) { add_span(W, py, a, ivs[k].b); end = ivs[k].b; }
                }
            }
        }
}

/* compare the target's alpha (A8 or ARGB32) with the reference */
static int cmp_cov2(const char *what, int W, int H, int bpp, int tol, double mean_tol, int allow, int cap) {
    int i, maxd = 0, worst = 0, over = 0;
    double sum = 0;
    char buf[160];
    for (i = 0; i < W * H; i++) {
        int want = (int)floor(cov[i] * 255 + 0.5), got = arena[TGT + (unsigned long)i * bpp], d = abs(want - got);
        sum += d;
        if (d > tol) over++;
        if (d > maxd) { maxd = d; worst = i; }
    }
    if (getenv("DUMP") && maxd > tol) {       /* DUMP=1: the pixels round the worst, got/wanted */
        int x, y, wx = worst % W, wy = worst / W;
        for (y = wy - 4; y <= wy + 4; y++) {
            for (x = wx - 6; x <= wx + 6; x++)
                if (x >= 0 && y >= 0 && x < W && y < H) printf("%3d/%3d ", arena[TGT + (unsigned long)(y * W + x) * bpp], (int)floor(cov[y * W + x] * 255 + 0.5));
            printf("\n");
        }
    }
    snprintf(buf, sizeof buf, "%s: largest difference %d at (%d, %d), %d over %d (%d may be, up to %d), mean %.3f (tolerance %.2f)",
             what, maxd, worst % W, worst / W, over, tol, allow, cap, sum / (W * H), mean_tol);
    check(over <= allow && maxd <= cap && sum / (W * H) <= mean_tol, buf);
    return maxd;
}
static int cmp_cov(const char *what, int W, int H, int bpp, int tol, double mean_tol) {
    return cmp_cov2(what, W, H, bpp, tol, mean_tol, 0, tol);
}

/* a cleared W x H target of format f, and a new batch and path */
static void fresh(int W, int H, int f) {
    memset(arena + TGT, 0, (size_t)(W * H * (f == OGPU_FMT_A8 ? 1 : 4)));
    begin();
}
/* PATH_FILL of P into it */
static int do_fill(int W, int H, int f, unsigned long flags, unsigned long paint, const long *m, unsigned long work_bytes) {
    int bpp = f == OGPU_FMT_A8 ? 1 : 4;
    ogpu_surface(&B, 0, TGT, (unsigned long)(W * bpp), W, H, f);
    ogpu_target(&B, 0);
    ogpu_path_fill(&B, flags, paint, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, m, WORK, work_bytes);
    return run();
}

/* ---- fills ------------------------------------------------------------------------- */

static void star(double cx, double cy, double r) {
    int k;
    for (k = 0; k < 5; k++) {
        double a = -M_PI / 2 + k * 4 * M_PI / 5;
        if (!k) p_move(cx + r * cos(a), cy + r * sin(a)); else p_line(cx + r * cos(a), cy + r * sin(a));
    }
    p_close();
}

static void circle(double cx, double cy, double r) {
    double k = 0.5522847498 * r;
    p_move(cx + r, cy);
    p_cubic(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
    p_cubic(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
    p_cubic(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
    p_cubic(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
    p_close();
}

static void test_fills(void) {
    static ogpu_u8 keep[64 * 64];
    int W = 64, H = 64, r, i;
    /* a triangle with corners off the pixel grid: lines are exact (tolerance 3: the
     * reference's 256 scanlines, and the core's rounding to 24.8) */
    fresh(W, H, OGPU_FMT_A8);
    p_start(); p_move(3.3, 2.7); p_line(60.1, 20.45); p_line(17.9, 61.2); p_close(); p_end();
    r = do_fill(W, H, OGPU_FMT_A8, OGPU_PD_SRC, 0xFF000000UL, ID, 64 * 32 * 64);
    check(r == OGPU_OK, "triangle: no error");
    reference(W, H, 0, 0);
    cmp_cov("triangle, A8", W, H, 1, 3, 0.05);
    /* the same in one row of work memory at a time: the same bytes */
    memcpy(keep, arena + TGT, sizeof keep);
    fresh(W, H, OGPU_FMT_A8);
    p_start(); p_move(3.3, 2.7); p_line(60.1, 20.45); p_line(17.9, 61.2); p_close(); p_end();
    do_fill(W, H, OGPU_FMT_A8, OGPU_PD_SRC, 0xFF000000UL, ID, 64 * 32);
    check(!memcmp(keep, arena + TGT, sizeof keep), "triangle: one band or 64, the same bytes");
    fresh(W, H, OGPU_FMT_A8);
    p_start(); p_move(3.3, 2.7); p_line(60.1, 20.45); p_line(17.9, 61.2); p_close(); p_end();
    check(do_fill(W, H, OGPU_FMT_A8, OGPU_PD_SRC, 0xFF000000UL, ID, 64 * 32 - 1) == OGPU_ERR_UNSUPPORTED,
          "work memory under 32 bytes a pixel of width: UNSUPPORTED");

    /* a self-intersecting star: non-zero fills its middle, even-odd leaves it */
    for (i = 0; i < 2; i++) {
        fresh(W, H, OGPU_FMT_ARGB32);
        p_start(); star(32.2, 33.1, 30); p_end();
        do_fill(W, H, OGPU_FMT_ARGB32, OGPU_PD_OVER | (i ? OGPU_PATH_EVENODD : 0), 0xFF000000UL, ID, 1 << 20);
        reference(W, H, i, 0);
        /* where two edges cross inside a pixel, the core resolves the winding
         * per quarter of the pixel's height: up to a quarter of it off there */
        cmp_cov2(i ? "star, even-odd" : "star, non-zero", W, H, 4, 3, 0.05, 10, 64);
        check(arena[TGT + (33 * 64 + 32) * 4] == (i ? 0 : 255), i ? "star even-odd: the middle is empty" : "star non-zero: the middle is full");
    }

    /* curves: a circle of four cubics and a quadratic lens (curves cut within 1/16
     * pixel, so a pixel along one may differ by up to 24; the mean stays under 0.5) */
    fresh(W, H, OGPU_FMT_ARGB32);
    p_start(); circle(31.4, 30.8, 25.3); p_move(5, 60); p_quad(30, 40, 58, 61); p_close(); p_end();
    do_fill(W, H, OGPU_FMT_ARGB32, OGPU_PD_OVER, 0xFF000000UL, ID, 1 << 20);
    reference(W, H, 0, 0);
    cmp_cov("circle and lens", W, H, 4, 24, 0.5);

    /* the matrix: rotated 30 degrees, scaled 1.5 and moved, a rectangle and a hole */
    {
        double a = M_PI / 6, s = 1.5;
        long m[6];
        mtx[0] = s * cos(a); mtx[1] = -s * sin(a); mtx[2] = 30.25; mtx[3] = s * sin(a); mtx[4] = s * cos(a); mtx[5] = 4.5;
        m[0] = f16(mtx[0]); m[1] = f16(mtx[1]); m[2] = f16(mtx[2]); m[3] = f16(mtx[3]); m[4] = f16(mtx[4]); m[5] = f16(mtx[5]);
        mtx[0] = m[0] / 65536.0; mtx[1] = m[1] / 65536.0; mtx[2] = m[2] / 65536.0;
        mtx[3] = m[3] / 65536.0; mtx[4] = m[4] / 65536.0; mtx[5] = m[5] / 65536.0;
        fresh(W, H, OGPU_FMT_ARGB32);
        p_start();
        p_move(0, 0); p_line(30, 0); p_line(30, 25); p_line(0, 25); p_close();
        p_move(5, 5); p_line(5, 20); p_line(25, 20); p_line(25, 5); p_close();   /* the other way: a hole */
        p_end();
        do_fill(W, H, OGPU_FMT_ARGB32, OGPU_PD_OVER, 0xFF000000UL, m, 1 << 20);
        reference(W, H, 0, 0);
        cmp_cov("rotated, scaled rectangle with a hole", W, H, 4, 3, 0.05);
        mtx[0] = 1; mtx[1] = 0; mtx[2] = 0; mtx[3] = 0; mtx[4] = 1; mtx[5] = 0;
    }

    /* far outside on the left and above: what is left of the target still counts */
    fresh(W, H, OGPU_FMT_A8);
    p_start(); p_move(-30000, -20000); p_line(50.5, 10.25); p_line(20.75, 63.5); p_close(); p_end();
    do_fill(W, H, OGPU_FMT_A8, OGPU_PD_SRC, 0xFF000000UL, ID, 1 << 20);
    reference(W, H, 0, 0);
    cmp_cov("a triangle from far outside", W, H, 1, 3, 0.05);
}

/* ---- operators, masks, the clip, paint from a slot ---------------------------------- */

static unsigned m255(unsigned x, unsigned y) { unsigned t = x * y + 128; return (t + (t >> 8)) >> 8; }

static void test_ops(void) {
    int W = 48, H = 40, i, bad = 0;
    /* SRC acts on the whole drawable rectangle: inside the clip, outside the path,
     * pixels become 0; outside the clip they stay */
    begin();
    for (i = 0; i < W * H; i++) arena[TGT + i] = 200;
    p_start(); p_move(10.5, 10.5); p_line(30, 12); p_line(20, 30); p_close(); p_end();
    ogpu_surface(&B, 0, TGT, (unsigned long)W, W, H, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_clip(&B, 4, 4, 36, 30);
    ogpu_path_fill(&B, OGPU_PD_SRC, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
    check(run() == OGPU_OK, "SRC: no error");
    reference(W, H, 0, 0);
    for (i = 0; i < W * H; i++) {
        int x = i % W, y = i / W, in = x >= 4 && y >= 4 && x < 40 && y < 34;
        int want = in ? (int)floor(cov[i] * 255 + 0.5) : 200;
        if (abs(want - arena[TGT + i]) > 2) bad++;
    }
    check(!bad, "SRC: unbounded inside the clip, nothing outside it");

    /* a clip mask: a circle drawn into an A8 surface, intersected (IN) with a
     * square; then a slot's pattern painted through it, tiled */
    begin();
    memset(arena + MSK, 0, (size_t)(W * H));
    p_start(); circle(24, 20, 17.5); p_end();
    ogpu_surface(&B, 1, MSK, (unsigned long)W, W, H, OGPU_FMT_A8);
    ogpu_target(&B, 1);
    ogpu_path_fill(&B, OGPU_PD_SRC, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
    check(run() == OGPU_OK, "the clip mask's circle");
    {
        static double c1[48 * 40];
        static ogpu_u8 m1[48 * 40];
        reference(W, H, 0, 0);
        memcpy(c1, cov, sizeof c1);
        memcpy(m1, arena + MSK, sizeof m1);
        begin();
        p_start(); p_move(12.25, 6.5); p_line(46, 6.5); p_line(46, 38); p_line(12.25, 38); p_close(); p_end();
        ogpu_surface(&B, 1, MSK, (unsigned long)W, W, H, OGPU_FMT_A8);
        ogpu_target(&B, 1);
        ogpu_path_fill(&B, OGPU_PD_IN, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
        check(run() == OGPU_OK, "the clip mask's square, IN");
        reference(W, H, 0, 0);
        bad = 0;
        for (i = 0; i < W * H; i++) {
            int want = (int)m255(m1[i], (unsigned)floor(cov[i] * 255 + 0.5));
            if (abs(want - arena[MSK + i]) > 2) bad++;
        }
        check(!bad, "two clip paths intersected: the mask is their product");
        memcpy(m1, arena + MSK, sizeof m1);
        /* a 4 x 4 slot, tiled from (3, 2), through the mask, OVER a grey target */
        for (i = 0; i < 16; i++) {
            ogpu_u8 *q = arena + SRCS + i * 4;
            q[0] = 255; q[1] = (ogpu_u8)(i * 16); q[2] = (ogpu_u8)(255 - i * 16); q[3] = (ogpu_u8)(i & 1 ? 255 : 0);
        }
        begin();
        for (i = 0; i < W * H; i++) { ogpu_u8 *q = arena + TGT + i * 4; q[0] = 255; q[1] = q[2] = q[3] = 100; }
        p_start(); p_move(0, 0); p_line(W, 0); p_line(W, H); p_line(0, H); p_close(); p_end();
        ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, MSK, (unsigned long)W, W, H, OGPU_FMT_A8);
        ogpu_surface(&B, 2, SRCS, 16, 4, 4, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_path_fill(&B, OGPU_PD_OVER | OGPU_PATH_SLOT | OGPU_PATH_REPEAT | OGPU_PATH_MASK, 2, 3, 2, 1, 0, 0,
                       PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
        check(run() == OGPU_OK, "a slot's pattern through the mask");
        bad = 0;
        for (i = 0; i < W * H; i++) {
            int x = i % W, y = i / W, k;
            const ogpu_u8 *s = arena + SRCS + ((((y - 2) % 4 + 4) % 4) * 4 + ((x - 3) % 4 + 4) % 4) * 4;
            unsigned ma = m1[i], sa = m255(s[0], ma), ia = 255 - sa;
            for (k = 1; k < 4; k++) {
                unsigned want = m255(s[k], ma) + m255(100, ia);
                if (want > 255) want = 255;
                if (arena[TGT + i * 4 + k] != want) bad++;
            }
        }
        check(!bad, "a slot's pattern through the mask: exact");
        (void)c1;
    }
}

/* ---- patterns: a picture under a matrix ------------------------------------------- */

static void test_patterns(void) {
    static const char *en[4] = { "none", "pad", "repeat", "reflect" };
    int W = 48, H = 40, e, bil, i, k, bad, maxd;
    double a = 0.4, inv[6], det;
    long m[6];
    char buf[160];
    struct OGPUBatch G;
    /* an 8 x 6 premultiplied picture, the part 1, 1, 6 x 4 of it used */
    for (i = 0; i < 48; i++) {
        ogpu_u8 *q = arena + SRCS + i * 4;
        q[0] = (ogpu_u8)(i % 3 ? 255 : 128); q[1] = (ogpu_u8)((i * 37) % (q[0] + 1)); q[2] = (ogpu_u8)((i * 91) % (q[0] + 1));
        q[3] = (ogpu_u8)((i * 13) % (q[0] + 1));
    }
    m[0] = f16(3.1 * cos(a)); m[1] = f16(-2.3 * sin(a)); m[2] = f16(17.3); m[3] = f16(3.1 * sin(a)); m[4] = f16(2.3 * cos(a)); m[5] = f16(6.6);
    det = (m[0] / 65536.0) * (m[4] / 65536.0) - (m[1] / 65536.0) * (m[3] / 65536.0);
    inv[0] = m[4] / 65536.0 / det; inv[1] = -m[1] / 65536.0 / det; inv[3] = -m[3] / 65536.0 / det; inv[4] = m[0] / 65536.0 / det;
    inv[2] = -(inv[0] * m[2] + inv[1] * m[5]) / 65536.0; inv[5] = -(inv[3] * m[2] + inv[4] * m[5]) / 65536.0;
    for (e = 0; e < 4; e++)
        for (bil = 0; bil < 2; bil++) {
            ogpu_batch_init(&G, arena + GRAD, 0x1000);
            ogpu_image_paint(&G, 2, (unsigned long)e | (bil ? OGPU_PATTERN_BILINEAR : 0), 1, 1, 6, 4, m, 200);
            for (i = 0; i < W * H; i++) { ogpu_u8 *q = arena + TGT + i * 4; q[0] = 255; q[1] = 90; q[2] = 60; q[3] = 30; }
            begin();
            ogpu_path_move(&P, 0, 0); ogpu_path_line(&P, fx(W), 0); ogpu_path_line(&P, fx(W), fx(H)); ogpu_path_line(&P, 0, fx(H));
            ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
            ogpu_surface(&B, 2, SRCS, 32, 8, 6, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0);
            ogpu_path_fill(&B, OGPU_PD_OVER | OGPU_PATH_PATTERN, GRAD, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
            check(run() == OGPU_OK, "pattern: no error");
            bad = 0; maxd = 0;
            for (i = 0; i < W * H; i++) {
                double px = i % W + 0.5, py = i / W + 0.5, u = inv[0] * px + inv[1] * py + inv[2], v = inv[3] * px + inv[4] * py + inv[5];
                double s[4] = { 0, 0, 0, 0 }, d[4] = { 255, 90, 60, 30 };
                int corner, worst = 0;
                for (corner = 0; corner < (bil ? 4 : 1); corner++) {
                    int x0 = bil ? (int)floor(u - 0.5) + (corner & 1) : (int)floor(u), y0 = bil ? (int)floor(v - 0.5) + (corner >> 1) : (int)floor(v);
                    double wgt = 1;
                    int ok = 1;
                    if (bil) {
                        double fu = u - 0.5 - floor(u - 0.5), fv = v - 0.5 - floor(v - 0.5);
                        wgt = (corner & 1 ? fu : 1 - fu) * (corner >> 1 ? fv : 1 - fv);
                    }
                    if (e == OGPU_PATTERN_PAD) { x0 = x0 < 0 ? 0 : x0 > 5 ? 5 : x0; y0 = y0 < 0 ? 0 : y0 > 3 ? 3 : y0; }
                    else if (e == OGPU_PATTERN_REPEAT) { x0 = ((x0 % 6) + 6) % 6; y0 = ((y0 % 4) + 4) % 4; }
                    else if (e == OGPU_PATTERN_REFLECT) {
                        x0 = ((x0 % 12) + 12) % 12; y0 = ((y0 % 8) + 8) % 8;
                        if (x0 >= 6) x0 = 11 - x0;
                        if (y0 >= 4) y0 = 7 - y0;
                    } else ok = x0 >= 0 && y0 >= 0 && x0 < 6 && y0 < 4;
                    if (ok) for (k = 0; k < 4; k++) s[k] += wgt * arena[SRCS + ((1 + y0) * 8 + 1 + x0) * 4 + k];
                }
                for (k = 0; k < 4; k++) s[k] *= 200 / 255.0;
                for (k = 0; k < 4; k++) {
                    int want = (int)floor(s[k] + d[k] * (1 - s[0] / 255) + 0.5), dd = abs(want - arena[TGT + i * 4 + k]);
                    if (dd > worst) worst = dd;
                }
                if (worst > 3) bad++;
                if (worst > maxd) maxd = worst;
            }
            /* within 3 (bilinear weights are 8 bits); nearest may pick the
             * neighbour where a pixel's centre is within 1/65536 of an edge */
            snprintf(buf, sizeof buf, "pattern under a matrix, %s, %s: %d pixels over 3 (largest %d)", en[e], bil ? "bilinear" : "nearest", bad, maxd);
            check(bad <= (bil ? 0 : 4), buf);
        }
}

/* ---- strokes ----------------------------------------------------------------------- */

/* the reference's pieces for a polyline (closed or not), as canvas defines a
 * stroke: a rectangle for each segment, joins on the outside, caps */
static void piece_poly(int n, const double *x, const double *y) {
    int i;
    if (npc >= MAXP) return;
    pcs[npc].n = n; pcs[npc].disc = 0;
    for (i = 0; i < n; i++) { pcs[npc].x[i] = x[i]; pcs[npc].y[i] = y[i]; }
    npc++;
}
static void piece_disc(double x, double y, double r) {
    if (npc >= MAXP) return;
    pcs[npc].disc = 1; pcs[npc].x[0] = x; pcs[npc].y[0] = y; pcs[npc].r = r;
    npc++;
}
static void ref_stroke(int n, const double *px, const double *py, int closed, double hw, int cap, int join, double limit) {
    int i, segs = closed ? n : n - 1;
    for (i = 0; i < segs; i++) {
        int j = (i + 1) % n;
        double dx = px[j] - px[i], dy = py[j] - py[i], l = sqrt(dx * dx + dy * dy), nx = -dy / l * hw, ny = dx / l * hw;
        double qx[4], qy[4];
        qx[0] = px[i] + nx; qy[0] = py[i] + ny; qx[1] = px[j] + nx; qy[1] = py[j] + ny;
        qx[2] = px[j] - nx; qy[2] = py[j] - ny; qx[3] = px[i] - nx; qy[3] = py[i] - ny;
        piece_poly(4, qx, qy);
    }
    for (i = closed ? 0 : 1; i < (closed ? n : n - 1); i++) {
        int a = (i + n - 1) % n, b = (i + 1) % n;
        double ux = px[i] - px[a], uy = py[i] - py[a], vx = px[b] - px[i], vy = py[b] - py[i];
        double lu = sqrt(ux * ux + uy * uy), lv = sqrt(vx * vx + vy * vy), cr, co, s;
        double tx[4], ty[4];
        ux /= lu; uy /= lu; vx /= lv; vy /= lv;
        cr = ux * vy - uy * vx; co = ux * vx + uy * vy;
        if (join == OGPU_JOIN_ROUND) { piece_disc(px[i], py[i], hw); continue; }
        s = cr > 0 ? -1 : 1;
        tx[0] = px[i]; ty[0] = py[i];
        tx[1] = px[i] - s * uy * hw; ty[1] = py[i] + s * ux * hw;
        if (join == OGPU_JOIN_MITER && 1 / sqrt((1 + co) / 2) <= limit) {
            double mx = -uy + -vy, my = ux + vx, k = hw / (1 + co);
            tx[2] = px[i] + s * mx * k; ty[2] = py[i] + s * my * k;
            tx[3] = px[i] - s * vy * hw; ty[3] = py[i] + s * vx * hw;
            piece_poly(4, tx, ty);
        } else {
            tx[2] = px[i] - s * vy * hw; ty[2] = py[i] + s * vx * hw;
            piece_poly(3, tx, ty);
        }
    }
    if (!closed)
        for (i = 0; i < 2; i++) {
            int e = i ? n - 1 : 0, o = i ? n - 2 : 1;
            double ux = px[e] - px[o], uy = py[e] - py[o], l = sqrt(ux * ux + uy * uy), qx[4], qy[4];
            ux /= l; uy /= l;
            if (cap == OGPU_CAP_ROUND) piece_disc(px[e], py[e], hw);
            else if (cap == OGPU_CAP_SQUARE) {
                qx[0] = px[e] - uy * hw; qy[0] = py[e] + ux * hw;
                qx[1] = qx[0] + ux * hw; qy[1] = qy[0] + uy * hw;
                qx[3] = px[e] + uy * hw; qy[3] = py[e] - ux * hw;
                qx[2] = qx[3] + ux * hw; qy[2] = qy[3] + uy * hw;
                piece_poly(4, qx, qy);
            }
        }
}

static int stroke(int W, int H, int n, const double *x, const double *y, int closed, double width, int style, double limit,
                  int ndash, const double *dash, double off) {
    int i;
    unsigned long dn = (unsigned long)ndash;
    memset(arena + TGT, 0, (size_t)(W * H));
    begin();
    for (i = 0; i < n; i++) {
        if (!i) ogpu_path_move(&P, fx(x[i]), fx(y[i])); else ogpu_path_line(&P, fx(x[i]), fx(y[i]));
    }
    if (closed) ogpu_path_close(&P);
    for (i = 0; i < ndash; i++) {
        ogpu_u8 *q = arena + DASH + i * 4;
        long v = f16(dash[i]);
        q[0] = (ogpu_u8)(v >> 24); q[1] = (ogpu_u8)(v >> 16); q[2] = (ogpu_u8)(v >> 8); q[3] = (ogpu_u8)v;
    }
    ogpu_surface(&B, 0, TGT, (unsigned long)W, W, H, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_path_stroke(&B, OGPU_PD_OVER, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20,
                     f16(width), (unsigned long)style, f16(limit), DASH, dn, f16(off));
    return run();
}

/* the dashes of a polyline, as the reference's pieces (each dash its own
 * open polyline, with caps; joins inside a dash) */
static void ref_dashes(int n, const double *x, const double *y, double hw, int cap, int join, double limit,
                       int ndash, const double *dash, double off) {
    double total = 0, rem, qx[64], qy[64];
    int i, di = 0, on = 1, nq = 0, k;
    for (i = 0; i < ndash; i++) total += dash[i];
    if (ndash & 1) total *= 2;
    off = fmod(off, total);
    if (off < 0) off += total;
    while (off >= dash[di]) { off -= dash[di]; di = (di + 1) % ndash; on ^= 1; }
    rem = dash[di] - off;
    if (on) { qx[0] = x[0]; qy[0] = y[0]; nq = 1; }
    for (k = 0; k + 1 < n; k++) {
        double dx = x[k + 1] - x[k], dy = y[k + 1] - y[k], len = sqrt(dx * dx + dy * dy), pos = 0;
        while (pos < len) {
            double step = rem < len - pos ? rem : len - pos;
            pos += step; rem -= step;
            if (on) { qx[nq] = x[k] + dx * pos / len; qy[nq] = y[k] + dy * pos / len; nq++; }
            if (rem <= 1e-9) {
                if (on && nq > 1) ref_stroke(nq, qx, qy, 0, hw, cap, join, limit);
                on ^= 1; di = (di + 1) % ndash; rem = dash[di];
                nq = 0;
                if (on) { qx[0] = x[k] + dx * pos / len; qy[0] = y[k] + dy * pos / len; nq = 1; }
            }
        }
    }
    if (on && nq > 1) ref_stroke(nq, qx, qy, 0, hw, cap, join, limit);
}

static void test_strokes(void) {
    static const double zx[5] = { 10.3, 40.6, 20.2, 70.8, 86.1 }, zy[5] = { 12.4, 20.7, 50.3, 66.6, 20.2 };
    static const double rx[4] = { 20.5, 75.25, 75.25, 20.5 }, ry[4] = { 18.5, 18.5, 70.75, 70.75 };
    static const char *capn[3] = { "butt", "round", "square" }, *joinn[3] = { "miter", "round", "bevel" };
    int W = 96, H = 88, c, j;
    char what[96];
    for (c = 0; c < 3; c++)
        for (j = 0; j < 3; j++) {
            int round = c == OGPU_CAP_ROUND || j == 1;
            /* a zigzag: polygons within 6 (a join's corner is worked out from
             * normals rounded to 1/256 pixel); arcs cut within 1/16 pixel (24) */
            npc = 0;
            check(stroke(W, H, 5, zx, zy, 0, 7.5, c | (j << 4), 4.0, 0, 0, 0) == OGPU_OK, "stroke: no error");
            ref_stroke(5, zx, zy, 0, 3.75, c, j << 4, 4.0);
            reference(W, H, 0, 1);
            snprintf(what, sizeof what, "zigzag stroke, %s caps, %s joins", capn[c], joinn[j]);
            cmp_cov(what, W, H, 1, round ? 24 : 6, round ? 0.2 : 0.05);
        }
    /* a closed rectangle with miter, round and bevel joins all round */
    for (j = 0; j < 3; j++) {
        npc = 0;
        stroke(W, H, 4, rx, ry, 1, 9.0, j << 4, 10.0, 0, 0, 0);
        ref_stroke(4, rx, ry, 1, 4.5, 0, j << 4, 10.0);
        reference(W, H, 0, 1);
        snprintf(what, sizeof what, "closed rectangle, %s joins", joinn[j]);
        cmp_cov(what, W, H, 1, j == 1 ? 24 : 6, j == 1 ? 0.2 : 0.05);
    }
    /* the miter limit: a sharp corner bevelled at limit 2, mitred at 10 */
    {
        static const double sx[3] = { 10, 80, 12 }, sy[3] = { 20, 40, 60 };
        for (j = 0; j < 2; j++) {
            npc = 0;
            stroke(W, H, 3, sx, sy, 0, 6.0, OGPU_JOIN_MITER, j ? 10.0 : 2.0, 0, 0, 0);
            ref_stroke(3, sx, sy, 0, 3.0, 0, OGPU_JOIN_MITER, j ? 10.0 : 2.0);
            reference(W, H, 0, 1);
            cmp_cov(j ? "sharp corner, miter limit 10 (mitred)" : "sharp corner, miter limit 2 (bevelled)", W, H, 1, 6, 0.05);
        }
    }
    /* dashes: an odd count used twice, an offset, on a polyline, butt caps; then
     * round caps with gaps wider than the line (caps that overlap the next
     * dash are pieces drawn over each other: along their edges up to a
     * quarter of a pixel too dark) */
    {
        static const double d1[3] = { 9.0, 4.5, 2.25 }, d2[2] = { 12.0, 6.0 };
        npc = 0;
        stroke(W, H, 5, zx, zy, 0, 4.0, OGPU_CAP_BUTT | OGPU_JOIN_BEVEL, 4.0, 3, d1, 5.5);
        ref_dashes(5, zx, zy, 2.0, OGPU_CAP_BUTT, OGPU_JOIN_BEVEL, 4.0, 3, d1, 5.5);
        reference(W, H, 0, 1);
        /* a dash too short for its inner join to meet goes in to the corner
         * and out again: a pixel or two there up to a quarter off */
        cmp_cov2("dashes 9, 4.5, 2.25 from 5.5, butt caps", W, H, 1, 6, 0.05, 3, 64);
        npc = 0;
        stroke(W, H, 5, zx, zy, 0, 5.0, OGPU_CAP_ROUND | OGPU_JOIN_ROUND, 4.0, 2, d2, -3.0);
        ref_dashes(5, zx, zy, 2.5, OGPU_CAP_ROUND, OGPU_JOIN_ROUND, 4.0, 2, d2, -3.0);
        reference(W, H, 0, 1);
        cmp_cov("dashes 12, 6 from -3, round caps", W, H, 1, 24, 0.2);
    }
    /* a stroked circle of cubics against its ring (inner and outer circles) */
    {
        memset(arena + TGT, 0, (size_t)(W * H));
        begin();
        p_start(); circle(48, 44, 30); p_end();
        ogpu_surface(&B, 0, TGT, (unsigned long)W, W, H, OGPU_FMT_A8);
        ogpu_target(&B, 0);
        ogpu_path_stroke(&B, OGPU_PD_OVER, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20,
                         f16(6.0), OGPU_JOIN_MITER, f16(10.0), 0, 0, 0);
        check(run() == OGPU_OK, "stroked circle: no error");
        p_start(); circle(48, 44, 33); circle(48, 44, 27);
        /* the inner circle the other way round: a ring */
        {
            int k, m = ne / 2;
            for (k = m; k < ne; k++) { double t = ex0[k]; ex0[k] = ex1[k]; ex1[k] = t; t = ey0[k]; ey0[k] = ey1[k]; ey1[k] = t; }
        }
        reference(W, H, 0, 0);
        cmp_cov("stroked circle (width 6) against its ring", W, H, 1, 24, 0.5);
    }
}

/* ---- gradients ---------------------------------------------------------------------- */

struct stop { double o; unsigned long c; };

static double spread(double t, int s) {
    if (s == OGPU_SPREAD_REPEAT) return t - floor(t);
    if (s == OGPU_SPREAD_REFLECT) { t = fmod(fabs(t), 2); return t > 1 ? 2 - t : t; }
    return t < 0 ? 0 : t > 1 ? 1 : t;
}
static double chan(unsigned long c, int k) { return (double)((c >> (24 - 8 * k)) & 255); }
static void ref_colour(const struct stop *st, int n, double t, double *o) {
    int k, i;
    if (t < st[0].o) { for (k = 0; k < 4; k++) o[k] = chan(st[0].c, k); return; }
    for (i = 0; i + 1 < n && st[i + 1].o <= t; i++) ;
    if (i + 1 >= n) { for (k = 0; k < 4; k++) o[k] = chan(st[n - 1].c, k); return; }
    for (k = 0; k < 4; k++) {
        double f = (t - st[i].o) / (st[i + 1].o - st[i].o);
        o[k] = chan(st[i].c, k) * (1 - f) + chan(st[i + 1].c, k) * f;
    }
}

/* t at a point, or -1e9 for nothing (RADIAL) */
static double ref_t(int kind, const double *g, double x, double y) {
    if (kind == OGPU_GRAD_LINEAR) {
        double dx = g[2] - g[0], dy = g[3] - g[1];
        return ((x - g[0]) * dx + (y - g[1]) * dy) / (dx * dx + dy * dy);
    }
    if (kind == OGPU_GRAD_CONIC) {
        double a = atan2(y - g[1], x - g[0]) / (2 * M_PI) - g[2];
        return a - floor(a);
    } else {
        double cdx = g[3] - g[0], cdy = g[4] - g[1], dr = g[5] - g[2], pdx = x - g[0], pdy = y - g[1];
        double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + g[2] * dr, c = pdx * pdx + pdy * pdy - g[2] * g[2];
        double t1, t2, d;
        if (fabs(a) < 1e-12) { if (b == 0) return -1e9; t1 = t2 = c / (2 * b); }
        else {
            d = b * b - a * c;
            if (d < 0) return -1e9;
            t1 = (b + sqrt(d)) / a; t2 = (b - sqrt(d)) / a;
            if (t2 > t1) { d = t1; t1 = t2; t2 = d; }
        }
        if (g[2] + t1 * dr >= 0) return t1;
        if (g[2] + t2 * dr >= 0) return t2;
        return -1e9;
    }
}

static void test_gradient(const char *what, int kind, int spr, const double *g, const double *m, const struct stop *st, int n, int tol) {
    int W = 64, H = 48, i, k, maxd = 0;
    long lm[6], lg[6];
    double inv[6], det;
    char buf[160];
    struct OGPUBatch G;
    for (i = 0; i < 6; i++) { lm[i] = f16(m[i]); lg[i] = f16(g[i]); }
    det = (lm[0] / 65536.0) * (lm[4] / 65536.0) - (lm[1] / 65536.0) * (lm[3] / 65536.0);
    inv[0] = lm[4] / 65536.0 / det; inv[1] = -lm[1] / 65536.0 / det; inv[3] = -lm[3] / 65536.0 / det; inv[4] = lm[0] / 65536.0 / det;
    inv[2] = -(inv[0] * lm[2] + inv[1] * lm[5]) / 65536.0; inv[5] = -(inv[3] * lm[2] + inv[4] * lm[5]) / 65536.0;
    ogpu_batch_init(&G, arena + GRAD, 0x1000);
    ogpu_gradient(&G, kind, spr, n, lm, lg);
    for (i = 0; i < n; i++) ogpu_gradient_stop(&G, f16(st[i].o), st[i].c);
    memset(arena + TGT, 0, (size_t)(W * H * 4));
    begin();
    ogpu_path_move(&P, 0, 0); ogpu_path_line(&P, fx(W), 0); ogpu_path_line(&P, fx(W), fx(H)); ogpu_path_line(&P, 0, fx(H));
    ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_path_fill(&B, OGPU_PD_SRC | OGPU_PATH_GRADIENT, GRAD, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
    check(run() == OGPU_OK, "gradient: no error");
    for (i = 0; i < W * H; i++) {
        double px = i % W + 0.5, py = i / W + 0.5, gx = inv[0] * px + inv[1] * py + inv[2], gy = inv[3] * px + inv[4] * py + inv[5];
        double t = ref_t(kind, g, gx, gy), o[4];
        if (t < -1e8) o[0] = o[1] = o[2] = o[3] = 0;
        else ref_colour(st, n, spread(t, spr), o);
        for (k = 0; k < 4; k++) {
            int d = abs((int)floor(o[k] + 0.5) - arena[TGT + i * 4 + k]);
            /* a pixel right on a sharp stop may go either way */
            if (d > maxd) maxd = d;
        }
    }
    snprintf(buf, sizeof buf, "%s: largest difference %d (tolerance %d)", what, maxd, tol);
    check(maxd <= tol, buf);
}

static void test_gradients(void) {
    static const struct stop s3[3] = { { 0.0, 0xFFFF0000UL }, { 0.4, 0x80008000UL }, { 1.0, 0xFF0000FFUL } };
    static const struct stop s4[4] = { { 0.1, 0xFF203040UL }, { 0.3, 0x40102030UL }, { 0.6, 0xC0C0A000UL }, { 0.9, 0x00000000UL } };
    static const double id[6] = { 1, 0, 0, 0, 1, 0 };
    double rot[6], lin[6] = { 5, 7, 55, 30, 0, 0 }, rad[6] = { 20, 20, 3, 32, 24, 22 }, foc[6] = { 32, 24, 0, 32, 24, 16 };
    double con[6] = { 31.5, 23.5, 0.125, 0, 0, 0 }, a = 0.3;
    static const char *sn[3] = { "pad", "repeat", "reflect" };
    char what[96];
    int s;
    rot[0] = cos(a) * 1.25; rot[1] = -sin(a) * 0.8; rot[2] = 6.5; rot[3] = sin(a) * 1.25; rot[4] = cos(a) * 0.8; rot[5] = -3.25;
    /* within 2 of the real colours (the stops mixed at 16 bits) */
    for (s = 0; s < 3; s++) {
        snprintf(what, sizeof what, "linear gradient, three stops, %s", sn[s]);
        test_gradient(what, OGPU_GRAD_LINEAR, s, lin, id, s3, 3, 2);
        snprintf(what, sizeof what, "radial gradient, two circles, four stops, %s", sn[s]);
        test_gradient(what, OGPU_GRAD_RADIAL, s, rad, id, s4, 4, 2);
    }
    test_gradient("linear gradient, rotated and scaled", OGPU_GRAD_LINEAR, OGPU_SPREAD_REFLECT, lin, rot, s3, 3, 2);
    test_gradient("radial gradient, one circle (CSS), transformed", OGPU_GRAD_RADIAL, OGPU_SPREAD_PAD, foc, rot, s3, 3, 2);
    {
        double out[6] = { 10, 24, 4, 40, 24, 2 };     /* circles apart: a cone, nothing outside it */
        test_gradient("radial gradient, a cone between circles apart", OGPU_GRAD_RADIAL, OGPU_SPREAD_PAD, out, id, s3, 3, 2);
    }
    /* conic: the angle within 0.0003 of a turn (3) */
    test_gradient("conic gradient", OGPU_GRAD_CONIC, OGPU_SPREAD_PAD, con, id, s4, 4, 3);
    test_gradient("conic gradient, transformed", OGPU_GRAD_CONIC, OGPU_SPREAD_PAD, con, rot, s3, 3, 3);
    {
        static const struct stop hard[4] = { { 0, 0xFF000000UL }, { 0.5, 0xFF000000UL }, { 0.5, 0xFFFFFFFFUL }, { 1, 0xFFFFFFFFUL } };
        double h[6] = { 0.25, 0, 63.75, 0, 0, 0 };
        test_gradient("two stops at one offset: a sharp change", OGPU_GRAD_LINEAR, OGPU_SPREAD_PAD, h, id, hard, 4, 2);
    }
}

/* ---- blur --------------------------------------------------------------------------- */

static void ref_box(double *v, int n, int lo, int hi) {
    static double t[256];
    int i, k;
    for (i = 0; i < n; i++) {
        double s = 0;
        for (k = i - lo; k <= i + hi; k++) if (k >= 0 && k < n) s += v[k];
        t[i] = s / (lo + hi + 1);
    }
    memcpy(v, t, sizeof(double) * (size_t)n);
}
static void ref_box3(double *v, int n, int d) {
    if (d < 2) return;
    if (d & 1) { ref_box(v, n, d / 2, d / 2); ref_box(v, n, d / 2, d / 2); ref_box(v, n, d / 2, d / 2); }
    else { ref_box(v, n, d / 2, d / 2 - 1); ref_box(v, n, d / 2 - 1, d / 2); ref_box(v, n, d / 2, d / 2); }
}

static void test_blur(void) {
    static const double radii[4] = { 2.0, 5.0, 10.5, 24.0 };
    static double img[96 * 96], col[96];
    int W = 96, H = 96, r, i, j, maxd, bad;
    char buf[160];
    for (r = 0; r < 4; r++) {
        double sigma = radii[r] / 2;
        int d = (int)floor(sigma * 3 * sqrt(2 * M_PI) / 4 + 0.5), rad16 = (int)(radii[r] * 16);
        /* a square of coverage blurred in A8 */
        memset(arena + TGT, 0, (size_t)(W * H));
        for (i = 0; i < W * H; i++) {
            int x = i % W, y = i / W;
            img[i] = (x >= 30 && x < 60 && y >= 35 && y < 50) ? 255 : 0;
            arena[TGT + i] = (ogpu_u8)img[i];
        }
        begin();
        ogpu_surface(&B, 0, TGT, (unsigned long)W, W, H, OGPU_FMT_A8);
        ogpu_target(&B, 0);
        ogpu_blur(&B, 0, 0, W, H, rad16, rad16, WORK, 2 * 96);
        check(run() == OGPU_OK, "blur: no error");
        /* the three boxes in doubles: within 3 (each of six passes rounds) */
        for (j = 0; j < H; j++) ref_box3(img + j * W, W, d);
        for (i = 0; i < W; i++) {
            for (j = 0; j < H; j++) col[j] = img[j * W + i];
            ref_box3(col, H, d);
            for (j = 0; j < H; j++) img[j * W + i] = col[j];
        }
        maxd = 0;
        for (i = 0; i < W * H; i++) { int dd = abs((int)floor(img[i] + 0.5) - arena[TGT + i]); if (dd > maxd) maxd = dd; }
        snprintf(buf, sizeof buf, "blur radius %.1f (box %d): largest difference from three boxes %d (tolerance 3)", radii[r], d, maxd);
        check(maxd <= 3, buf);
        /* and close to the Gaussian of radius / 2: within 4% of full scale */
        maxd = 0;
        for (i = 0; i < W * H; i++) {
            double x = i % W + 0.5, y = i / W + 0.5, k = 1 / (sqrt(2.0) * sigma);
            double gx = 0.5 * (erf((60 - x) * k) - erf((30 - x) * k)), gy = 0.5 * (erf((50 - y) * k) - erf((35 - y) * k));
            int dd = abs((int)floor(255 * gx * gy + 0.5) - arena[TGT + i]);
            if (dd > maxd) maxd = dd;
        }
        snprintf(buf, sizeof buf, "blur radius %.1f: largest difference from the Gaussian %d (tolerance 10)", radii[r], maxd);
        check(maxd <= 10, buf);
    }
    /* ARGB32: every channel alike, the clip respected, too little work refused */
    memset(arena + TGT, 0, (size_t)(W * H * 4));
    for (i = 40; i < 50; i++) for (j = 0; j < 4; j++) arena[TGT + (40 * W + i) * 4 + j] = (ogpu_u8)(j ? 128 : 255);
    begin();
    ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_clip(&B, 0, 0, W, 60);
    ogpu_blur(&B, 0, 0, W, H, 6 * 16, 6 * 16, WORK, 2 * 4 * 96);
    check(run() == OGPU_OK, "ARGB32 blur: no error");
    bad = 0;
    for (i = 0; i < W * H; i++) {
        const ogpu_u8 *q = arena + TGT + i * 4;
        if (i / W >= 60 && (q[0] | q[1])) bad++;
        if (q[1] != q[2] || q[2] != q[3] || q[1] > q[0]) bad++;
    }
    check(!bad && arena[TGT + (40 * W + 45) * 4] > 0 && arena[TGT + (44 * W + 45) * 4] > 0, "ARGB32 blur: premultiplied stays so, the clip is kept");
    begin();
    ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_blur(&B, 0, 0, W, H, 32, 32, WORK, 2 * 4 * 96 - 1);
    check(run() == OGPU_ERR_UNSUPPORTED, "blur with too little work memory: UNSUPPORTED");
}

/* ---- blend modes -------------------------------------------------------------------- */

static double lum(const double *c) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }
static void clipc(double *c) {
    double l = lum(c), n = c[0], x = c[0];
    int k;
    for (k = 1; k < 3; k++) { if (c[k] < n) n = c[k]; if (c[k] > x) x = c[k]; }
    for (k = 0; k < 3; k++) {
        if (n < 0) c[k] = l + (c[k] - l) * l / (l - n);
        if (x > 1) c[k] = l + (c[k] - l) * (1 - l) / (x - l);
    }
}
static void setlum(double *c, double l) { double d = l - lum(c); c[0] += d; c[1] += d; c[2] += d; clipc(c); }
static double satc(const double *c) {
    double n = c[0], x = c[0];
    int k;
    for (k = 1; k < 3; k++) { if (c[k] < n) n = c[k]; if (c[k] > x) x = c[k]; }
    return x - n;
}
static void setsat(double *c, double s) {
    int i[3] = { 0, 1, 2 }, t;
    if (c[i[0]] > c[i[1]]) { t = i[0]; i[0] = i[1]; i[1] = t; }
    if (c[i[1]] > c[i[2]]) { t = i[1]; i[1] = i[2]; i[2] = t; }
    if (c[i[0]] > c[i[1]]) { t = i[0]; i[0] = i[1]; i[1] = t; }
    if (c[i[2]] > c[i[0]]) { c[i[1]] = (c[i[1]] - c[i[0]]) * s / (c[i[2]] - c[i[0]]); c[i[2]] = s; }
    else c[i[1]] = c[i[2]] = 0;
    c[i[0]] = 0;
}
static double sep(int op, double cs, double cb) {
    double dd;
    switch (op) {
    case OGPU_BLEND_MULTIPLY: return cs * cb;
    case OGPU_BLEND_SCREEN: return cb + cs - cb * cs;
    case OGPU_BLEND_OVERLAY: return cb <= 0.5 ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb);
    case OGPU_BLEND_DARKEN: return cs < cb ? cs : cb;
    case OGPU_BLEND_LIGHTEN: return cs > cb ? cs : cb;
    case OGPU_BLEND_COLOR_DODGE: return cb == 0 ? 0 : cs >= 1 ? 1 : (cb / (1 - cs) < 1 ? cb / (1 - cs) : 1);
    case OGPU_BLEND_COLOR_BURN: return cb >= 1 ? 1 : cs == 0 ? 0 : 1 - ((1 - cb) / cs < 1 ? (1 - cb) / cs : 1);
    case OGPU_BLEND_HARD_LIGHT: return cs <= 0.5 ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb);
    case OGPU_BLEND_SOFT_LIGHT:
        if (cs <= 0.5) return cb - (1 - 2 * cs) * cb * (1 - cb);
        dd = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : sqrt(cb);
        return cb + (2 * cs - 1) * (dd - cb);
    case OGPU_BLEND_DIFFERENCE: return fabs(cb - cs);
    default: return cb + cs - 2 * cb * cs;     /* EXCLUSION */
    }
}

static void test_blends(void) {
    static const char *names[15] = { "multiply", "screen", "overlay", "darken", "lighten", "color-dodge", "color-burn",
                                     "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation",
                                     "color", "luminosity" };
    int op, it, k;
    char buf[160];
    unsigned rs = 99;
    for (op = OGPU_BLEND_MULTIPLY; op <= OGPU_BLEND_LUMINOSITY; op++) {
        int maxd = 0, tol = op == OGPU_BLEND_SOFT_LIGHT || op >= OGPU_BLEND_HUE ? 2 : 1;
        for (it = 0; it < 3000; it++) {
            unsigned s[4], d[4];
            double as, ad, Cs[3], Cb[3], Bl[3], o[4];
            for (k = 0; k < 2; k++) {
                unsigned *p = k ? d : s, a, j;
                rs = rs * 1103515245u + 12345u;
                a = (rs >> 8) & 3 ? (rs >> 12) & 255 : (rs >> 10) & 1 ? 255 : 0;
                p[0] = a;
                for (j = 1; j < 4; j++) { rs = rs * 1103515245u + 12345u; p[j] = a ? ((rs >> 8) & 0xFFFF) % (a + 1) : 0; }
            }
            /* the core: COMPOSITE_PD, solid source, one ARGB32 pixel */
            for (k = 0; k < 4; k++) arena[TGT + k] = (ogpu_u8)d[k];
            begin();
            ogpu_surface(&B, 0, TGT, 4, 1, 1, OGPU_FMT_ARGB32);
            ogpu_target(&B, 0);
            ogpu_composite_pd(&B, (unsigned long)op | OGPU_PD_SOLID, (unsigned long)s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3],
                              0, 0, 0, 0, 0, 0, 0, 1, 1);
            if (run() != OGPU_OK) { maxd = 999; break; }
            /* the reference */
            as = s[0] / 255.0; ad = d[0] / 255.0;
            for (k = 0; k < 3; k++) { Cs[k] = as > 0 ? s[k + 1] / 255.0 / as : 0; Cb[k] = ad > 0 ? d[k + 1] / 255.0 / ad : 0; }
            for (k = 0; k < 3; k++) { if (Cs[k] > 1) Cs[k] = 1; if (Cb[k] > 1) Cb[k] = 1; }
            if (op < OGPU_BLEND_HUE) for (k = 0; k < 3; k++) Bl[k] = sep(op, Cs[k], Cb[k]);
            else {
                for (k = 0; k < 3; k++) Bl[k] = op == OGPU_BLEND_SATURATION || op == OGPU_BLEND_LUMINOSITY ? Cb[k] : Cs[k];
                if (op == OGPU_BLEND_HUE) { setsat(Bl, satc(Cb)); setlum(Bl, lum(Cb)); }
                else if (op == OGPU_BLEND_SATURATION) { setsat(Bl, satc(Cs)); setlum(Bl, lum(Cb)); }
                else if (op == OGPU_BLEND_COLOR) setlum(Bl, lum(Cb));
                else setlum(Bl, lum(Cs));
            }
            o[0] = (as + ad - as * ad) * 255;
            for (k = 0; k < 3; k++) o[k + 1] = (s[k + 1] / 255.0 * (1 - ad) + d[k + 1] / 255.0 * (1 - as) + as * ad * Bl[k]) * 255;
            for (k = 0; k < 4; k++) {
                int dd = abs((int)floor(o[k] + 0.5) - arena[TGT + k]);
                if (dd > maxd) maxd = dd;
            }
        }
        snprintf(buf, sizeof buf, "blend mode %s: largest difference %d over 3000 pixels (tolerance %d)", names[op - OGPU_BLEND_MULTIPLY], maxd, tol);
        check(maxd <= tol, buf);
    }
    /* through a path: multiply over a colour, the coverage as the mask */
    {
        int W = 16, H = 16, i, bad = 0;
        for (i = 0; i < W * H; i++) { ogpu_u8 *q = arena + TGT + i * 4; q[0] = 255; q[1] = 200; q[2] = 100; q[3] = 50; }
        begin();
        p_start(); p_move(2.5, 2.5); p_line(13.5, 2.5); p_line(13.5, 13.5); p_line(2.5, 13.5); p_close(); p_end();
        ogpu_surface(&B, 0, TGT, (unsigned long)W * 4, W, H, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_path_fill(&B, OGPU_BLEND_MULTIPLY, 0xFF808080UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, 1 << 20);
        check(run() == OGPU_OK, "a path with multiply: no error");
        bad += arena[TGT + (8 * W + 8) * 4 + 1] != 100 || arena[TGT + (8 * W + 8) * 4 + 3] != 25;
        bad += arena[TGT + (2 * W + 2) * 4 + 1] != 175;         /* a quarter covered */
        bad += arena[TGT + (0 * W + 0) * 4 + 1] != 200;         /* outside */
        check(!bad, "a path with multiply: inside, at a corner and outside");
    }
}

int main(void) {
    test_fills();
    test_ops();
    test_patterns();
    test_strokes();
    test_gradients();
    test_blur();
    test_blends();
    check(ogpu_core_supports(OGPU_OP_PATH_FILL, OGPU_FMT_ARGB32) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_PATH_STROKE, OGPU_FMT_A8) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_PATH_FILL, OGPU_FMT_CLUT8) == OGPU_NONE
          && ogpu_core_supports(OGPU_OP_BLUR, OGPU_FMT_ARGB32) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_BLUR, OGPU_FMT_RGB565) == OGPU_NONE, "OGPU_Query's answers for v1.6");
    printf("vector (stream v1.6): %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
