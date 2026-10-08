/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU core tests (G1): each command against a plain per-pixel
 * reference, the errors, random streams that must never reach outside the
 * memory they were given, and a golden scene whose checksum every back end
 * (68k, Cradle, PiStorm) must reproduce: tests/golden/opengpu-g1.txt, and
 * for stream v1.1 (A8 masks) a second one: tests/golden/opengpu-v11.txt. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"
#include "golden_scenes.h"

/* Amiga addresses in these tests are offsets into one arena. */
#define ARENA (1L << 20)
static ogpu_u8 arena[ARENA];
static long fences[8], nfences;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static void on_fence(void *user, ogpu_u32 id) { (void)user; if (nfences < 8) fences[nfences++] = (long)id; }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char sbuf[1 << 16];
static struct OGPUBatch B;
static struct ogpu_core C;

static void begin(void) { ogpu_batch_init(&B, sbuf, sizeof sbuf / 4); }
static long run(void) {
    ogpu_core_init(&C);
    C.map = map; C.fence = on_fence; C.user = 0;
    nfences = 0;
    return ogpu_core_run(&C, sbuf, B.words);
}

static int bpp_of(int f) { return f == OGPU_FMT_RGB565 ? 2 : f == OGPU_FMT_ARGB32 ? 4 : 1; }
static unsigned long px(long base, long bpr, int f, int x, int y) {
    const ogpu_u8 *p = arena + base + y * bpr + x * bpp_of(f);
    if (f == OGPU_FMT_RGB565) return ((unsigned long)p[0] << 8) | p[1];
    if (f == OGPU_FMT_ARGB32) return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
    return p[0];
}
static void setpx(long base, long bpr, int f, int x, int y, unsigned long v) {
    ogpu_u8 *p = arena + base + y * bpr + x * bpp_of(f);
    if (f == OGPU_FMT_RGB565) { p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; }
    else if (f == OGPU_FMT_ARGB32) { p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }
    else p[0] = (ogpu_u8)v;
}

static unsigned long rnd_state = 12345;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }

static void noise(long base, long bytes) { long i; for (i = 0; i < bytes; i++) arena[base + i] = (ogpu_u8)rnd(); }

#define W 64
#define H 40
#define T0 0x1000L      /* target */
#define S0 0x40000L     /* a second surface */
#define D0 0x80000L     /* data: templates, patterns, pixels */
#define REF 0xC0000L    /* the reference picture */

/* ---- each command against a reference ---------------------------------------------- */

static void test_fill(int f) {
    long bpr = W * bpp_of(f) + 6;
    unsigned long colour = f == OGPU_FMT_CLUT8 ? 0x5A : f == OGPU_FMT_RGB565 ? 0xF81F : 0x80123456UL;
    int x, y;
    noise(T0, bpr * H);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, f); ogpu_target(&B, 0);
    ogpu_clip(&B, 5, 3, 40, 30);
    ogpu_fill(&B, -10, -10, 30, 30, colour);    /* reaches over the clip's corner */
    ogpu_clip(&B, 0, 0, 0, 0);
    ogpu_invert(&B, 50, 30, 100, 100, f == OGPU_FMT_ARGB32 ? 0x00FFFFFFUL : 0xFFFFUL);
    CHECK(run() == 6 && C.last_error == OGPU_OK, "fill %d ran with %d", f, C.last_error);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        unsigned long r = px(REF, bpr, f, x, y);
        if (x >= 5 && x < 20 && y >= 3 && y < 20) r = colour;
        if (x >= 50 && y >= 30) r ^= f == OGPU_FMT_CLUT8 ? 0xFF : f == OGPU_FMT_RGB565 ? 0xFFFF : 0x00FFFFFFUL;
        CHECK(px(T0, bpr, f, x, y) == r, "fill fmt %d at %d,%d: %lx, want %lx", f, x, y, px(T0, bpr, f, x, y), r);
        if (failures > 20) return;
    }
}

static void test_copy_overlap(int f, int dx, int dy) {
    long bpr = W * bpp_of(f);
    int x, y, sx = 10, sy = 8, w = 30, h = 20;
    noise(T0, bpr * H);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    /* Reference: through a copy of the source rectangle. */
    {
        static unsigned long tmp[W * H];
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) tmp[y * w + x] = px(REF, bpr, f, sx + x, sy + y);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            int tx = sx + dx + x, ty = sy + dy + y;
            if (tx >= 0 && ty >= 0 && tx < W && ty < H) setpx(REF, bpr, f, tx, ty, tmp[y * w + x]);
        }
    }
    begin();
    ogpu_surface(&B, 3, T0, bpr, W, H, f); ogpu_target(&B, 3);
    ogpu_copy(&B, 3, sx, sy, sx + dx, sy + dy, w, h);
    run();
    CHECK(memcmp(arena + T0, arena + REF, (size_t)(bpr * H)) == 0, "copy fmt %d by %d,%d", f, dx, dy);
}

static void test_copy_between(void) {
    long bpr = W * 2;
    int x, y;
    noise(T0, bpr * H); noise(S0, bpr * H);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        int sx = x - 20 + 3, sy = y - 15 - 4;      /* source 3,-4 lands at 20,15 */
        if (x >= 20 && y >= 15 && x < 20 + 50 && y < 15 + 30 && sx >= 0 && sy >= 0 && sx < W && sy < H)
            setpx(REF, bpr, OGPU_FMT_RGB565, x, y, px(S0, bpr, OGPU_FMT_RGB565, sx, sy));
    }
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_RGB565);
    ogpu_surface(&B, 1, S0, bpr, W, H, OGPU_FMT_RGB565);
    ogpu_target(&B, 0);
    ogpu_copy(&B, 1, 3, -4, 20, 15, 50, 30);
    run();
    CHECK(memcmp(arena + T0, arena + REF, (size_t)(bpr * H)) == 0, "copy between surfaces, source clipped");
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_RGB565);
    ogpu_surface(&B, 1, S0, W, W, H, OGPU_FMT_CLUT8);
    ogpu_target(&B, 0);
    ogpu_copy(&B, 1, 0, 0, 0, 0, 4, 4);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "copy across formats is refused");
}

static void test_template(int f, int mode) {
    long bpr = W * bpp_of(f), tbpr = 12;
    unsigned long fg = f == OGPU_FMT_ARGB32 ? 0xFF00FF00UL : 0x0F, bg = f == OGPU_FMT_ARGB32 ? 0xFF0000FFUL : 0x03;
    int x, y, first = 5, tx = -3, ty = 2, tw = 70, th = 20;
    noise(T0, bpr * H); noise(D0, tbpr * th);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    for (y = 0; y < th; y++) for (x = 0; x < tw; x++) {
        int ax = tx + x, ay = ty + y, bit = first + x;
        int set = (arena[D0 + y * tbpr + (bit >> 3)] >> (7 - (bit & 7))) & 1;
        if (ax < 0 || ay < 0 || ax >= W || ay >= H) continue;
        if (ax >= 40) continue;                                 /* outside the clips below */
        if (mode & OGPU_INVERSVID) set = !set;
        if (mode & OGPU_COMPLEMENT) { if (set) setpx(REF, bpr, f, ax, ay, px(REF, bpr, f, ax, ay) ^ (f == OGPU_FMT_ARGB32 ? 0xFFFFFFUL : f == OGPU_FMT_RGB565 ? 0xFFFFUL : 0xFF)); }
        else if (set) setpx(REF, bpr, f, ax, ay, fg);
        else if (mode & OGPU_JAM2) setpx(REF, bpr, f, ax, ay, bg);
    }
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, f); ogpu_target(&B, 0);
    ogpu_clip(&B, 0, 0, 40, H);
    ogpu_template(&B, D0, (unsigned long)tbpr, (unsigned long)first, tx, ty, tw, 8, fg, bg, mode);
    ogpu_clip(&B, 0, 0, W, 10);
    ogpu_template(&B, D0 + 8 * tbpr, (unsigned long)tbpr, (unsigned long)first, tx, ty + 8, tw, th - 8, fg, bg, mode);
    ogpu_clip(&B, 0, 10, 40, H - 10);
    ogpu_template(&B, D0 + 8 * tbpr, (unsigned long)tbpr, (unsigned long)first, tx, ty + 8, tw, th - 8, fg, bg, mode);
    run();
    CHECK(C.last_error == OGPU_OK && memcmp(arena + T0, arena + REF, (size_t)(bpr * H)) == 0, "template fmt %d mode %d", f, mode);
}

static void test_pattern(void) {
    long bpr = W;
    int x, y;
    static const unsigned short pat[4] = { 0xAAAA, 0x5555, 0xF0F0, 0x0001 };
    for (y = 0; y < 4; y++) { arena[D0 + y * 2] = (ogpu_u8)(pat[y] >> 8); arena[D0 + y * 2 + 1] = (ogpu_u8)pat[y]; }
    noise(T0, bpr * H);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    for (y = 7; y < 30; y++) for (x = 3; x < 50; x++) {
        int set = (pat[y & 3] >> (15 - (x & 15))) & 1;
        setpx(REF, bpr, OGPU_FMT_CLUT8, x, y, set ? 9 : 2);
    }
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
    ogpu_pattern(&B, D0, 4, 3, 7, 47, 23, 9, 2, OGPU_JAM2);
    run();
    CHECK(C.last_error == OGPU_OK && memcmp(arena + T0, arena + REF, (size_t)(bpr * H)) == 0, "pattern");
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
    ogpu_pattern(&B, D0, 3, 0, 0, 4, 4, 1, 0, OGPU_JAM1);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "pattern rows must be a power of two");
}

static void test_lines(void) {
    long bpr = W;
    int i;
    for (i = 0; i < 200; i++) {
        int x0 = (int)(rnd() % 90) - 13, y0 = (int)(rnd() % 60) - 10, x1 = (int)(rnd() % 90) - 13, y1 = (int)(rnd() % 60) - 10;
        int dx = abs(x1 - x0), dy = abs(y1 - y0), n = 0, x, y, inside = 0;
        memset(arena + T0, 0, (size_t)(bpr * H));
        begin();
        ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
        ogpu_line(&B, x0, y0, x1, y1, 1, OGPU_JAM1);
        run();
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) if (arena[T0 + y * bpr + x]) n++;
        /* Count the pixels of the whole line that fall inside, by a second
         * walk along its major axis. */
        {
            int steps = dx > dy ? dx : dy, k;
            for (k = 0; k <= steps; k++) {
                long num_x = (long)(x1 - x0) * k, num_y = (long)(y1 - y0) * k;
                int px_ = x0 + (int)(steps ? (num_x >= 0 ? (2 * num_x + steps) / (2 * steps) : -((-2 * num_x + steps) / (2 * steps))) : 0);
                int py_ = y0 + (int)(steps ? (num_y >= 0 ? (2 * num_y + steps) / (2 * steps) : -((-2 * num_y + steps) / (2 * steps))) : 0);
                if (px_ >= 0 && py_ >= 0 && px_ < W && py_ < H) inside++;
            }
        }
        CHECK(abs(n - inside) <= 1 + (dx > dy ? dx : dy) / 16, "line %d,%d-%d,%d: %d pixels, about %d expected", x0, y0, x1, y1, n, inside);
        if (x0 >= 0 && y0 >= 0 && x0 < W && y0 < H) CHECK(arena[T0 + y0 * bpr + x0] == 1, "line start drawn");
        if (x1 >= 0 && y1 >= 0 && x1 < W && y1 < H) CHECK(arena[T0 + y1 * bpr + x1] == 1, "line end drawn");
        if (failures > 20) return;
    }
    /* COMPLEMENT twice gives back what was there. */
    noise(T0, bpr * H);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
    ogpu_line(&B, 0, 0, 63, 39, 0, OGPU_COMPLEMENT);
    ogpu_line(&B, 0, 0, 63, 39, 0, OGPU_COMPLEMENT);
    run();
    CHECK(memcmp(arena + T0, arena + REF, (size_t)(bpr * H)) == 0, "complemented line twice");
}

static void test_pixels(void) {
    long bpr = W * 2;
    unsigned long i;
    /* ARGB32 to RGB565, and INDEX8 through a table. */
    for (i = 0; i < 4; i++) {
        static const unsigned long c[4] = { 0xFFFF0000UL, 0xFF00FF00UL, 0xFF0000FFUL, 0xFF808080UL };
        setpx(D0, 16, OGPU_FMT_ARGB32, (int)i, 0, c[i]);
    }
    for (i = 0; i < 256; i++) setpx(D0 + 0x1000, 0, OGPU_FMT_ARGB32, (int)i, 0, 0xFF000000UL | (i << 16) | ((255 - i) << 8));
    for (i = 0; i < 4; i++) arena[D0 + 0x2000 + i] = (ogpu_u8)(i * 85);
    memset(arena + T0, 0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_RGB565); ogpu_target(&B, 0);
    ogpu_pixels(&B, D0, 16, OGPU_FMT_ARGB32, 0, 0, 0, 4, 1);
    ogpu_pixels(&B, D0 + 0x2000, 4, OGPU_FMT_INDEX8, D0 + 0x1000, 0, 1, 4, 1);
    run();
    CHECK(C.last_error == OGPU_OK, "pixels ran");
    CHECK(px(T0, bpr, OGPU_FMT_RGB565, 0, 0) == 0xF800 && px(T0, bpr, OGPU_FMT_RGB565, 1, 0) == 0x07E0
          && px(T0, bpr, OGPU_FMT_RGB565, 2, 0) == 0x001F && px(T0, bpr, OGPU_FMT_RGB565, 3, 0) == 0x8410, "ARGB to RGB565");
    CHECK(px(T0, bpr, OGPU_FMT_RGB565, 0, 1) == 0x07E0 && px(T0, bpr, OGPU_FMT_RGB565, 3, 1) == 0xF800, "INDEX8 through a table");
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_RGB565); ogpu_target(&B, 0);
    ogpu_pixels(&B, D0, 4, OGPU_FMT_CLUT8, 0, 0, 0, 4, 1);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "bare pens on a true-colour target are refused");
}

static void test_composite(void) {
    long bpr = W * 4;
    unsigned long d;
    /* Half alpha, no scaling: grey over black. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    setpx(S0, 16, OGPU_FMT_ARGB32, 0, 0, 0xFFC8C8C8UL);
    setpx(S0, 16, OGPU_FMT_ARGB32, 1, 0, 0x80FF0000UL);
    setpx(S0, 16, OGPU_FMT_ARGB32, 2, 0, 0xFF000000UL);
    setpx(S0, 16, OGPU_FMT_ARGB32, 3, 0, 0xFFFFFFFFUL);
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 16, 4, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 128, 0);
    ogpu_composite(&B, 1, 1, 0, 1, 1, 1, 0, 1, 1, 255, OGPU_COMP_SRCALPHA);
    ogpu_composite(&B, 1, 0, 0, 2, 1, 0, 2, 8, 2, 255, 0);            /* nearest, x4 and x2 */
    ogpu_composite(&B, 1, 2, 0, 2, 1, 0, 5, 4, 1, 255, OGPU_COMP_BILINEAR);
    run();
    CHECK(C.last_error == OGPU_OK, "composite ran (%d)", C.last_error);
    d = px(T0, bpr, OGPU_FMT_ARGB32, 0, 0);
    CHECK(d == 0x80646464UL, "half of C8 over black is 64 (got %08lx)", d);
    d = px(T0, bpr, OGPU_FMT_ARGB32, 1, 0);
    CHECK(d == 0x80800000UL, "source alpha 80 (got %08lx)", d);
    CHECK(px(T0, bpr, OGPU_FMT_ARGB32, 3, 2) == 0xFFC8C8C8UL && px(T0, bpr, OGPU_FMT_ARGB32, 4, 3) == 0xFFFF0000UL,
          "nearest scaling");
    /* Bilinear: 0 to 255 over 4 pixels: 0, 0x3F or 0x40, 0xBF or 0xC0, 255. */
    d = px(T0, bpr, OGPU_FMT_ARGB32, 0, 5) & 255;
    CHECK(d == 0, "bilinear left edge (%02lx)", d);
    d = px(T0, bpr, OGPU_FMT_ARGB32, 1, 5) & 255;
    CHECK(d >= 0x3E && d <= 0x41, "bilinear quarter (%02lx)", d);
    d = px(T0, bpr, OGPU_FMT_ARGB32, 3, 5) & 255;
    CHECK(d == 0xFF, "bilinear right edge (%02lx)", d);
}

/* ---- v1.1: A8, MASK, and COMPOSITE's MASK, ADD and IN ---------------------------------- */

static unsigned long m255(unsigned long x, unsigned long y) { return (x * y + 127) / 255; }

static void test_a8(void) {
    long bpr = W + 3;
    int x, y, bad = 0;
    /* FILL, COPY and A8 PIXELS on an A8 surface are plain bytes. */
    noise(T0, bpr * H);
    for (x = 0; x < 16; x++) arena[D0 + x] = (ogpu_u8)(x * 17);
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_A8); ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, W, H, 0x40);
    ogpu_pixels(&B, D0, 16, OGPU_FMT_A8, 0, 2, 2, 16, 1);
    ogpu_copy(&B, 0, 2, 2, 2, 3, 16, 1);
    run();
    CHECK(C.last_error == OGPU_OK, "A8 ran (%d)", C.last_error);
    for (x = 0; x < 16; x++) bad += px(T0, bpr, OGPU_FMT_A8, 2 + x, 3) != (unsigned long)(x * 17);
    CHECK(!bad && px(T0, bpr, OGPU_FMT_A8, 0, 0) == 0x40, "A8 fill, pixels and copy");
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_A8); ogpu_target(&B, 0);
    ogpu_pixels(&B, D0, 64, OGPU_FMT_ARGB32, 0, 0, 0, 4, 1);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "colours don't go into A8");
    begin();
    ogpu_surface(&B, 0, T0, W * 4, W, H, OGPU_FMT_ARGB32); ogpu_target(&B, 0);
    ogpu_pixels(&B, D0, 16, OGPU_FMT_A8, 0, 0, 0, 4, 1);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "coverage doesn't go into colours");
    (void)y;
}

/* MASK: a colour through an A8 mask, against a per-pixel reference. */
static void test_mask(int f) {
    long bpr = W * bpp_of(f) + 2, mbpr = 37;
    unsigned long colour = 0xC0FF8020UL;
    int x, y, bad = 0;
    noise(T0, bpr * H);
    noise(D0, mbpr * 30);
    memcpy(arena + REF, arena + T0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, f); ogpu_target(&B, 0);
    ogpu_clip(&B, 4, 2, 50, 30);
    ogpu_mask(&B, D0, (unsigned long)mbpr, -3, 1, 36, 30, colour);
    run();
    CHECK(C.last_error == OGPU_OK, "mask fmt %d ran (%d)", f, C.last_error);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long d = px(REF, bpr, f, x, y), want = d;
            if (x >= 4 && x < 33 && y >= 2 && y < 31) {
                unsigned long m = arena[D0 + (y - 1) * mbpr + (x + 3)], ea = m255(m, 0xC0), da, dr, dg, db;
                if (f == OGPU_FMT_RGB565) {
                    dr = (d >> 11) & 31; dr = (dr << 3) | (dr >> 2); dg = (d >> 5) & 63; dg = (dg << 2) | (dg >> 4);
                    db = d & 31; db = (db << 3) | (db >> 2); da = 255;
                } else if (f == OGPU_FMT_A8) { da = d; dr = dg = db = 0; }
                else { da = d >> 24; dr = (d >> 16) & 255; dg = (d >> 8) & 255; db = d & 255; }
                if (ea == 255) { dr = 0xFF; dg = 0x80; db = 0x20; da = 255; }
                else if (ea) {
                    dr = m255(0xFF, ea) + m255(dr, 255 - ea); dg = m255(0x80, ea) + m255(dg, 255 - ea);
                    db = m255(0x20, ea) + m255(db, 255 - ea); da = ea + m255(da, 255 - ea);
                }
                if (dr > 255) dr = 255;
                if (dg > 255) dg = 255;
                if (db > 255) db = 255;
                if (da > 255) da = 255;
                want = f == OGPU_FMT_RGB565 ? ((dr >> 3) << 11) | ((dg >> 2) << 5) | (db >> 3)
                     : f == OGPU_FMT_A8 ? da : (da << 24) | (dr << 16) | (dg << 8) | db;
            }
            if (px(T0, bpr, f, x, y) != want && bad++ < 3)
                printf("  mask fmt %d at %d,%d: %08lx, want %08lx\n", f, x, y, px(T0, bpr, f, x, y), want);
        }
    CHECK(!bad, "mask fmt %d matches the reference (%d differ)", f, bad);
}

/* COMPOSITE with a mask, ADD and IN, into ARGB32 and A8. */

/* ---- v1.2: blend modes, FILL_BLEND, LINES_BLEND, POINTS_BLEND, COMPOSITE_AFFINE, YUV, formats ---- */

static unsigned long r255(unsigned long x, unsigned long y) { unsigned long t = x * y + 128; return (t + (t >> 8)) >> 8; }
static unsigned long sat(unsigned long v) { return v > 255 ? 255 : v; }

/* SDL's blend modes, written plainly, channel by channel (ARGB32 words). */
static unsigned long ref_blend(unsigned long d, unsigned long s, unsigned long sa, int mode) {
    unsigned long da = d >> 24 & 255, dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
    unsigned long sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255, a, r, g, b;
    switch (mode) {
    case OGPU_BLEND_NONE: return sa << 24 | sr << 16 | sg << 8 | sb;
    case OGPU_BLEND_ADD:
        if (!sa) return d;
        a = da; r = sat(dr + r255(sr, sa)); g = sat(dg + r255(sg, sa)); b = sat(db + r255(sb, sa)); break;
    case OGPU_BLEND_MOD: a = da; r = r255(sr, dr); g = r255(sg, dg); b = r255(sb, db); break;
    case OGPU_BLEND_MUL:
        a = da; r = sat(r255(sr, dr) + r255(dr, 255 - sa)); g = sat(r255(sg, dg) + r255(dg, 255 - sa)); b = sat(r255(sb, db) + r255(db, 255 - sa));
        break;
    default:
        if (sa == 255) return 0xFF000000UL | (s & 0xFFFFFF);
        if (!sa) return d;
        a = sat(sa + r255(da, 255 - sa)); r = sat(r255(sr, sa) + r255(dr, 255 - sa));
        g = sat(r255(sg, sa) + r255(dg, 255 - sa)); b = sat(r255(sb, sa) + r255(db, 255 - sa));
    }
    return a << 24 | r << 16 | g << 8 | b;
}

static void test_fill_blend(void) {
    static const unsigned long dsts[3] = { 0xFF204060UL, 0x80FFFFFFUL, 0x00000000UL };
    static const unsigned long cols[4] = { 0x80FF8000UL, 0xFF102030UL, 0x00FFFFFFUL, 0x40A0B0C0UL };
    long bpr = W * 4;
    int m, i, k;
    for (m = 0; m <= OGPU_BLEND_MUL; m++)
        for (i = 0; i < 3; i++)
            for (k = 0; k < 4; k++) {
                unsigned long want = ref_blend(dsts[i], cols[k], cols[k] >> 24, m), got;
                setpx(T0, bpr, OGPU_FMT_ARGB32, 3, 3, dsts[i]);
                begin();
                ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
                ogpu_target(&B, 0);
                ogpu_fill_blend(&B, 3, 3, 1, 1, cols[k], m);
                run();
                got = px(T0, bpr, OGPU_FMT_ARGB32, 3, 3);
                CHECK(C.last_error == OGPU_OK && got == want, "FILL_BLEND mode %d: %08lx on %08lx gave %08lx, want %08lx", m, cols[k], dsts[i], got, want);
            }
    /* Into RGB565: the same channels, truncated; alpha kept out. */
    begin();
    ogpu_surface(&B, 0, T0, W * 2, W, H, OGPU_FMT_RGB565);
    ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, 4, 1, 0x0000);
    ogpu_fill_blend(&B, 0, 0, 4, 1, 0xFFFF8040UL, OGPU_BLEND_BLEND);
    run();
    CHECK(px(T0, W * 2, OGPU_FMT_RGB565, 2, 0) == 0xFC08UL, "FILL_BLEND into RGB565 (%04lx)", px(T0, W * 2, OGPU_FMT_RGB565, 2, 0));
    begin();
    ogpu_surface(&B, 0, T0, W, W, H, OGPU_FMT_CLUT8);
    ogpu_target(&B, 0);
    ogpu_fill_blend(&B, 0, 0, 4, 1, 0xFFFF8040UL, OGPU_BLEND_BLEND);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "FILL_BLEND has no CLUT8");
}

static void test_lines_points(void) {
    long bpr = W * 4;
    int x, y, n;
    ogpu_u8 *pts = arena + D0;
    /* A strip round a square, last points left out: each corner once, so a
     * half-transparent ADD gives every pixel the same value. */
    static const int sq[5][2] = { { 2, 2 }, { 12, 2 }, { 12, 12 }, { 2, 12 }, { 2, 2 } };
    for (n = 0; n < 5; n++) { pts[n * 4] = 0; pts[n * 4 + 1] = (ogpu_u8)sq[n][0]; pts[n * 4 + 2] = 0; pts[n * 4 + 3] = (ogpu_u8)sq[n][1]; }
    memset(arena + T0, 0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_lines_blend(&B, D0, 5, 0x80402010UL, OGPU_BLEND_ADD | OGPU_LINES_STRIP);
    run();
    n = 0;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            unsigned long v = px(T0, bpr, OGPU_FMT_ARGB32, x, y);
            if (v) { n++; CHECK(v == 0x00201008UL, "strip pixel %d,%d is %08lx", x, y, v); }
        }
    CHECK(C.last_error == OGPU_OK && n == 40, "a strip round a 10x10 square sets 40 pixels once (%d)", n);
    /* Pairs with their last points: two crossing lines. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_lines_blend(&B, D0, 4, 0xFFFFFFFFUL, OGPU_BLEND_NONE | OGPU_LINES_LAST);
    run();
    CHECK(px(T0, bpr, OGPU_FMT_ARGB32, 12, 12) == 0xFFFFFFFFUL && px(T0, bpr, OGPU_FMT_ARGB32, 2, 12) == 0xFFFFFFFFUL
          && px(T0, bpr, OGPU_FMT_ARGB32, 7, 7) == 0 , "pairs, each with its last point");
    /* Points, clipped. */
    pts[0] = 0xFF; pts[1] = 0xFF; pts[2] = 0; pts[3] = 1;          /* (-1, 1): outside */
    memset(arena + T0, 0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_points_blend(&B, D0, 3, 0xFF00FF00UL, OGPU_BLEND_BLEND);
    run();
    CHECK(C.last_error == OGPU_OK && px(T0, bpr, OGPU_FMT_ARGB32, 12, 2) == 0xFF00FF00UL
          && px(T0, bpr, OGPU_FMT_ARGB32, 12, 12) == 0xFF00FF00UL, "points");
}

static void test_affine(void) {
    long bpr = W * 4, m[6];
    int x, y, ok;
    /* A 4x3 source of distinct opaque pixels. */
    for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) setpx(S0, 16, OGPU_FMT_ARGB32, x, y, 0xFF000000UL | (unsigned long)(y * 4 + x + 1) * 0x0A0B0CUL);
    /* Identity, moved to (5, 6): the same as a 1:1 copy. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    m[0] = 65536; m[1] = 0; m[2] = 5L << 16; m[3] = 0; m[4] = 65536; m[5] = 6L << 16;
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 16, 4, 3, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 4, 3, m, 0xFFFFFFFFUL, OGPU_BLEND_BLEND);
    run();
    ok = C.last_error == OGPU_OK;
    for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) ok &= px(T0, bpr, OGPU_FMT_ARGB32, 5 + x, 6 + y) == px(S0, 16, OGPU_FMT_ARGB32, x, y);
    ok &= px(T0, bpr, OGPU_FMT_ARGB32, 4, 6) == 0 && px(T0, bpr, OGPU_FMT_ARGB32, 9, 6) == 0 && px(T0, bpr, OGPU_FMT_ARGB32, 5, 9) == 0;
    CHECK(ok, "affine identity is a copy");
    /* A quarter turn: source (u, v) lands at (20 - v, 10 + u): x = -v + 20, y = u + 10. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    m[0] = 0; m[1] = -65536; m[2] = 20L << 16; m[3] = 65536; m[4] = 0; m[5] = 10L << 16;
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 16, 4, 3, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 4, 3, m, 0xFFFFFFFFUL, OGPU_BLEND_NONE);
    run();
    ok = C.last_error == OGPU_OK;
    for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) ok &= px(T0, bpr, OGPU_FMT_ARGB32, 19 - y, 10 + x) == px(S0, 16, OGPU_FMT_ARGB32, x, y);
    CHECK(ok, "affine quarter turn");
    /* A horizontal flip at twice the size, through a colour: red halved, alpha 128. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    m[0] = -2L * 65536; m[1] = 0; m[2] = 8L << 16; m[3] = 0; m[4] = 2L * 65536; m[5] = 0;
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 16, 4, 3, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 4, 3, m, 0x807FFFFFUL, OGPU_BLEND_NONE);
    run();
    {
        unsigned long s = px(S0, 16, OGPU_FMT_ARGB32, 0, 1), want = 0x80000000UL | r255(s >> 16 & 255, 0x7F) << 16 | (s & 0xFFFF);
        CHECK(C.last_error == OGPU_OK && px(T0, bpr, OGPU_FMT_ARGB32, 7, 2) == want && px(T0, bpr, OGPU_FMT_ARGB32, 6, 3) == want,
              "affine flip x2 with colour (%08lx, want %08lx)", px(T0, bpr, OGPU_FMT_ARGB32, 7, 2), want);
    }
    /* Too small to invert, and a flat matrix: refused, and nothing. */
    m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = 1; m[5] = 0;
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 16, 4, 3, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 4, 3, m, 0xFFFFFFFFUL, 0);
    m[0] = 65536; m[1] = 65536; m[3] = 65536; m[4] = 65536;
    ogpu_composite_affine(&B, 1, 0, 0, 4, 3, m, 0xFFFFFFFFUL, 0);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "a vanishing scale is refused");
}

static void test_yuv(void) {
    long bpr = W * 4;
    int x, y, i, ok;
    ogpu_u8 *yp = arena + D0, *up = yp + 0x1000, *vp = yp + 0x2000, *uv = yp + 0x3000, *pk = yp + 0x4000;
    unsigned long ref[8][4];
    /* An 8x4 picture: Y ramps, U and V per 2x2 block. Packed and semi-planar copies of it. */
    for (y = 0; y < 4; y++) for (x = 0; x < 8; x++) yp[y * 8 + x] = (ogpu_u8)(16 + x * 25 + y * 3);
    for (y = 0; y < 2; y++) for (x = 0; x < 4; x++) { up[y * 4 + x] = (ogpu_u8)(64 + x * 40); vp[y * 4 + x] = (ogpu_u8)(200 - y * 90 - x * 10); }
    for (y = 0; y < 2; y++) for (x = 0; x < 4; x++) { uv[y * 8 + x * 2] = up[y * 4 + x]; uv[y * 8 + x * 2 + 1] = vp[y * 4 + x]; }
    for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) {
        ogpu_u8 *q = pk + y * 16 + x * 4;
        q[0] = yp[y * 8 + x * 2]; q[1] = up[(y / 2) * 4 + x]; q[2] = yp[y * 8 + x * 2 + 1]; q[3] = vp[(y / 2) * 4 + x];
    }
    memset(arena + T0, 0, (size_t)(bpr * H));
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_yuv(&B, D0, 8, D0 + 0x1000, 4, D0 + 0x2000, 4, 8, 4, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_I420, OGPU_YUV_BT601));
    ogpu_yuv(&B, D0, 8, D0 + 0x3000, 8, 0, 0, 8, 4, 10, 0, OGPU_YUV_FORMAT(OGPU_YUV_NV12, OGPU_YUV_BT601));
    ogpu_yuv(&B, D0 + 0x4000, 16, 0, 0, 0, 0, 8, 4, 20, 0, OGPU_YUV_FORMAT(OGPU_YUV_YUY2, OGPU_YUV_BT601));
    run();
    ok = C.last_error == OGPU_OK;
    for (y = 0; y < 4; y++) for (x = 0; x < 8; x++) {
        /* BT.601 in floating point, as the reference. */
        double Y = yp[y * 8 + x] - 16, U = up[(y / 2) * 4 + x / 2] - 128, V = vp[(y / 2) * 4 + x / 2] - 128;
        double r = 1.164 * Y + 1.596 * V, g = 1.164 * Y - 0.392 * U - 0.813 * V, b = 1.164 * Y + 2.017 * U;
        unsigned long got = px(T0, bpr, OGPU_FMT_ARGB32, x, y);
        int gr = (int)(got >> 16 & 255), gg = (int)(got >> 8 & 255), gb = (int)(got & 255);
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        ok &= (got >> 24) == 255 && gr - r < 2 && r - gr < 2 && gg - g < 2 && g - gg < 2 && gb - b < 2 && b - gb < 2;
        if (y < 4 && x < 4) ref[x][y] = got;
        ok &= px(T0, bpr, OGPU_FMT_ARGB32, 10 + x, y) == got && px(T0, bpr, OGPU_FMT_ARGB32, 20 + x, y) == got;
    }
    (void)ref;
    CHECK(ok, "YUV I420, NV12 and YUY2 give BT.601's colours, the same each way");
    /* Video range: 16 is black, 235 white. */
    for (i = 0; i < 32; i++) yp[i] = i < 16 ? 16 : 235;
    memset(up, 128, 8); memset(vp, 128, 8);
    begin();
    ogpu_surface(&B, 0, T0, bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_yuv(&B, D0, 8, D0 + 0x1000, 4, D0 + 0x2000, 4, 8, 4, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_I420, OGPU_YUV_BT601));
    run();
    CHECK(px(T0, bpr, OGPU_FMT_ARGB32, 0, 0) == 0xFF000000UL && px(T0, bpr, OGPU_FMT_ARGB32, 0, 2) == 0xFFFFFFFFUL, "black and white");
}

/* The v1.2 formats: each drawn the same as RGB565 or ARGB32, in its own bytes. */
static void test_formats_v12(void) {
    static const int fmts[4] = { OGPU_FMT_RGB565PC, OGPU_FMT_RGB555, OGPU_FMT_RGB555PC, OGPU_FMT_BGRA32 };
    int k, x;
    for (k = 0; k < 4; k++) {
        int f = fmts[k], bpp = f == OGPU_FMT_BGRA32 ? 4 : 2;
        long bpr = W * bpp;
        unsigned long got[4], want[4];
        memset(arena + T0, 0, (size_t)(bpr * H));
        setpx(S0, 16, OGPU_FMT_ARGB32, 0, 0, 0xFFF08040UL);
        setpx(S0, 16, OGPU_FMT_ARGB32, 1, 0, 0x8020C0E0UL);
        begin();
        ogpu_surface(&B, 0, T0, bpr, W, H, f);
        ogpu_surface(&B, 1, S0, 16, 2, 1, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_fill_blend(&B, 0, 0, 8, 1, 0xFF336699UL, OGPU_BLEND_NONE);
        ogpu_composite(&B, 1, 0, 0, 2, 1, 2, 0, 2, 1, 255, OGPU_COMP_SRCALPHA);
        ogpu_pixels(&B, S0, 16, OGPU_FMT_ARGB32, 0, 4, 0, 2, 1);
        run();
        for (x = 0; x < 4; x++) {
            const ogpu_u8 *p = arena + T0 + (x * 2 + 1) * bpp;   /* pixels 1, 3, 5, 7 */
            unsigned long r, g, b;
            if (f == OGPU_FMT_BGRA32) { got[x] = (unsigned long)p[2] << 16 | (unsigned long)p[1] << 8 | p[0]; }
            else {
                unsigned v = f == OGPU_FMT_RGB555 ? (unsigned)(p[0] << 8 | p[1]) : (unsigned)(p[1] << 8 | p[0]);
                if (f == OGPU_FMT_RGB565PC) { r = v >> 11 & 31; g = v >> 5 & 63; b = v & 31; got[x] = r << 16 | g << 8 | b; }
                else { r = v >> 10 & 31; g = v >> 5 & 31; b = v & 31; got[x] = r << 16 | g << 8 | b; }
            }
        }
        /* The same through ARGB32, cut to the format's bits. */
        memset(arena + T0, 0, (size_t)(W * 4 * H));
        begin();
        ogpu_surface(&B, 0, T0, W * 4, W, H, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 1, S0, 16, 2, 1, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_fill_blend(&B, 0, 0, 8, 1, 0xFF336699UL, OGPU_BLEND_NONE);
        ogpu_composite(&B, 1, 0, 0, 2, 1, 2, 0, 2, 1, 255, OGPU_COMP_SRCALPHA);
        ogpu_pixels(&B, S0, 16, OGPU_FMT_ARGB32, 0, 4, 0, 2, 1);
        run();
        for (x = 0; x < 4; x++) {
            unsigned long v = px(T0, W * 4, OGPU_FMT_ARGB32, x * 2 + 1, 0), r = v >> 16 & 255, g = v >> 8 & 255, b = v & 255;
            if (f == OGPU_FMT_BGRA32) want[x] = v & 0xFFFFFF;
            else if (f == OGPU_FMT_RGB565PC) want[x] = (r >> 3) << 16 | (g >> 2) << 8 | (b >> 3);
            else want[x] = (r >> 3) << 16 | (g >> 3) << 8 | (b >> 3);
        }
        /* The composite blends with what was there, read back from fewer bits:
         * pixel 3 may differ by the rounding of the format; the rest exactly. */
        CHECK(got[0] == want[0] && got[2] == want[2] && got[3] == want[3], "format %d: %06lx %06lx %06lx, want %06lx %06lx %06lx",
              f, got[0], got[2], got[3], want[0], want[2], want[3]);
    }
}

static void test_composite_v11(void) {
    long bpr = W * 4, abpr = W;
    unsigned long d;
    int x, y, bad = 0;
    /* A grey source through a mask ramp onto black. */
    memset(arena + T0, 0, (size_t)(bpr * H));
    for (x = 0; x < 16; x++) setpx(S0, 64, OGPU_FMT_ARGB32, x, 0, 0xFFC8C8C8UL);
    for (x = 0; x < 16; x++) arena[D0 + x] = (ogpu_u8)(x * 17);
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 64, 16, 1, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 2, D0, 16, 16, 1, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_composite_masked(&B, 1, 0, 0, 16, 1, 0, 0, 16, 1, 255, 0, 2, 0, 0);
    run();
    CHECK(C.last_error == OGPU_OK, "masked composite ran (%d)", C.last_error);
    for (x = 0; x < 16; x++) {
        unsigned long m = (unsigned long)x * 17, c = m255(0xC8, m), want = (m << 24) | (c << 16) | (c << 8) | c;
        if (x == 15) want = 0xFFC8C8C8UL;
        bad += px(T0, bpr, OGPU_FMT_ARGB32, x, 0) != want;
    }
    CHECK(!bad, "masked composite follows the mask");
    /* ADD and IN on A8: combining clip masks. */
    for (x = 0; x < 8; x++) { arena[S0 + 0x1000 + x] = (ogpu_u8)(x * 32); arena[T0 + x] = 200; }
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)abpr, W, H, OGPU_FMT_A8);
    ogpu_surface(&B, 1, S0 + 0x1000, 8, 8, 1, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 8, 1, 0, 0, 8, 1, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_ADD);
    run();
    CHECK(C.last_error == OGPU_OK, "ADD ran");
    for (x = 0; x < 8; x++) {
        unsigned long want = 200 + (unsigned long)x * 32;
        if (want > 255) want = 255;
        bad += px(T0, abpr, OGPU_FMT_A8, x, 0) != want;
    }
    CHECK(!bad, "A8 ADD saturates");
    for (x = 0; x < 8; x++) arena[T0 + x] = 200;
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)abpr, W, H, OGPU_FMT_A8);
    ogpu_surface(&B, 1, S0 + 0x1000, 8, 8, 1, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 8, 1, 0, 0, 8, 1, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_IN);
    run();
    for (x = 0; x < 8; x++) bad += px(T0, abpr, OGPU_FMT_A8, x, 0) != m255(200, (unsigned long)x * 32);
    CHECK(C.last_error == OGPU_OK && !bad, "A8 IN multiplies");
    /* The errors: ADD with IN; a mask that isn't A8 or doesn't cover the rect; a short command. */
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 64, 16, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, OGPU_COMP_ADD | OGPU_COMP_IN);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "ADD with IN refused");
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 64, 16, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite_masked(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, 0, 1, 0, 0);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "an ARGB mask refused");
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 64, 16, 1, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 2, D0, 16, 16, 1, OGPU_FMT_A8);
    ogpu_target(&B, 0);
    ogpu_composite_masked(&B, 1, 0, 0, 16, 1, 0, 0, 16, 2, 255, 0, 2, 0, 0);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "a mask smaller than the rect refused");
    begin();
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, S0, 64, 16, 1, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 1, 1, 0, 0, 1, 1, 255, OGPU_COMP_MASK);
    run();
    CHECK(C.last_error == OGPU_ERR_BADLEN, "a masked composite without its mask words");
    d = (unsigned long)ogpu_core_supports(OGPU_OP_MASK, OGPU_FMT_ARGB32);
    CHECK(d == OGPU_FULL && ogpu_core_supports(OGPU_OP_MASK, OGPU_FMT_CLUT8) == OGPU_NONE
          && ogpu_core_supports(OGPU_OP_COMPOSITE, OGPU_FMT_A8) == OGPU_FULL
          && ogpu_core_supports(OGPU_OP_PIXELS, OGPU_FMT_A8) == OGPU_PARTIAL, "v1.1 supports");
    (void)y;
}

static void test_errors(void) {
    unsigned char *p;
    begin();
    ogpu_fill(&B, 0, 0, 1, 1, 1);
    run();
    CHECK(C.last_error == OGPU_ERR_NOSURFACE, "no target");
    begin();
    ogpu_surface(&B, 0, ARENA - 10, 64, 64, 64, OGPU_FMT_CLUT8);
    run();
    CHECK(C.last_error == OGPU_ERR_NOMAP, "a surface past the arena can't be mapped");
    /* An unknown opcode is skipped; the fence after it still comes. */
    begin();
    p = sbuf;
    p[0] = 0x7F; p[1] = 0x00; p[2] = 0x00; p[3] = 0x03;
    B.words = 3;
    ogpu_fence(&B, 77);
    CHECK(run() == 2 && C.last_error == OGPU_ERR_BADOP && nfences == 1 && fences[0] == 77, "unknown opcode skipped");
    /* A length past the end stops the run. */
    begin();
    ogpu_fence(&B, 1);
    sbuf[3] = 9;
    run();
    CHECK(C.last_error == OGPU_ERR_BADLEN && nfences == 0, "length past the end");
    /* A one-word FILL stops the run: the FENCE after it never comes. */
    begin();
    ogpu_surface(&B, 0, T0, W, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
    p = sbuf + B.words * 4;
    p[0] = 0; p[1] = OGPU_OP_FILL; p[2] = 0; p[3] = 1;
    B.words += 1;
    ogpu_fence(&B, 5);
    run();
    CHECK(C.last_error == OGPU_ERR_BADLEN && nfences == 0, "a command shorter than its arguments stops the run");
    /* Spans past 32 bits are refused, not wrapped (they would be on the 68k). */
    begin();
    ogpu_surface(&B, 0, 0, 0x01000000UL, 1, 257, OGPU_FMT_CLUT8);
    run();
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "a surface spanning more than 4 GiB");
    /* A clip that ends at 0 (left of the target) draws nothing, not everything. */
    memset(arena + T0, 7, W * H);
    begin();
    ogpu_surface(&B, 0, T0, W, W, H, OGPU_FMT_CLUT8); ogpu_target(&B, 0);
    ogpu_clip(&B, -10, 0, 10, H);
    ogpu_fill(&B, 0, 0, W, H, 1);
    run();
    CHECK(C.last_error == OGPU_OK && arena[T0] == 7 && arena[T0 + W * H - 1] == 7, "a clip ending at 0 draws nothing");
    /* COPY between two slots on the same memory is overlap-safe too. */
    {
        int r, ok = 1;
        for (r = 0; r < 4; r++) memset(arena + T0 + r * W, r + 1, W);
        begin();
        ogpu_surface(&B, 0, T0, W, W, H, OGPU_FMT_CLUT8);
        ogpu_surface(&B, 1, T0, W, W, H, OGPU_FMT_CLUT8);
        ogpu_target(&B, 0);
        ogpu_copy(&B, 1, 0, 0, 0, 1, W, 3);
        run();
        for (r = 1; r < 4; r++) ok &= arena[T0 + r * W] == r;
        CHECK(C.last_error == OGPU_OK && ok, "copy down through an aliased slot");
    }
    /* A batch that runs out of room says so. */
    {
        unsigned char small[16];
        struct OGPUBatch b;
        ogpu_batch_init(&b, small, 4);
        ogpu_fill(&b, 0, 0, 1, 1, 0);
        ogpu_fill(&b, 0, 0, 1, 1, 0);
        CHECK(b.overflow && b.words == 4, "overflow");
    }
}

/* Random streams: must never touch memory outside what map() handed out
 * (run under AddressSanitizer by tests/run.sh). */
static void test_random_streams(void) {
    int round;
    for (round = 0; round < 3000; round++) {
        long i, n = 1 + (long)(rnd() % 200);
        ogpu_u8 *p = sbuf;
        begin();
        ogpu_surface(&B, (int)(rnd() % 4), T0 + (long)(rnd() % 4096), 1 + rnd() % 400, (int)(rnd() % 120), (int)(rnd() % 90), 1 + (int)(rnd() % 3));
        ogpu_target(&B, (int)(rnd() % 4));
        for (i = B.words * 4; i < (B.words + n) * 4; i++) p[i] = (ogpu_u8)rnd();
        /* Mostly real opcodes with plausible lengths. */
        for (i = B.words; i < B.words + n; ) {
            static const int ops[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x20, 0x01, 0x02, 0x03, 0xF0 };
            static const int lens[] = { 4, 4, 5, 9, 8, 5, 7, 8, 6, 2, 3, 2 };
            int k = (int)(rnd() % 12), len = lens[k] + (rnd() % 8 == 0 ? (int)(rnd() % 3) - 1 : 0);
            if (len < 1) len = 1;
            if (i + len > B.words + n) break;
            p[i * 4] = 0; p[i * 4 + 1] = (ogpu_u8)ops[k]; p[i * 4 + 2] = 0; p[i * 4 + 3] = (ogpu_u8)len;
            /* Keep coordinates small most of the time so drawing happens. */
            {
                long j;
                for (j = 1; j < len; j++) if (rnd() % 4) { p[(i + j) * 4] = 0; p[(i + j) * 4 + 2] = 0; p[(i + j) * 4 + 1] &= 0x7F; }
            }
            i += len;
        }
        B.words += n;
        run();
    }
    CHECK(1, "random streams");
}

/* ---- the golden scene ----------------------------------------------------------------- */

static long scene_run(void *user, const unsigned char *stream, long words) {
    (void)user;
    ogpu_core_init(&C);
    C.map = map; C.fence = on_fence; C.user = 0;
    nfences = 0;
    ogpu_core_run(&C, stream, words);
    CHECK(nfences == 1, "the scene's fence");
    return C.last_error;
}

static int golden_check(const char *path, unsigned long got, const char *what) {
    FILE *f = fopen(path, "r");
    unsigned long want = 0;
    CHECK(f && fscanf(f, "%lx", &want) == 1, "golden file %s", path);
    if (f) fclose(f);
    CHECK(got == want, "%s checksum %08lx, golden file says %08lx", what, got, want);
    return got == want;
}

/* OGPU_OP_VIRGL: BADOP with no hook; with one, called in stream order with the whole command. */
static long ext_calls, ext_order, ext_addr, ext_bytes;
static int on_ext(void *user, int op, const ogpu_u8 *cmd, long words) {
    (void)user;
    ext_calls++;
    ext_order = nfences;                    /* fences seen before it */
    ext_addr = (long)(((unsigned long)cmd[4] << 24) | ((unsigned long)cmd[5] << 16) | ((unsigned long)cmd[6] << 8) | cmd[7]);
    ext_bytes = (long)(((unsigned long)cmd[8] << 24) | ((unsigned long)cmd[9] << 16) | ((unsigned long)cmd[10] << 8) | cmd[11]);
    return op == OGPU_OP_VIRGL && words == 3 ? OGPU_OK : OGPU_ERR_BADLEN;
}
static void test_ext(void) {
    begin(); ogpu_fence(&B, 1); ogpu_virgl(&B, 0x1000, 256); ogpu_fence(&B, 2);
    run();
    CHECK(C.last_error == OGPU_ERR_BADOP && C.error_word == 2 && nfences == 2, "VIRGL with no hook: BADOP, the rest runs");
    begin(); ogpu_fence(&B, 1); ogpu_virgl(&B, 0x1000, 256); ogpu_fence(&B, 2);
    ogpu_core_init(&C);
    C.map = map; C.fence = on_fence; C.ext = on_ext; C.user = 0;
    nfences = 0; ext_calls = 0;
    ogpu_core_run(&C, sbuf, B.words);
    CHECK(C.last_error == OGPU_OK && ext_calls == 1 && ext_order == 1 && ext_addr == 0x1000 && ext_bytes == 256 && nfences == 2,
          "VIRGL with a hook: called once, in order, with its words");
    C.ext = 0;
    CHECK(ogpu_core_supports(OGPU_OP_VIRGL, 0) == OGPU_NONE, "the core answers NONE for VIRGL");
}

int main(int argc, char **argv) {
    static const int fmts[3] = { OGPU_FMT_CLUT8, OGPU_FMT_RGB565, OGPU_FMT_ARGB32 };
    unsigned long golden, golden11, golden12;
    int k;
    for (k = 0; k < 3; k++) {
        test_fill(fmts[k]);
        test_copy_overlap(fmts[k], 5, 3); test_copy_overlap(fmts[k], -5, -3);
        test_copy_overlap(fmts[k], 7, -2); test_copy_overlap(fmts[k], -7, 2);
        test_copy_overlap(fmts[k], 3, 0); test_copy_overlap(fmts[k], 0, 0);
        test_template(fmts[k], OGPU_JAM1); test_template(fmts[k], OGPU_JAM2);
        test_template(fmts[k], OGPU_COMPLEMENT); test_template(fmts[k], OGPU_JAM2 | OGPU_INVERSVID);
    }
    test_copy_between();
    test_pattern();
    test_lines();
    test_pixels();
    test_composite();
    test_a8();
    test_mask(OGPU_FMT_RGB565); test_mask(OGPU_FMT_ARGB32); test_mask(OGPU_FMT_A8);
    test_composite_v11();
    test_fill_blend();
    test_lines_points();
    test_affine();
    test_yuv();
    test_formats_v12();
    test_errors();
    test_ext();
    test_random_streams();
    {
        struct ogpu_scene_env env;
        memset(&env, 0, sizeof env);
        env.arena = arena; env.addr = 0; env.run = scene_run;
        golden = ogpu_golden_g1(&env);
        golden11 = ogpu_golden_v11(&env);
        golden12 = ogpu_golden_v12(&env);
        CHECK(env.failures == 0, "the golden scenes ran (%d failed)", env.failures);
    }
    if (argc > 1) golden_check(argv[1], golden, "golden scene");
    if (argc > 2) golden_check(argv[2], golden11, "v1.1 scene");
    if (argc > 3) golden_check(argv[3], golden12, "v1.2 scene");
    CHECK(ogpu_core_supports(OGPU_OP_COMPOSITE, OGPU_FMT_CLUT8) == OGPU_NONE
          && ogpu_core_supports(OGPU_OP_PIXELS, OGPU_FMT_CLUT8) == OGPU_PARTIAL
          && ogpu_core_supports(OGPU_OP_FILL, OGPU_FMT_ARGB32) == OGPU_FULL, "supports");
    printf("opengpu core: golden scene %08lx, v1.1 scene %08lx, v1.2 scene %08lx; %s\n", golden, golden11, golden12,
           failures ? "FAILED" : "all tests passed");
    return failures != 0;
}
