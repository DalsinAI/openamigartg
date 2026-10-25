/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPUVecBench: the 68k core's vector and picture commands, as GFOffice
 * draws with them (W139's drawbench scenes, gfoffice tools/drawbench), timed
 * on this 68k for several copies of the core linked side by side (the
 * variants: the core at different commits), and each copy's pixels compared
 * with the first's, byte for byte. Everything is drawn into ARGB32 memory,
 * white before each round and not timed; nothing goes through
 * opengpu.library, so the 68k always draws, driver or not.
 *   OGPUVecBench [ROUNDS] [work=KB] [only=SCENE]
 * tools/vecbench.sh builds it (the variants from git refs). Written by Team 5
 * (25 Oct 2026) for the 68040 work on the core's software paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

/* vb_cores.h (tools/vecbench.sh writes it): VB_CORES, and for each variant
 * VB_CORE(name, init, run) */
#define VB_CORE(n, i, r) void i(struct ogpu_core *); long r(struct ogpu_core *, const ogpu_u8 *, long);
#include "vb_cores.h"
#undef VB_CORE
struct variant { const char *name; void (*init)(struct ogpu_core *); long (*run)(struct ogpu_core *, const ogpu_u8 *, long); };
#define VB_CORE(n, i, r) { n, i, r },
static const struct variant variants[] = {
#include "vb_cores.h"
};
#undef VB_CORE
#define NV ((int)(sizeof variants / sizeof variants[0]))

struct Device *TimerBase;
static struct timerequest treq;
static ULONG efreq;

static double now_us(void)
{
    struct EClockVal e;
    efreq = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1e6 / (double)efreq;
}

typedef struct { ULONG *px; int w, h; long bpr; } target;

static void clear(target *t, ULONG argb)
{
    long i, n = (long)t->w * t->h;
    for (i = 0; i < n; i++) t->px[i] = argb;
}

static ogpu_u8 *map_id(void *user, ogpu_u32 address, ogpu_u32 length) { (void)user; (void)length; return (ogpu_u8 *)address; }

#define BATCH_WORDS 8192
#define PATHS 64
#define PATH_WORDS 1024
static unsigned long work_bytes = 1024UL * 1024UL;
static ULONG *batch_buf, *path_buf, *grad_buf;
static UBYTE *work;
static struct OGPUBatch B, P[PATHS], G;
static int npaths;
static const struct variant *cur;
static struct ogpu_core core;

static void ob_begin(target *t)
{
    ogpu_batch_init(&B, batch_buf, BATCH_WORDS);
    ogpu_batch_init(&G, grad_buf, 512);
    npaths = 0;
    ogpu_surface(&B, 0, (unsigned long)t->px, (unsigned long)t->bpr, t->w, t->h, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
}
static struct OGPUBatch *ob_path(void)
{
    struct OGPUBatch *p = &P[npaths];
    ogpu_batch_init(p, path_buf + (long)npaths * PATH_WORDS, PATH_WORDS);
    npaths++;
    return p;
}
static long ob_run(void)
{
    if (B.overflow) return -100;
    cur->init(&core);
    core.map = map_id; core.fence = 0; core.ext = 0; core.user = 0;
    cur->run(&core, B.buf, B.words);
    return core.last_error;
}

static const long ID6[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
static long f8(double v) { return (long)floor(v * 256.0 + 0.5); }
static long f16(double v) { return (long)floor(v * 65536.0 + 0.5); }

/* an arc of the circle (cx, cy, r) from a0 to a1 (radians, y down) as cubics, the pen at its start */
static void arc_cubics(struct OGPUBatch *p, double cx, double cy, double r, double a0, double a1)
{
    int n = (int)ceil((a1 - a0) / (M_PI / 2.0)), i;
    double step;
    if (n < 1) n = 1;
    step = (a1 - a0) / n;
    for (i = 0; i < n; i++) {
        double a = a0 + i * step, b = a + step, k = 4.0 / 3.0 * tan(step / 4.0);
        double x0 = cos(a), y0 = sin(a), x3 = cos(b), y3 = sin(b);
        ogpu_path_cubic(p, f8(cx + r * (x0 - k * y0)), f8(cy + r * (y0 + k * x0)), f8(cx + r * (x3 + k * y3)),
                        f8(cy + r * (y3 - k * x3)), f8(cx + r * x3), f8(cy + r * y3));
    }
}

/* ---- the scenes (drawbench's, and a few more of GFOffice's shapes) ---- */

static const ULONG accents[6] = { 0xFF4472C4UL, 0xFFED7D31UL, 0xFFA5A5A5UL, 0xFFFFC000UL, 0xFF5B9BD5UL, 0xFF70AD47UL };
static const double pie_values[6] = { 30, 22, 17, 13, 10, 8 };
#define SERIES 4
#define POINTS 24
static double series_y[SERIES][POINTS];
#define PLOT_X0 470
#define PLOT_X1 780
#define PLOT_Y0 40
#define PLOT_Y1 440
#define GRID 9

static void make_series(void)
{
    unsigned long seed = 12345;
    int s, i;
    for (s = 0; s < SERIES; s++)
        for (i = 0; i < POINTS; i++) {
            seed = seed * 1103515245UL + 12345UL;
            series_y[s][i] = PLOT_Y0 + 20 + (double)((seed >> 8) % 3600) / 10.0;
        }
}
static double series_x(int i) { return PLOT_X0 + 6 + (double)i * (PLOT_X1 - PLOT_X0 - 12) / (POINTS - 1); }
static double pie_angle(int k)
{
    double sum = 0, total = 0;
    int i;
    for (i = 0; i < 6; i++) { total += pie_values[i]; if (i < k) sum += pie_values[i]; }
    return -M_PI / 2.0 + 2.0 * M_PI * sum / total;
}

static struct OGPUBatch *slice_path(int k)
{
    struct OGPUBatch *p = ob_path();
    double a0 = pie_angle(k), a1 = pie_angle(k + 1);
    ogpu_path_move(p, f8(240), f8(240));
    ogpu_path_line(p, f8(240 + 200 * cos(a0)), f8(240 + 200 * sin(a0)));
    arc_cubics(p, 240, 240, 200, a0, a1);
    ogpu_path_close(p);
    return p;
}

/* a pie: six slices, each filled, then a 2 px white border with round joins */
static long pie(target *t)
{
    int k;
    ob_begin(t);
    for (k = 0; k < 6; k++) {
        struct OGPUBatch *p = slice_path(k);
        ogpu_path_fill(&B, OGPU_PD_OVER, accents[k], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                       (unsigned long)work, work_bytes);
        ogpu_path_stroke(&B, OGPU_PD_OVER, 0xFFFFFFFFUL, 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words,
                         ID6, (unsigned long)work, work_bytes, f16(2.0), OGPU_CAP_BUTT | OGPU_JOIN_ROUND, f16(10.0), 0, 0, 0);
    }
    return ob_run();
}

/* the pie's slices filled only */
static long piefill(target *t)
{
    int k;
    ob_begin(t);
    for (k = 0; k < 6; k++) {
        struct OGPUBatch *p = slice_path(k);
        ogpu_path_fill(&B, OGPU_PD_OVER, accents[k], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                       (unsigned long)work, work_bytes);
    }
    return ob_run();
}

/* a line chart: 9 gridlines as fills, four series of 24 points stroked 3 px, round joins and caps */
static long lines(target *t)
{
    int s, i;
    ob_begin(t);
    for (i = 0; i < GRID; i++)
        ogpu_fill(&B, PLOT_X0, PLOT_Y0 + i * (PLOT_Y1 - PLOT_Y0) / (GRID - 1), PLOT_X1 - PLOT_X0, 1, 0xFFD9D9D9UL);
    for (s = 0; s < SERIES; s++) {
        struct OGPUBatch *p = ob_path();
        for (i = 0; i < POINTS; i++) (i ? ogpu_path_line : ogpu_path_move)(p, f8(series_x(i)), f8(series_y[s][i]));
        ogpu_path_stroke(&B, OGPU_PD_OVER, accents[s], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                         (unsigned long)work, work_bytes, f16(3.0), OGPU_CAP_ROUND | OGPU_JOIN_ROUND, f16(10.0), 0, 0, 0);
    }
    return ob_run();
}

/* polygons: a column chart's 12 bars on fractional edges, a 9-point star (even-odd) and a rounded rectangle, each
 * filled, then a dashed 1.5 px outline of the rounded rectangle with miter joins */
static long polys(target *t)
{
    struct OGPUBatch *p;
    int k;
    static long dash[2];
    ob_begin(t);
    for (k = 0; k < 12; k++) {
        double x = 20.3 + k * 36.5, top = 60.6 + (k * 37 % 11) * 22.25;
        p = ob_path();
        ogpu_path_move(p, f8(x), f8(460)); ogpu_path_line(p, f8(x), f8(top)); ogpu_path_line(p, f8(x + 24.4), f8(top));
        ogpu_path_line(p, f8(x + 24.4), f8(460)); ogpu_path_close(p);
        ogpu_path_fill(&B, OGPU_PD_OVER, accents[k % 6], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                       (unsigned long)work, work_bytes);
    }
    p = ob_path();
    for (k = 0; k < 9; k++) {
        double a = -M_PI / 2 + k * 4.0 * M_PI / 9.0;
        (k ? ogpu_path_line : ogpu_path_move)(p, f8(620 + 150 * cos(a)), f8(240 + 150 * sin(a)));
    }
    ogpu_path_close(p);
    ogpu_path_fill(&B, OGPU_PD_OVER | OGPU_PATH_EVENODD, 0xC0702080UL, 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words,
                   ID6, (unsigned long)work, work_bytes);
    p = ob_path();
    {
        double x0 = 480, y0 = 330, x1 = 780, y1 = 460, r = 18, kk = r * 0.5523;
        ogpu_path_move(p, f8(x0 + r), f8(y0));
        ogpu_path_line(p, f8(x1 - r), f8(y0));
        ogpu_path_cubic(p, f8(x1 - r + kk), f8(y0), f8(x1), f8(y0 + r - kk), f8(x1), f8(y0 + r));
        ogpu_path_line(p, f8(x1), f8(y1 - r));
        ogpu_path_cubic(p, f8(x1), f8(y1 - r + kk), f8(x1 - r + kk), f8(y1), f8(x1 - r), f8(y1));
        ogpu_path_line(p, f8(x0 + r), f8(y1));
        ogpu_path_cubic(p, f8(x0 + r - kk), f8(y1), f8(x0), f8(y1 - r + kk), f8(x0), f8(y1 - r));
        ogpu_path_line(p, f8(x0), f8(y0 + r));
        ogpu_path_cubic(p, f8(x0), f8(y0 + r - kk), f8(x0 + r - kk), f8(y0), f8(x0 + r), f8(y0));
        ogpu_path_close(p);
    }
    ogpu_path_fill(&B, OGPU_PD_OVER, 0xFFDEEBF7UL, 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                   (unsigned long)work, work_bytes);
    dash[0] = f16(6); dash[1] = f16(3);
    ogpu_path_stroke(&B, OGPU_PD_OVER, 0xFF1F3864UL, 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                     (unsigned long)work, work_bytes, f16(1.5), OGPU_CAP_BUTT | OGPU_JOIN_MITER, f16(10.0),
                     (unsigned long)dash, 2, 0);
    return ob_run();
}

/* a slide's background: a three-stop linear gradient over the whole target, corner to corner (vertical: top to
 * bottom) */
static int grad_vertical;
static long gradient(target *t)
{
    struct OGPUBatch *p;
    long geo[6];
    ob_begin(t);
    geo[0] = 0; geo[1] = 0; geo[2] = grad_vertical ? 0 : f16(t->w); geo[3] = f16(t->h); geo[4] = 0; geo[5] = 0;
    ogpu_gradient(&G, OGPU_GRAD_LINEAR, OGPU_SPREAD_PAD, 3, ID6, geo);
    ogpu_gradient_stop(&G, 0, 0xFF1F3864UL);
    ogpu_gradient_stop(&G, f16(0.5), 0xFF4472C4UL);
    ogpu_gradient_stop(&G, f16(1.0), 0xFFDEEBF7UL);
    p = ob_path();
    ogpu_path_move(p, 0, 0); ogpu_path_line(p, f8(t->w), 0); ogpu_path_line(p, f8(t->w), f8(t->h)); ogpu_path_line(p, 0, f8(t->h));
    ogpu_path_close(p);
    ogpu_path_fill(&B, OGPU_PD_SRC | OGPU_PATH_GRADIENT, (unsigned long)G.buf, 0, 0, 0, 0, 0, (unsigned long)p->buf,
                   (unsigned long)p->words, ID6, (unsigned long)work, work_bytes);
    return ob_run();
}
static long vgradient(target *t) { long r; grad_vertical = 1; r = gradient(t); grad_vertical = 0; return r; }

/* a full-page picture, scaled */
#define PIC_W 860
#define PIC_H 1216
static target pic;
static void make_picture(void)
{
    int x, y;
    for (y = 0; y < PIC_H; y++)
        for (x = 0; x < PIC_W; x++) {
            ULONG r = (ULONG)(x * 255 / PIC_W), g = (ULONG)(y * 255 / PIC_H), b = 160;
            if (((x + y) / 3) % 7 == 0) { r = 255; g = 255; b = 255; }
            if ((x / 4 + y / 4) % 2 && x > PIC_W / 2 && y > PIC_H / 2) { r /= 2; g /= 2; b /= 2; }
            pic.px[(long)y * PIC_W + x] = 0xFF000000UL | (r << 16) | (g << 8) | b;
        }
}
/* COMPOSITE with BILINEAR (opaque: straight colour is premultiplied) */
static long composite(target *t)
{
    ob_begin(t);
    ogpu_surface(&B, 1, (unsigned long)pic.px, (unsigned long)pic.bpr, PIC_W, PIC_H, OGPU_FMT_ARGB32);
    ogpu_composite(&B, 1, 0, 0, PIC_W, PIC_H, 0, 0, t->w, t->h, 255, OGPU_COMP_BILINEAR);
    return ob_run();
}
/* the picture as a padded bilinear pattern, painting its rectangle's path */
static long pattern(target *t)
{
    struct OGPUBatch *p;
    long m[6];
    ob_begin(t);
    ogpu_surface(&B, 1, (unsigned long)pic.px, (unsigned long)pic.bpr, PIC_W, PIC_H, OGPU_FMT_ARGB32);
    m[0] = f16((double)t->w / PIC_W); m[1] = 0; m[2] = 0; m[3] = 0; m[4] = f16((double)t->h / PIC_H); m[5] = 0;
    ogpu_image_paint(&G, 1, OGPU_PATTERN_PAD | OGPU_PATTERN_BILINEAR, 0, 0, PIC_W, PIC_H, m, 255);
    p = ob_path();
    ogpu_path_move(p, 0, 0); ogpu_path_line(p, f8(t->w), 0); ogpu_path_line(p, f8(t->w), f8(t->h)); ogpu_path_line(p, 0, f8(t->h));
    ogpu_path_close(p);
    ogpu_path_fill(&B, OGPU_PD_SRC | OGPU_PATH_PATTERN, (unsigned long)G.buf, 0, 0, 0, 0, 0, (unsigned long)p->buf,
                   (unsigned long)p->words, ID6, (unsigned long)work, work_bytes);
    return ob_run();
}

/* ---- timing and the comparison ---- */

static int rounds = 5;
static ULONG *keep;
static target chart, page, half;

static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

struct scene { const char *name; long (*fn)(target *); target *t; };

static void bench(const struct scene *sc)
{
    int v;
    long npx = (long)sc->t->w * sc->t->h;
    for (v = 0; v < NV; v++) {
        double us[32], t0;
        long r = 0, i, diff = 0;
        int k, n = rounds > 32 ? 32 : rounds, maxd = 0;
        cur = &variants[v];
        for (k = 0; k < n; k++) {
            clear(sc->t, 0xFFFFFFFFUL);
            t0 = now_us();
            r = sc->fn(sc->t);
            us[k] = now_us() - t0;
            if (r) break;
        }
        if (r) { printf("%-9s %-14s error %ld\n", sc->name, cur->name, r); continue; }
        qsort(us, (size_t)n, sizeof us[0], dcmp);
        if (!v) memcpy(keep, sc->t->px, (size_t)npx * 4);
        else
            for (i = 0; i < npx; i++)
                if (keep[i] != sc->t->px[i]) {
                    ULONG a = keep[i], b = sc->t->px[i];
                    int s;
                    diff++;
                    for (s = 0; s < 32; s += 8) { int e = abs((int)((a >> s) & 255) - (int)((b >> s) & 255)); if (e > maxd) maxd = e; }
                }
        printf("%-9s %-14s best %9.0f us  median %9.0f us", sc->name, cur->name, us[0], us[n / 2]);
        if (v) printf("  pixels differing from %s: %ld (largest %d)", variants[0].name, diff, maxd);
        printf("\n");
    }
}

static int alloc_target(target *t, int w, int h)
{
    t->w = w; t->h = h; t->bpr = (long)w * 4;
    t->px = (ULONG *)malloc((size_t)w * h * 4);
    return t->px != NULL;
}

int main(int argc, char **argv)
{
    static const struct scene scenes[] = {
        { "pie", pie, &chart }, { "piefill", piefill, &chart }, { "lines", lines, &chart }, { "polys", polys, &chart },
        { "gradient", gradient, &chart }, { "vgradient", vgradient, &chart },
        { "page100c", composite, &page }, { "page50c", composite, &half },
        { "page100p", pattern, &page }, { "page50p", pattern, &half },
    };
    const char *only = 0;
    int i;
    /* OS 3.2's ROM mathieeesingbas.library can leave the FPU in single
     * precision: the scenes' coordinates are worked out in doubles */
    __asm__ volatile("fmove.l %0,%%fpcr" : : "d"(0));
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "work=", 5)) work_bytes = strtoul(argv[i] + 5, NULL, 10) << 10;
        else if (!strncmp(argv[i], "only=", 5)) only = argv[i] + 5;
        else if (atoi(argv[i]) > 0) rounds = atoi(argv[i]);
    }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) { printf("no timer.device\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    now_us();
    batch_buf = (ULONG *)malloc(BATCH_WORDS * 4);
    path_buf = (ULONG *)malloc((size_t)PATHS * PATH_WORDS * 4);
    grad_buf = (ULONG *)malloc(512 * 4);
    work = (UBYTE *)malloc(work_bytes);
    pic.w = PIC_W; pic.h = PIC_H; pic.bpr = PIC_W * 4;
    pic.px = (ULONG *)malloc((size_t)PIC_W * PIC_H * 4);
    keep = (ULONG *)malloc((size_t)794 * 1123 * 4);
    if (!batch_buf || !path_buf || !grad_buf || !work || !pic.px || !keep || !alloc_target(&chart, 800, 480) ||
        !alloc_target(&page, 794, 1123) || !alloc_target(&half, 397, 561)) {
        printf("out of memory\n");
        return 20;
    }
    make_series();
    make_picture();
    printf("OGPUVecBench: %d rounds, %lu KB of work memory, E-clock %lu Hz; %d variants:", rounds, work_bytes >> 10,
           (unsigned long)efreq, NV);
    for (i = 0; i < NV; i++) printf(" %s", variants[i].name);
    printf("\n");
    for (i = 0; i < (int)(sizeof scenes / sizeof scenes[0]); i++)
        if (!only || strstr(only, scenes[i].name)) bench(&scenes[i]);
    CloseDevice((struct IORequest *)&treq);
    return 0;
}
