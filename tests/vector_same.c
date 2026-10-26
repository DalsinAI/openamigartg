/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The core against an earlier copy of itself, byte for byte: random batches
 * of the commands whose 68k paths were made faster (PATH_FILL, PATH_STROKE
 * with every paint, mask, operator and format; COMPOSITE), each run by both
 * on the same memory, and every byte of the targets, sources and masks
 * compared, with the error and where it was. The work memory isn't
 * compared: it is scratch. Written by Team 5 (25 Oct 2026) with the 68040
 * paths; tests/vector_same.sh builds the earlier copy from a git ref.
 *   vector_same [ROUNDS [SEED [MIX]]]     MIX: 0 both commands (default), 1 paths only, 2 COMPOSITE only,
 *                                         3 paths painted with gradients and patterns, OVER or SRC, into ARGB32 or XRGB32
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

/* the earlier core, built with its three functions renamed */
void old_ogpu_core_init(struct ogpu_core *c);
long old_ogpu_core_run(struct ogpu_core *c, const ogpu_u8 *stream, long words);

#define ARENA   0x400000UL
#define TGT     0x000000UL      /* the target, up to 256 x 256 x 4 */
#define SRC     0x040000UL      /* a slot to paint or composite with */
#define MSK     0x080000UL      /* a mask slot */
#define PATH    0x0C0000UL      /* path words */
#define GRAD    0x0D0000UL      /* a gradient or a pattern */
#define DASH    0x0D8000UL      /* dashes */
#define CMP_END 0x0E0000UL      /* compared up to here */
#define WORK    0x100000UL      /* work memory, up to 3 MB */
static ogpu_u8 *arena, *arena2, *start;
static ogpu_u8 *cur;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > ARENA - address) return 0;
    return cur + address;
}

static unsigned long seed = 1;
static unsigned long rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned long)(seed >> 33); }
static long rn(long n) { return n > 0 ? (long)(rnd() % (unsigned long)n) : 0; }
static long rr(long a, long b) { return a + rn(b - a + 1); }

static ogpu_u8 sbuf[16384];
/* The non-separable blend modes divide by zero (both copies; on the 68k a
 * trap) where a colour isn't premultiplied (a channel past its alpha): they
 * are only drawn in rounds whose target and paint are premultiplied. */
static int premul_round, pure_colours, focus, cmd_no;   /* cmd_no: the command's own path and paint memory */
static struct OGPUBatch B, P, G;
static const int fmts[] = { OGPU_FMT_ARGB32, OGPU_FMT_ARGB32, OGPU_FMT_ARGB32, OGPU_FMT_XRGB32, OGPU_FMT_XRGB32,
                            OGPU_FMT_RGB565, OGPU_FMT_RGB565PC, OGPU_FMT_RGB555, OGPU_FMT_RGB555PC, OGPU_FMT_A8,
                            OGPU_FMT_BGRA32, OGPU_FMT_RGBA32, OGPU_FMT_ABGR32, OGPU_FMT_CLUT8 };
static int bpp_of(int f) { return f == OGPU_FMT_A8 || f == OGPU_FMT_CLUT8 ? 1 : f >= OGPU_FMT_RGB565PC && f <= OGPU_FMT_RGB555PC ? 2 : f == OGPU_FMT_RGB565 ? 2 : 4; }

static void fill_rand(unsigned long at, unsigned long n, int kind) {
    unsigned long i;
    for (i = 0; i < n; i++) {
        unsigned v = (unsigned)rnd();
        if (kind == 1) v = (i & 3) == 0 ? 255 : v;          /* opaque ARGB */
        else if (kind == 2) v = (v & 1) ? 0 : 255;           /* 0 or 255 */
        start[at + i] = (ogpu_u8)v;
    }
}

/* a slot of random size and content; returns its width and height */
static void slot_rand(int slot, unsigned long at, int *w, int *h, int allow_any) {
    int f = allow_any ? fmts[rn(13)] : OGPU_FMT_ARGB32, bpp;
    long bpr;
    *w = (int)rr(1, rn(4) ? 64 : 200); *h = (int)rr(1, rn(4) ? 64 : 200);
    bpp = bpp_of(f);
    bpr = (long)*w * bpp + (rn(3) ? 0 : rn(9));
    fill_rand(at, (unsigned long)bpr * *h, rn(3) == 0 ? (int)rn(3) : 0);
    ogpu_surface(&B, slot, at + (rn(5) ? 0 : (unsigned long)rn(4)), (unsigned long)bpr, *w, *h, f);
}

static long coord(int span) {    /* 24.8, around a target span px wide */
    long v;
    switch (rn(12)) {
    case 0: v = rr(-(long)span * 256, 2L * span * 256); break;
    case 1: v = rr(0, span) * 256; break;                       /* on the grid */
    case 2: v = rr(-0x1000000L, 0x1000000L); break;            /* far */
    default: v = rr(-20L * 256, ((long)span + 20) * 256); break;
    }
    return v;
}

static void path_rand(int w, int h) {
    int sp, nsp = (int)rr(1, rn(3) ? 2 : 5), k, n;
    ogpu_batch_init(&P, start + PATH + (unsigned long)cmd_no * 0x4000, 0x1000);
    if (rn(6) == 0) {                                           /* a pixel-aligned rectangle or a circle */
        long x0 = coord(w), y0 = coord(h), x1 = coord(w), y1 = coord(h);
        if (rn(2)) { x0 &= ~255L; y0 &= ~255L; x1 &= ~255L; y1 &= ~255L; }
        if (rn(2)) {
            ogpu_path_move(&P, x0, y0); ogpu_path_line(&P, x1, y0); ogpu_path_line(&P, x1, y1); ogpu_path_line(&P, x0, y1);
            ogpu_path_close(&P);
        } else {
            long cx = x0, cy = y0, r = rr(1, 60L * 256), kk = r * 552 / 1000;
            ogpu_path_move(&P, cx + r, cy);
            ogpu_path_cubic(&P, cx + r, cy + kk, cx + kk, cy + r, cx, cy + r);
            ogpu_path_cubic(&P, cx - kk, cy + r, cx - r, cy + kk, cx - r, cy);
            ogpu_path_cubic(&P, cx - r, cy - kk, cx - kk, cy - r, cx, cy - r);
            ogpu_path_cubic(&P, cx + kk, cy - r, cx + r, cy - kk, cx + r, cy);
            ogpu_path_close(&P);
        }
        return;
    }
    if (rn(8) == 0) ogpu_path_line(&P, coord(w), coord(h));      /* starts at (0, 0) */
    for (sp = 0; sp < nsp; sp++) {
        ogpu_path_move(&P, coord(w), coord(h));
        n = (int)rr(0, rn(4) ? 8 : 30);
        for (k = 0; k < n; k++) {
            switch (rn(rn(3) ? 2 : 4)) {
            case 0: case 1: ogpu_path_line(&P, coord(w), coord(h)); break;
            case 2: ogpu_path_quad(&P, coord(w), coord(h), coord(w), coord(h)); break;
            default: ogpu_path_cubic(&P, coord(w), coord(h), coord(w), coord(h), coord(w), coord(h)); break;
            }
        }
        if (rn(2)) ogpu_path_close(&P);
    }
}

static void matrix_rand(long *m) {
    m[0] = 0x10000; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = 0x10000; m[5] = 0;
    switch (rn(5)) {
    case 0: m[0] = rr(-0x30000, 0x30000); m[1] = rr(-0x30000, 0x30000); m[3] = rr(-0x30000, 0x30000); m[4] = rr(-0x30000, 0x30000);
            m[2] = rr(-0x400000, 0x400000); m[5] = rr(-0x400000, 0x400000); break;
    case 1: m[0] = m[4] = rr(0x4000, 0x40000); m[2] = rr(-0x200000, 0x200000); m[5] = rr(-0x200000, 0x200000); break;
    case 2: if (rn(4) == 0) { m[0] = m[1] = m[3] = m[4] = 0; } break;     /* flat */
    default: break;
    }
}

static unsigned long colour_rand(void) {
    unsigned a = rn(3) ? 255 : (unsigned)rn(256), r, g, b;
    if (rn(6) == 0) a = 0;
    r = (unsigned)rn(a + 1); g = (unsigned)rn(a + 1); b = (unsigned)rn(a + 1);    /* premultiplied */
    if (rn(10) == 0 && !pure_colours) { r = (unsigned)rn(256); g = (unsigned)rn(256); b = (unsigned)rn(256); }
    return ((unsigned long)a << 24) | ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
}

static unsigned long paint_rand(unsigned long *flags, int tw, int th, int *px, int *py) {
    long m[6], geo[6];
    int k, n, sw, sh;
    *px = *py = 0;
    switch (focus ? 4 + (int)rn(4) : (int)rn(8)) {
    case 0: case 1: case 2: return colour_rand();
    case 3:                                                     /* a slot */
        slot_rand(1, SRC, &sw, &sh, 1);
        *flags |= OGPU_PATH_SLOT | (rn(2) ? OGPU_PATH_REPEAT : 0);
        *px = (int)rr(-40, tw); *py = (int)rr(-40, th);
        return 1;
    case 4: case 5:                                             /* a gradient */
        ogpu_batch_init(&G, start + GRAD + (unsigned long)cmd_no * 0x1000, 0x400);
        matrix_rand(m);
        if (rn(2)) { m[0] = m[4] = 0x10000; m[1] = m[3] = m[2] = m[5] = 0; }
        for (k = 0; k < 6; k++) geo[k] = rr(-0x400000, (long)(tw > th ? tw : th) * 0x10000 * 2);
        k = (int)rr(1, 3);
        if (k == 1 && rn(3) == 0) { geo[0] = 0; geo[1] = 0; geo[2] = (long)tw << 16; geo[3] = (long)th << 16; }
        if (k == 2) { geo[2] = rr(0, 0x400000); geo[5] = rr(0, 0x600000); }
        if (k == 3) { geo[2] = rr(0, 0xFFFF); }
        n = (int)rr(1, 5);
        ogpu_gradient(&G, k, (int)rn(3), n, m, geo);
        for (k = 0; k < n; k++) ogpu_gradient_stop(&G, rn(5) ? rr(0, 0x10000) : rr(-0x8000, 0x18000), colour_rand());
        *flags |= OGPU_PATH_GRADIENT;
        return GRAD + (unsigned long)cmd_no * 0x1000;
    default:                                                    /* a pattern */
        slot_rand(1, SRC, &sw, &sh, rn(4) == 0);
        ogpu_batch_init(&G, start + GRAD + (unsigned long)cmd_no * 0x1000, 0x400);
        matrix_rand(m);
        if (rn(2)) { m[0] = rr(0x2000, 0x30000); m[4] = rr(0x2000, 0x30000); m[1] = m[3] = 0; m[2] = rr(-0x200000, 0x200000); m[5] = rr(-0x200000, 0x200000); }
        {
            int x = (int)rn(sw), y = (int)rn(sh);
            ogpu_image_paint(&G, 1, (unsigned long)rn(4) | (rn(2) ? OGPU_PATTERN_BILINEAR : 0), rn(4) ? 0 : x, rn(4) ? 0 : y,
                             rn(4) ? 0 : (int)rr(1, sw - x), rn(4) ? 0 : (int)rr(1, sh - y), m, rn(3) ? 255 : (int)rn(256));
        }
        *flags |= OGPU_PATH_PATTERN;
        return GRAD + (unsigned long)cmd_no * 0x1000;
    }
}

static int op_rand(void) {
    static const int common[] = { OGPU_PD_OVER, OGPU_PD_OVER, OGPU_PD_OVER, OGPU_PD_SRC, OGPU_PD_SRC };
    if (rn(2) || (focus && rn(8))) return common[rn(5)];
    if (rn(4)) return (int)rn(OGPU_PD_OPS);
    if (rn(8) == 0) return OGPU_BLEND_LUMINOSITY + 1;                   /* not an operator */
    return (int)rr(OGPU_BLEND_MULTIPLY, premul_round ? OGPU_BLEND_LUMINOSITY : OGPU_BLEND_HUE - 1);
}

static void path_cmd(int tw, int th, int tbpp) {
    unsigned long flags = (unsigned long)op_rand(), paint, wb;
    int px = 0, py = 0, mx = 0, my = 0, stroke = (int)rn(2);
    long m[6];
    int w3c = (int)(flags & 255) >= OGPU_BLEND_HUE && (int)(flags & 255) <= OGPU_BLEND_LUMINOSITY;
    if (rn(3) == 0) flags |= OGPU_PATH_EVENODD;
    pure_colours = w3c;
    paint = w3c ? colour_rand() : paint_rand(&flags, tw, th, &px, &py);
    pure_colours = 0;
    if (rn(6) == 0 && !w3c) {
        int mw, mh;
        slot_rand(2, MSK, &mw, &mh, 1);
        flags |= OGPU_PATH_MASK;
        mx = (int)rr(-30, tw); my = (int)rr(-30, th);
    }
    path_rand(tw, th);
    matrix_rand(m);
    if (rn(2)) { m[0] = m[4] = 0x10000; m[1] = m[3] = 0; m[2] = rn(2) ? 0 : rr(-0x100000, 0x100000); m[5] = rn(2) ? 0 : rr(-0x100000, 0x100000); }
    (void)tbpp;
    switch (rn(8)) {        /* work memory: the least, about a row with its bits, a little, or plenty */
    case 0: wb = (unsigned long)tw * 32; break;
    case 1: wb = (unsigned long)tw * 32 + (unsigned long)rn(tw / 8 + 8) * 4; break;
    case 2: wb = (unsigned long)tw * 32 * rr(1, 4) + (unsigned long)rn(64) * 16; break;
    case 3: wb = (unsigned long)rn(tw * 32); break;                     /* too little */
    case 4: wb = (unsigned long)tw * 33 * rr(2, 12); break;
    default: wb = 0x300000UL; break;
    }
    if (!stroke)
        ogpu_path_fill(&B, flags, paint, px, py, 2, mx, my, PATH + (unsigned long)cmd_no * 0x4000, (unsigned long)P.words, m, WORK, wb);
    else {
        long width = rn(8) == 0 ? rr(-0x10000, 0x8000) : rr(0x1000, rn(3) ? 0x60000 : 0x200000);
        unsigned long style = (unsigned long)rn(rn(20) ? 3 : 4) | ((unsigned long)rn(rn(20) ? 3 : 4) << 4);
        unsigned long nd = rn(3) ? 0 : (unsigned long)rr(1, 6), k;
        for (k = 0; k < nd; k++) {          /* the lengths, 16.16, big-endian */
            long v = rn(6) ? rr(0x8000, 0x200000) : rr(-0x10000, 0x10000);
            ogpu_u8 *q = start + DASH + (unsigned long)cmd_no * 64 + k * 4;
            q[0] = (ogpu_u8)((unsigned long)v >> 24); q[1] = (ogpu_u8)((unsigned long)v >> 16); q[2] = (ogpu_u8)((unsigned long)v >> 8); q[3] = (ogpu_u8)v;
        }
        ogpu_path_stroke(&B, flags, paint, px, py, 2, mx, my, PATH + (unsigned long)cmd_no * 0x4000, (unsigned long)P.words, m, WORK, wb, width, style,
                         rr(0x10000, 0xA0000), DASH + (unsigned long)cmd_no * 64, nd, rr(-0x400000, 0x400000));
    }
}

static void composite_cmd(int tw, int th) {
    int sw, sh, x, y, w, h, sx, sy, cw, ch;
    unsigned long flags = 0;
    slot_rand(1, SRC, &sw, &sh, rn(3) == 0);
    sx = (int)rn(sw); sy = (int)rn(sh); cw = (int)rr(1, sw - sx); ch = (int)rr(1, sh - sy);
    if (rn(3) == 0) { sx = sy = 0; cw = sw; ch = sh; }
    x = (int)rr(-20, tw); y = (int)rr(-20, th);
    w = (int)rr(1, rn(2) ? tw + 20 : 3 * tw); h = (int)rr(1, rn(2) ? th + 20 : 3 * th);
    if (rn(2)) flags |= OGPU_COMP_BILINEAR;
    if (rn(2)) flags |= OGPU_COMP_SRCALPHA;
    if (rn(6) == 0) flags |= rn(2) ? OGPU_COMP_ADD : OGPU_COMP_IN;
    if (rn(8) == 0) flags |= OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(rn(8));
    if (rn(8) == 0 && !(flags & OGPU_COMP_BILINEAR)) flags |= OGPU_COMP_PIXELART;
    if (rn(6) == 0) {
        int mw, mh;
        ogpu_surface(&B, 2, MSK, (unsigned long)w + 8, w + 8, h + 8, OGPU_FMT_A8);
        fill_rand(MSK, (unsigned long)(w + 8) * (h + 8), (int)rn(3));
        (void)mw; (void)mh;
        ogpu_composite_masked(&B, 1, sx, sy, cw, ch, x, y, w, h, rn(3) ? 255 : (int)rn(256), flags | OGPU_COMP_MASK, 2,
                              (int)rn(8), (int)rn(8));
        return;
    }
    ogpu_composite(&B, 1, sx, sy, cw, ch, x, y, w, h, rn(3) ? 255 : (int)rn(256), flags);
}

/* a target whose every pixel is premultiplied (a channel never past its alpha) */
static void premul_fill(unsigned long at, long bpr, int w, int h, int f) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ogpu_u8 *p = start + at + (unsigned long)y * bpr + (unsigned long)x * 4;
            unsigned a = rn(2) ? 255 : (unsigned)rn(256), c0 = (unsigned)rn(a + 1), c1 = (unsigned)rn(a + 1), c2 = (unsigned)rn(a + 1);
            if (f == OGPU_FMT_BGRA32 || f == OGPU_FMT_RGBA32) { p[0] = (ogpu_u8)c0; p[1] = (ogpu_u8)c1; p[2] = (ogpu_u8)c2; p[3] = (ogpu_u8)a; }
            else { p[0] = (ogpu_u8)a; p[1] = (ogpu_u8)c0; p[2] = (ogpu_u8)c1; p[3] = (ogpu_u8)c2; }
        }
}

static long run_with(ogpu_u8 *mem, int old, struct ogpu_core *c) {
    cur = mem;
    if (old) old_ogpu_core_init(c); else ogpu_core_init(c);
    c->map = map; c->fence = 0; c->ext = 0; c->user = 0;
    return old ? old_ogpu_core_run(c, sbuf, B.words) : ogpu_core_run(c, sbuf, B.words);
}

int main(int argc, char **argv) {
    long rounds = argc > 1 ? atol(argv[1]) : 3000, r, bad = 0, cmds = 0, drawn = 0, failed = 0;
    unsigned long s0 = argc > 2 ? strtoul(argv[2], 0, 10) : 1;
    int mix = argc > 3 ? atoi(argv[3]) : 0;
    focus = mix == 3;
    struct ogpu_core co, cn;
    start = calloc(1, ARENA); arena = calloc(1, ARENA); arena2 = calloc(1, ARENA);
    if (!start || !arena || !arena2) return 2;
    for (r = 0; r < rounds; r++) {
        int tw, th, f, bpp, k, n;
        long bpr;
        unsigned long i;
        seed = s0 * 1000003UL + (unsigned long)r;
        memset(start, 0, CMP_END);
        ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
        tw = (int)rr(1, rn(3) ? 120 : 250); th = (int)rr(1, rn(3) ? 120 : 250);
        f = focus && rn(8) ? fmts[rn(5)] : fmts[rn(rn(10) ? 13 : 14)];
        bpp = bpp_of(f);
        bpr = (long)tw * bpp + (rn(4) ? 0 : rn(8));
        premul_round = bpp == 4 && rn(3) == 0;
        if (premul_round) premul_fill(TGT, bpr, tw, th, f);
        else fill_rand(TGT, (unsigned long)bpr * th, (int)rn(2));
        if (bpp != 4) premul_round = rn(3) == 0;                /* no alpha but A8's, which has no colour */
        ogpu_surface(&B, 0, premul_round && bpp == 4 ? TGT : TGT + (rn(6) ? 0 : (unsigned long)rn(4)), (unsigned long)bpr, tw, th, f);
        ogpu_target(&B, 0);
        if (rn(5) == 0) ogpu_clip(&B, (int)rr(-10, tw), (int)rr(-10, th), (int)rr(0, tw), (int)rr(0, th));
        n = (int)rr(1, 3);
        for (k = 0; k < n; k++) {
            cmd_no = k;
            if (mix == 2 || (mix == 0 && !rn(5))) composite_cmd(tw, th); else path_cmd(tw, th, bpp);
            cmds++;
        }
        memcpy(arena, start, CMP_END); memcpy(arena2, start, CMP_END);
        run_with(arena, 1, &co);
        run_with(arena2, 0, &cn);
        if (co.last_error) failed++;
        if (memcmp(arena, start, CMP_END)) drawn++;
        if (co.last_error != cn.last_error || co.error_word != cn.error_word) {
            printf("round %ld (seed %lu): error %d at %ld before, %d at %ld now\n", r, s0, co.last_error, co.error_word, cn.last_error,
                   cn.error_word);
            bad++;
            continue;
        }
        for (i = 0; i < CMP_END; i++)
            if (arena[i] != arena2[i]) {
                printf("round %ld (seed %lu): byte %06lx differs (%02x before, %02x now), target %dx%d format %d\n", r, s0, i,
                       arena[i], arena2[i], tw, th, f);
                bad++;
                break;
            }
    }
    printf("vector_same: %ld rounds, %ld commands (%ld rounds drew something, %ld met an error), %ld differ\n", rounds, cmds,
           drawn, failed, bad);
    return bad != 0;
}
