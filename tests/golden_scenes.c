/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's golden scenes (golden/opengpu-g1.txt, golden/opengpu-v11.txt),
 * built once for the host test (test_opengpu.c, the C core) and for
 * OpenGPUCheck on an Amiga (tools/ogpu_check.c, through opengpu.library and
 * its driver). test_opengpu_vk.c keeps its own copy, which draws parts of
 * the scene from the board's video RAM. The
 * scene's memory is one arena; e->addr is the address the back end knows
 * arena[0] by (0 on the host, the arena itself on an Amiga). */
#include <string.h>
#include "golden_scenes.h"
#include "../include/opengpu/build.h"

#define T0 0x1000L      /* target */
#define S0 0x40000L     /* a second surface */
#define D0 0x80000L     /* data: templates, patterns, pixels */
#define A(off) (e->addr + (unsigned long)(off))
#define SCHECK(cond, ...) do { if (!(cond)) e->failures++; } while (0)

static ogpu_u8 *arena;
static unsigned char sbuf[1 << 14];
static struct OGPUBatch B;
static void begin(void) { ogpu_batch_init(&B, sbuf, sizeof sbuf / 4); }
static int bpp_of(int f) { return f == OGPU_FMT_RGB565 ? 2 : f == OGPU_FMT_ARGB32 ? 4 : 1; }
static void setpx(long base, long bpr, int f, int x, int y, unsigned long v) {
    ogpu_u8 *p = arena + base + y * bpr + x * bpp_of(f);
    if (f == OGPU_FMT_RGB565) { p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; }
    else if (f == OGPU_FMT_ARGB32) { p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }
    else p[0] = (ogpu_u8)v;
}
static unsigned long rnd_state;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }
static void noise(long base, long bytes) { long i; for (i = 0; i < bytes; i++) arena[base + i] = (ogpu_u8)rnd(); }

unsigned long ogpu_fnv(const ogpu_u8 *p, long n, unsigned long h) {
    while (n--) { h ^= *p++; h = (h * 16777619UL) & 0xFFFFFFFFUL; }
    return h;
}

unsigned long ogpu_golden_g1(struct ogpu_scene_env *e) {
    arena = e->arena;
    static const int fmts[3] = { OGPU_FMT_CLUT8, OGPU_FMT_RGB565, OGPU_FMT_ARGB32 };
    unsigned long h = 2166136261UL;
    int k, i;
    rnd_state = 2026;
    noise(D0, 0x4000);
    for (i = 0; i < 64 * 48; i++) setpx(S0, 64 * 4, OGPU_FMT_ARGB32, i % 64, i / 64, ((unsigned long)(i * 37) & 255) << 24 | (unsigned long)(i * 2654435761UL & 0xFFFFFFUL));
    for (k = 0; k < 3; k++) {
        int f = fmts[k];
        long bpr = 160 * bpp_of(f);
        unsigned long white = f == OGPU_FMT_CLUT8 ? 2 : f == OGPU_FMT_RGB565 ? 0xFFFF : 0xFFFFFFFFUL;
        unsigned long blue = f == OGPU_FMT_CLUT8 ? 3 : f == OGPU_FMT_RGB565 ? 0x2B5F : 0xFF2A5DB0UL;
        memset(arena + T0, 0, (size_t)(bpr * 100));
        begin();
        ogpu_surface(&B, 0, A(T0), (unsigned long)bpr, 160, 100, f);
        ogpu_surface(&B, 1, A(S0), 64 * 4, 64, 48, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_fill(&B, 0, 0, 160, 100, white);
        ogpu_fill(&B, 0, 0, 160, 11, blue);
        ogpu_template(&B, A(D0), 20, 3, 4, 2, 150, 8, 1, 0, OGPU_JAM1);
        ogpu_pattern(&B, A(D0) + 0x100, 8, 10, 20, 60, 30, blue, white, OGPU_JAM2);
        for (i = 0; i < 12; i++) ogpu_line(&B, 80, 55, 80 + (i - 6) * 13, i & 1 ? 99 : 12, blue, OGPU_JAM1);
        ogpu_invert(&B, 100, 60, 40, 30, f == OGPU_FMT_ARGB32 ? 0x00FFFFFFUL : 0xFFFF);
        ogpu_copy(&B, 0, 0, 0, 30, 70, 80, 25);
        ogpu_clip(&B, 2, 2, 150, 90);
        if (f != OGPU_FMT_CLUT8) {
            ogpu_composite(&B, 1, 0, 0, 64, 48, 90, 15, 96, 72, 200, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR);
            ogpu_composite(&B, 1, 8, 8, 16, 16, 5, 60, 40, 30, 255, 0);
            ogpu_pixels(&B, A(S0), 64 * 4, OGPU_FMT_ARGB32, 0, 120, 70, 64, 48);
        } else {
            ogpu_pixels(&B, A(D0) + 0x200, 32, OGPU_FMT_CLUT8, 0, 120, 70, 32, 32);
        }
        ogpu_fence(&B, (unsigned long)k);
        { long r = e->run(e->user, sbuf, B.words); SCHECK(r == OGPU_OK, "golden scene fmt %d ran (%ld)", f, r); }
        h = ogpu_fnv(arena + T0, bpr * 100, h);
    }
    return h;
}

/* v1.1's scene: anti-aliased "glyphs" (MASK), a rounded clip (masked
 * COMPOSITE) and clip masks combined with ADD and IN. */
unsigned long ogpu_golden_v11(struct ogpu_scene_env *e) {
    arena = e->arena;
    static const int fmts[2] = { OGPU_FMT_RGB565, OGPU_FMT_ARGB32 };
    unsigned long h = 2166136261UL;
    int k, i, x, y;
    rnd_state = 1101;
    for (y = 0; y < 48; y++)        /* a disc's coverage, soft-edged */
        for (x = 0; x < 64; x++) {
            long dx = x * 2 - 63, dy = y * 2 - 47, r2 = dx * dx + dy * dy, c = (2200 - r2) / 4;
            arena[D0 + 0x2000 + y * 64 + x] = (ogpu_u8)(c < 0 ? 0 : c > 255 ? 255 : c);
        }
    for (i = 0; i < 64 * 48; i++) arena[D0 + 0x3000 + i] = (ogpu_u8)((i * 7) ^ (i >> 5));
    for (i = 0; i < 64 * 48; i++) setpx(S0, 64 * 4, OGPU_FMT_ARGB32, i % 64, i / 64, 0xFF000000UL | (unsigned long)(i * 2654435761UL & 0xFFFFFFUL));
    for (k = 0; k < 2; k++) {
        int f = fmts[k];
        long bpr = 160 * bpp_of(f);
        memset(arena + T0, 0, (size_t)(bpr * 100));
        memset(arena + T0 + 0x10000, 0, 64 * 48);
        begin();
        ogpu_surface(&B, 0, A(T0), (unsigned long)bpr, 160, 100, f);
        ogpu_surface(&B, 1, A(S0), 64 * 4, 64, 48, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 2, A(D0) + 0x2000, 64, 64, 48, OGPU_FMT_A8);
        ogpu_surface(&B, 3, A(D0) + 0x3000, 64, 64, 48, OGPU_FMT_A8);
        ogpu_surface(&B, 4, A(T0) + 0x10000, 64, 64, 48, OGPU_FMT_A8);
        /* Combine two clip masks in slot 4: disc ADD noise, then IN the disc. */
        ogpu_target(&B, 4);
        ogpu_composite(&B, 2, 0, 0, 64, 48, 0, 0, 64, 48, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_ADD);
        ogpu_composite(&B, 3, 0, 0, 64, 48, 0, 0, 64, 48, 128, OGPU_COMP_SRCALPHA | OGPU_COMP_ADD);
        ogpu_composite(&B, 2, 0, 0, 64, 48, 0, 0, 64, 48, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_IN);
        ogpu_target(&B, 0);
        ogpu_fill(&B, 0, 0, 160, 100, f == OGPU_FMT_RGB565 ? 0xC618 : 0xFFC0C0C0UL);
        for (i = 0; i < 6; i++) ogpu_mask(&B, A(D0) + 0x3000 + (unsigned long)i * 3, 64, 4 + i * 25, 4, 20, 14, 0xFF000000UL | (unsigned long)(i * 0x2A1F37));
        ogpu_composite_masked(&B, 1, 0, 0, 64, 48, 20, 30, 64, 48, 255, 0, 4, 0, 0);
        ogpu_clip(&B, 90, 25, 60, 70);
        ogpu_composite_masked(&B, 1, 0, 0, 32, 24, 80, 30, 64, 48, 200, OGPU_COMP_BILINEAR, 2, 0, 0);
        ogpu_fence(&B, (unsigned long)k);
        { long r = e->run(e->user, sbuf, B.words); SCHECK(r == OGPU_OK, "v1.1 scene fmt %d ran (%ld)", f, r); }
        h = ogpu_fnv(arena + T0, bpr * 100, h);
        h = ogpu_fnv(arena + T0 + 0x10000, 64 * 48, h);
    }
    return h;
}

