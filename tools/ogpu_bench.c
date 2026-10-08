/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPUBench: how fast OpenGPU's C core draws on this 68k, command by
 * command, beside a plain C loop over the same memory, so a slow command
 * shows as the core's fault and not the machine's. The same batches go
 * through opengpu.library too (its driver, when there is one).
 *   OpenGPUBench [CHIP|FAST|ANY]      (where the pictures live; ANY by default) */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/opengpu.h>
#include <stdio.h>
#include <string.h>

#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/opengpu.h"
#include "../include/opengpu/build.h"

struct Library *OpenGPUBase;
struct Device *TimerBase;
static struct timerequest treq;

#define W 320
#define H 240
#define SRC_AT 0x60000UL

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) { (void)user; (void)length; return (ogpu_u8 *)address; }

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

static unsigned char buf[4096];
static struct OGPUBatch B;
static ogpu_u8 *arena;

static void begin(int format)
{
    ogpu_batch_init(&B, buf, sizeof buf / 4);
    ogpu_surface(&B, 0, (unsigned long)arena, W * (format == OGPU_FMT_RGB565 ? 2 : 4), W, H, format);
    ogpu_surface(&B, 1, (unsigned long)arena + SRC_AT, 64 * 4, 64, 48, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
}

static struct ogpu_core c;
static double time_core(int n)
{
    double t0 = now_ms();
    int i;
    for (i = 0; i < n; i++) {
        ogpu_core_init(&c);
        c.map = map;
        ogpu_core_run(&c, buf, B.words);
    }
    return (now_ms() - t0) / n;
}

static double time_lib(int n)
{
    double t0 = now_ms();
    ULONG f;
    int i;
    for (i = 0; i < n; i++) { OGPU_Submit(buf, B.words, &f); OGPU_Wait(f); }
    return (now_ms() - t0) / n;
}

static int c_error(void) { return c.last_error; }

static void row(const char *what, long pixels, int n)
{
    double c = time_core(n), l = OpenGPUBase ? time_lib(n) : 0;
    printf("%-34s %9.3f ms %8.1f ns/px", what, c, c * 1e6 / pixels);
    if (OGPU_OK != (int)c_error()) printf(" (error %d)", c_error());
    if (OpenGPUBase) printf("   library %8.3f ms", l);
    {
        unsigned long h = 2166136261UL;
        long k;
        for (k = 0; k < W * H * 4L; k++) h = (h ^ arena[k]) * 16777619UL;
        printf("   [%08lx]", h & 0xFFFFFFFFUL);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    ULONG req = MEMF_ANY;
    int i, n = 5;
    double t0, t;
    if (argc > 1 && !strcmp(argv[1], "CHIP")) req = MEMF_CHIP;
    if (argc > 1 && !strcmp(argv[1], "FAST")) req = MEMF_FAST;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) { printf("no timer\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    if (!(arena = AllocVec(0x80000, req | MEMF_CLEAR))) { printf("no memory\n"); return 20; }
    OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0);
    printf("pictures at %08lx (%s RAM)\n", (unsigned long)arena, TypeOfMem(arena) & MEMF_CHIP ? "Chip" : "Fast");
    if (OpenGPUBase) printf("library back end: %s\n", (char *)OGPU_BackEndName(OGPU_BACKEND(OGPU_Query(OGPU_OP_FILL, OGPU_FMT_ARGB32))));
    for (i = 0; i < 64 * 48; i++) ((ULONG *)(arena + SRC_AT))[i] = 0x80000000UL | (ULONG)(i * 2654435761UL & 0xFFFFFF);

    t0 = now_ms();
    for (i = 0; i < n; i++) { ULONG *p = (ULONG *)arena; long k; for (k = 0; k < W * H; k++) p[k] = 0xFF336699UL; }
    t = (now_ms() - t0) / n;
    printf("%-34s %9.3f ms %8.1f ns/px\n", "plain C: 320x240 longs stored", t, t * 1e6 / (W * H));

    begin(OGPU_FMT_ARGB32); ogpu_fill(&B, 0, 0, W, H, 0xFF336699UL); row("FILL 320x240 ARGB32", W * H, n);
    begin(OGPU_FMT_RGB565); ogpu_fill(&B, 0, 0, W, H, 0x1234); row("FILL 320x240 RGB565", W * H, n);
    begin(OGPU_FMT_ARGB32); ogpu_copy(&B, 0, 0, 0, 8, 8, W - 8, H - 8); row("COPY 312x232 ARGB32", (W - 8) * (H - 8), n);
    begin(OGPU_FMT_ARGB32); ogpu_pixels(&B, (unsigned long)arena + SRC_AT, 256, OGPU_FMT_ARGB32, 0, 10, 10, 64, 48); row("PIXELS 64x48 ARGB32", 64 * 48, n);
    begin(OGPU_FMT_ARGB32); ogpu_composite(&B, 1, 0, 0, 64, 48, 0, 0, 64, 48, 255, 0); row("COMPOSITE 64x48 1:1 opaque", 64 * 48, n);
    begin(OGPU_FMT_ARGB32); ogpu_composite(&B, 1, 0, 0, 64, 48, 0, 0, 64, 48, 200, OGPU_COMP_SRCALPHA); row("COMPOSITE 64x48 1:1 alpha", 64 * 48, n);
    begin(OGPU_FMT_ARGB32); ogpu_composite(&B, 1, 0, 0, 64, 48, 20, 20, 256, 192, 200, OGPU_COMP_SRCALPHA); row("COMPOSITE 256x192 scaled", 256 * 192, n);
    begin(OGPU_FMT_ARGB32); ogpu_composite(&B, 1, 0, 0, 64, 48, 20, 20, 256, 192, 200, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR); row("COMPOSITE 256x192 bilinear", 256 * 192, n);
    begin(OGPU_FMT_ARGB32); for (i = 0; i < 20; i++) ogpu_line(&B, 160, 120, i * 16, i & 1 ? 0 : 239, 0xFFFFFFFFUL, OGPU_JAM1); row("20 LINEs", 20 * 240, n);
    begin(OGPU_FMT_ARGB32); ogpu_template(&B, (unsigned long)arena + SRC_AT, 40, 0, 0, 0, 320, 48, 0xFFFFFFFFUL, 0, OGPU_JAM2); row("TEMPLATE 320x48 JAM2", 320 * 48, n);

    begin(OGPU_FMT_ARGB32); ogpu_fill_blend(&B, 0, 0, W, H, 0x80336699UL, OGPU_BLEND_BLEND); row("FILL_BLEND 320x240 half (v1.2)", W * H, n);
    {
        long m[6];
        m[0] = 42566 * 3; m[1] = -24576 * 3; m[3] = 24576 * 3; m[4] = 42566 * 3; m[2] = 140L << 16; m[5] = 10L << 16;
        begin(OGPU_FMT_ARGB32); ogpu_composite_affine(&B, 1, 0, 0, 64, 48, m, 0xFFFFFFFFUL, OGPU_BLEND_BLEND | OGPU_AFF_SRCALPHA);
        row("COMPOSITE_AFFINE x3, 30 deg (v1.2)", 192 * 144, n);
        begin(OGPU_FMT_ARGB32); ogpu_composite_affine(&B, 1, 0, 0, 64, 48, m, 0xFFFFFFFFUL, OGPU_BLEND_BLEND | OGPU_AFF_SRCALPHA | OGPU_AFF_BILINEAR);
        row("  the same, bilinear", 192 * 144, n);
    }
    begin(OGPU_FMT_ARGB32);
    ogpu_yuv(&B, (unsigned long)arena + SRC_AT, 64, (unsigned long)arena + SRC_AT + 0x4000, 32, (unsigned long)arena + SRC_AT + 0x5000, 32,
             64, 48, 0, 0, OGPU_YUV_FORMAT(OGPU_YUV_I420, OGPU_YUV_BT601));
    row("YUV I420 64x48 (v1.2)", 64 * 48, n);

    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    FreeVec(arena);
    CloseDevice(&treq.tr_node);
    return 0;
}
