/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The Vulkan back end against the C core on what SDL 2's renderer and
 * OpenRTG's Workbench send, timed (GPU-3's measurements, before any change).
 *
 * Each scenario is a list of batches, built as the 68k side builds them
 * (OpenRTG: one batch per graphics call, waited for; SDL: one batch per
 * RunCommandQueue, then the window's present). Every batch is one run, as
 * one ring segment is in the runtime: ogpu_core_run on one copy of the
 * memory, ogpu_vk_run on another, each timed with the host's clock. After
 * each scenario both copies must be byte for byte the same (2D is exact).
 *
 * Memory is laid out as the runtime's is: video RAM (16 MiB, page aligned,
 * handed to the back end as host_vram, so the GPU draws in place: "import"
 * on RADV, "map" on V3DV), and Fast RAM beside it, which only the core's
 * map reaches (the back end copies what it needs into its arena).
 *
 * Prints one line per scenario: microseconds per batch on the core and on
 * the back end, and how many of the back end's commands the GPU drew, how
 * many fell to its core, and how many command buffers it sent.
 *   vk_2d_bench [REPEAT-SCALE]   (1 by default; 0.1 for a quick look) */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../host/vulkan/ogpu_vk.h"
#include "../../include/opengpu/build.h"

#define VBASE   0x40000000UL           /* video RAM's Amiga address */
#define VSIZE   (16UL << 20)
#define RBASE   0x08000000UL           /* Fast RAM's (Zorro III) */
#define RSIZE   (32UL << 20)

/* Places in video RAM */
#define SCREEN  (VBASE + 0x10000)      /* the Workbench screen, 1920 x 1080 */
#define SW 1920
#define SH 1080
#define VTEX    (VBASE + 0x900000)     /* SDL's textures and target, when they are in video RAM */
#define VWIN    (VBASE + 0xA00000)
/* Places in Fast RAM */
#define FONT    (RBASE + 0x000000)     /* 1-bit glyphs, 256 x 8 pixels a row */
#define PAT     (RBASE + 0x001000)
#define TABLE   (RBASE + 0x002000)     /* 256 ARGB colours */
#define CHUNKY  (RBASE + 0x010000)     /* 128 x 128 pens */
#define ARGBPIX (RBASE + 0x020000)     /* 128 x 128 ARGB */
#define FTEX    (RBASE + 0x100000)     /* SDL's sprite, 32 x 32 ARGB with alpha, and its background, 256 x 256 */
#define FBG     (RBASE + 0x110000)
#define FWIN    (RBASE + 0x200000)     /* SDL's window surface, 640 x 480 ARGB */
#define BACKING (RBASE + 0x400000)     /* a smart refresh window's hidden part, 640 x 480 */

static ogpu_u8 *vram_c, *ram_c, *vram_v, *ram_v;
static struct ogpu_core C;
static struct ogpu_vk *V;
static double scale = 1.0;
static int failures;

static ogpu_u8 *map_c(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a >= VBASE && a - VBASE <= VSIZE && len <= VSIZE - (a - VBASE)) return vram_c + (a - VBASE);
    if (a >= RBASE && a - RBASE <= RSIZE && len <= RSIZE - (a - RBASE)) return ram_c + (a - RBASE);
    return 0;
}
static ogpu_u8 *map_v(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a >= RBASE && a - RBASE <= RSIZE && len <= RSIZE - (a - RBASE)) return ram_v + (a - RBASE);
    return 0;
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

/* ---- batches -------------------------------------------------------------------- */

#define MAXB 2048
static unsigned char *bufs[MAXB];
static long words[MAXB];
static int nb;
static struct OGPUBatch B;

static void batch(void) {
    bufs[nb] = malloc(1 << 16);
    ogpu_batch_init(&B, bufs[nb], (1 << 16) / 4);
    nb++;
}
static void end(void) { words[nb - 1] = B.words; if (B.overflow) { printf("FAIL: batch overflow\n"); failures++; } }
static void clear_batches(void) { int i; for (i = 0; i < nb; i++) free(bufs[i]); nb = 0; }

static void screen(int slot, int fmt) {
    ogpu_surface(&B, slot, SCREEN, (unsigned long)SW * (fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 1), SW, SH, fmt);
}

/* The batches `reps` times on each, timed; then the two memories compared. */
static void run(const char *name, int reps) {
    struct ogpu_vk_stats s0, s1;
    double t, tc, tv;
    int r, i;
    long n = 0;
    reps = (int)(reps * scale);
    if (reps < 1) reps = 1;
    t = now_us();
    for (r = 0; r < reps; r++)
        for (i = 0; i < nb; i++) ogpu_core_run(&C, bufs[i], words[i]);
    tc = now_us() - t;
    ogpu_vk_stats(V, &s0);
    t = now_us();
    for (r = 0; r < reps; r++)
        for (i = 0; i < nb; i++) ogpu_vk_run(V, bufs[i], words[i]);
    tv = now_us() - t;
    ogpu_vk_stats(V, &s1);
    n = (long)reps * nb;
    printf("%-44s %8.1f %8.1f %6.2fx  %6.1f %6.1f %6.2f\n", name, tc / n, tv / n, tc / tv,
           (double)(s1.gpu - s0.gpu) / n, (double)(s1.cpu - s0.cpu) / n, (double)(s1.submits - s0.submits) / n);
    if (C.last_error != OGPU_OK || ogpu_vk_last_error(V) != OGPU_OK) {
        printf("FAIL %s: error %d (core) %d (vulkan)\n", name, C.last_error, ogpu_vk_last_error(V));
        failures++;
    }
    if (memcmp(vram_c, vram_v, VSIZE)) {
        long k;
        for (k = 0; k < (long)VSIZE && vram_c[k] == vram_v[k]; k++) ;
        printf("FAIL %s: video RAM differs first at +%lx\n", name, k);
        failures++;
        memcpy(vram_v, vram_c, VSIZE);
    }
    if (memcmp(ram_c, ram_v, RSIZE)) {
        printf("FAIL %s: Fast RAM differs\n", name);
        failures++;
        memcpy(ram_v, ram_c, RSIZE);
    }
    clear_batches();
}

/* ---- data ----------------------------------------------------------------------- */

static unsigned long rs = 2026;
static unsigned rnd(void) { rs = (rs * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (unsigned)(rs >> 8); }

static void both(unsigned long a, const void *p, unsigned long len) {
    if (a >= VBASE) { memcpy(vram_c + (a - VBASE), p, len); memcpy(vram_v + (a - VBASE), p, len); }
    else { memcpy(ram_c + (a - RBASE), p, len); memcpy(ram_v + (a - RBASE), p, len); }
}

static void fill_noise(unsigned long a, unsigned long len, int argb_alpha) {
    unsigned char *p = malloc(len);
    unsigned long i;
    for (i = 0; i < len; i++) p[i] = (unsigned char)rnd();
    if (argb_alpha) for (i = 0; i < len; i += 4) p[i] = (unsigned char)(i / 4 % 3 == 0 ? 0 : i / 4 % 3 == 1 ? 255 : p[i]);
    both(a, p, len);
    free(p);
}

/* ---- scenarios ------------------------------------------------------------------- */

/* OpenRTG: one graphics call, one batch (ogpu_begin: SURFACE, TARGET, the command). */
static void workbench(int fmt, const char *tag) {
    char name[64];
    int i;
    unsigned long white = fmt == OGPU_FMT_ARGB32 ? 0x00FFFFFFUL : fmt == OGPU_FMT_RGB565 ? 0xFFFF : 2;
    unsigned long blue = fmt == OGPU_FMT_ARGB32 ? 0x00305080UL : fmt == OGPU_FMT_RGB565 ? 0x328F : 3;

    snprintf(name, sizeof name, "%s RectFill 1x1 (the floor)", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_fill(&B, 5, 5, 1, 1, white); end();
    run(name, 2000);
    snprintf(name, sizeof name, "%s RectFill 8x8", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_fill(&B, 40, 40, 8, 8, blue); end();
    run(name, 2000);
    snprintf(name, sizeof name, "%s RectFill 300x200", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_fill(&B, 100, 100, 300, 200, blue); end();
    run(name, 500);
    snprintf(name, sizeof name, "%s SetRast (whole screen)", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_fill(&B, 0, 0, SW, SH, white); end();
    run(name, 100);
    snprintf(name, sizeof name, "%s RectFill COMPLEMENT 300x200", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_invert(&B, 100, 100, 300, 200, fmt == OGPU_FMT_ARGB32 ? 0xFFFFFF : fmt == OGPU_FMT_RGB565 ? 0xFFFF : 0xFF); end();
    run(name, 500);
    snprintf(name, sizeof name, "%s RectFill area pattern 300x200", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_pattern(&B, PAT, 2, 100, 320, 300, 200, blue, white, OGPU_JAM2); end();
    run(name, 500);
    snprintf(name, sizeof name, "%s Text 40 characters (one batch)", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0);
    for (i = 0; i < 40; i++) ogpu_template(&B, FONT, 256, (unsigned long)(i * 8 % 2048), 100 + i * 8, 600, 8, 8, blue, white, OGPU_JAM1);
    end();
    run(name, 500);
    snprintf(name, sizeof name, "%s Draw 100 lines across or down", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0);
    for (i = 0; i < 100; i++) ogpu_line(&B, 500 + i, 100, 500 + i, 140 + i, blue, OGPU_JAM1);
    end();
    run(name, 200);
    snprintf(name, sizeof name, "%s ClipBlit 300x100 in the screen", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_copy(&B, 0, 100, 100, 600, 400, 300, 100); end();
    run(name, 500);
    snprintf(name, sizeof name, "%s ScrollRaster 1000x600 by 8 (overlaps)", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0); ogpu_copy(&B, 0, 200, 208, 200, 200, 1000, 600); end();
    run(name, 100);
    snprintf(name, sizeof name, "%s WriteChunkyPixels 128x128 (Fast RAM)", tag);
    batch(); screen(0, fmt); ogpu_target(&B, 0);
    if (fmt == OGPU_FMT_CLUT8) ogpu_pixels(&B, CHUNKY, 128, OGPU_FMT_CLUT8, 0, 1200, 100, 128, 128);
    else ogpu_pixels(&B, CHUNKY, 128, OGPU_FMT_INDEX8, TABLE, 1200, 100, 128, 128);
    end();
    run(name, 500);
    if (fmt == OGPU_FMT_ARGB32) {
        /* WritePixelArray ARGB onto a 32-bit OpenRTG screen: XRGB32 (v1.3) */
        batch(); ogpu_surface(&B, 0, SCREEN, SW * 4, SW, SH, OGPU_FMT_XRGB32); ogpu_target(&B, 0);
        ogpu_pixels(&B, ARGBPIX, 512, OGPU_FMT_ARGB32, 0, 1400, 100, 128, 128); end();
        run("32 WritePixelArray ARGB 128x128 (XRGB32)", 500);
        /* WritePixelArrayAlpha: COMPOSITE SRCALPHA from Fast RAM */
        batch(); screen(0, fmt); ogpu_surface(&B, 1, ARGBPIX, 512, 128, 128, OGPU_FMT_ARGB32); ogpu_target(&B, 0);
        ogpu_composite(&B, 1, 0, 0, 128, 128, 1400, 300, 128, 128, 255, OGPU_COMP_SRCALPHA); end();
        run("32 WritePixelArrayAlpha 128x128", 500);
    }
    /* A smart refresh window moved: its hidden part out to Fast RAM, then back
     * (layers' BltBitMap both ways; the backing bitmap is a friend, in Fast RAM). */
    snprintf(name, sizeof name, "%s window 640x480: screen to backing", tag);
    {
        int bpp = fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 1;
        batch(); screen(0, fmt); ogpu_surface(&B, 1, BACKING, (unsigned long)640 * bpp, 640, 480, fmt);
        ogpu_target(&B, 1); ogpu_copy(&B, 0, 300, 300, 0, 0, 640, 480); end();
        run(name, 100);
        snprintf(name, sizeof name, "%s window 640x480: backing to screen", tag);
        batch(); screen(0, fmt); ogpu_surface(&B, 1, BACKING, (unsigned long)640 * bpp, 640, 480, fmt);
        ogpu_target(&B, 0); ogpu_copy(&B, 1, 0, 0, 340, 300, 640, 480); end();
        run(name, 100);
    }
}

/* SDL's testsprite2: a clear, 100 sprites 32x32 with alpha, in one batch;
 * the window (640x480 ARGB) where SDL keeps it, the sprites with it. */
static void sprites(unsigned long win, unsigned long tex, unsigned long flags, const char *name) {
    int i;
    batch();
    ogpu_surface(&B, 0, win, 640 * 4, 640, 480, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, tex, 32 * 4, 32, 32, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_clip(&B, 0, 0, 0, 0);
    ogpu_fill(&B, 0, 0, 640, 480, 0xFFA0A0A0UL);
    for (i = 0; i < 100; i++)
        ogpu_composite(&B, 1, 0, 0, 32, 32, (int)(rnd() % 600), (int)(rnd() % 440), 32, 32, 255, flags);
    end();
    run(name, 200);
}

static void sdl(void) {
    const unsigned long blend12 = OGPU_COMP_SRCALPHA | OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(OGPU_BLEND_BLEND);
    const long rot[6] = { 56756, -32768, 200L << 16, 32768, 56756, 60L << 16 };   /* 30 degrees, at 200, 60 */
    sprites(FWIN, FTEX, blend12, "SDL sprites, Fast RAM, v1.2 BLEND (today)");
    sprites(FWIN, FTEX, OGPU_COMP_SRCALPHA, "SDL sprites, Fast RAM, v1.1 OVER");
    sprites(VWIN, VTEX, OGPU_COMP_SRCALPHA, "SDL sprites, video RAM, v1.1 OVER");
    sprites(VWIN, VTEX, blend12, "SDL sprites, video RAM, v1.2 BLEND");
    /* The present: the window surface into the 32-bit screen (one COPY) */
    batch(); screen(0, OGPU_FMT_ARGB32); ogpu_surface(&B, 1, FWIN, 640 * 4, 640, 480, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0); ogpu_copy(&B, 1, 0, 0, 200, 200, 640, 480); end();
    run("SDL present 640x480, Fast RAM to screen", 200);
    batch(); screen(0, OGPU_FMT_ARGB32); ogpu_surface(&B, 1, VWIN, 640 * 4, 640, 480, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0); ogpu_copy(&B, 1, 0, 0, 200, 200, 640, 480); end();
    run("SDL present 640x480, video RAM to screen", 200);
    /* testrendercopyex: the background stretched (bilinear), then a rotated sprite */
    batch(); ogpu_surface(&B, 0, VWIN, 640 * 4, 640, 480, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, FBG, 256 * 4, 256, 256, OGPU_FMT_ARGB32); ogpu_target(&B, 0);
    ogpu_composite(&B, 1, 0, 0, 256, 256, 0, 0, 640, 480, 255, OGPU_COMP_BILINEAR); end();
    run("SDL background 256 to 640x480, bilinear", 200);
    batch(); ogpu_surface(&B, 0, VWIN, 640 * 4, 640, 480, OGPU_FMT_ARGB32);
    ogpu_surface(&B, 1, FBG, 256 * 4, 256, 256, OGPU_FMT_ARGB32); ogpu_target(&B, 0);
    ogpu_composite_affine(&B, 1, 0, 0, 128, 128, rot, 0xFFFFFFFFUL, OGPU_BLEND_BLEND | OGPU_AFF_SRCALPHA | OGPU_AFF_BILINEAR); end();
    run("SDL RenderCopyEx 128x128 rotated (AFFINE)", 200);
    batch(); ogpu_surface(&B, 0, VWIN, 640 * 4, 640, 480, OGPU_FMT_ARGB32); ogpu_target(&B, 0);
    ogpu_fill_blend(&B, 50, 50, 300, 200, 0x80FF4020UL, OGPU_BLEND_BLEND); end();
    run("SDL blended fill 300x200 (FILL_BLEND)", 200);
}

int main(int argc, char **argv) {
    struct ogpu_vk_config cfg;
    char err[200];
    void *h = 0;
    if (argc > 1) scale = atof(argv[1]);
    if (scale <= 0) scale = 1;
    vram_c = calloc(1, VSIZE); ram_c = calloc(1, RSIZE); ram_v = calloc(1, RSIZE);
    if (!vram_c || !ram_c || !ram_v || posix_memalign(&h, 65536, VSIZE)) { printf("FAIL: no memory\n"); return 1; }
    memset(h, 0, VSIZE);
    ogpu_core_init(&C);
    C.map = map_c;
    memset(&cfg, 0, sizeof cfg);
    cfg.vram_base = VBASE;
    cfg.vram_size = VSIZE;
    cfg.map = map_v;
    cfg.host_vram = h;                 /* as the runtime does: arena_size 0 (4 MiB) */
    V = ogpu_vk_create(&cfg, err, sizeof err);
    if (!V) { printf("vk_2d_bench: no Vulkan back end (%s)\n", err); return 2; }
    vram_v = ogpu_vk_vram(V);
    printf("vk_2d_bench: %s, video RAM %s; microseconds per batch (one batch = one ring run)\n",
           ogpu_vk_device(V), ogpu_vk_vram_mode(V));
    fill_noise(FONT, 0x1000, 0);
    fill_noise(PAT, 8, 0);
    fill_noise(TABLE, 1024, 0);
    fill_noise(CHUNKY, 128 * 128, 0);
    fill_noise(ARGBPIX, 128 * 128 * 4, 1);
    fill_noise(FTEX, 32 * 32 * 4, 1);
    fill_noise(VTEX, 32 * 32 * 4, 1);
    fill_noise(FBG, 256 * 256 * 4, 1);
    printf("%-44s %8s %8s %7s  %6s %6s %6s\n", "scenario", "core", "vulkan", "speed", "gpu", "cpu", "submit");
    workbench(OGPU_FMT_ARGB32, "32");
    workbench(OGPU_FMT_CLUT8, "8");
    sdl();
    ogpu_vk_destroy(V);
    printf("vk_2d_bench: %s (%d differences)\n", failures ? "FAILED" : "same pixels", failures);
    return failures ? 1 : 0;
}
