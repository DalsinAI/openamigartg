/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPUCheck: is OpenGPU on this Amiga drawing what it should, and how
 * fast? It names the back ends opengpu.library found, draws the golden
 * scenes (tests/golden_scenes.c) through the library, so through its driver
 * when there is one (ACRTG.gpu on the Cradle), and again on this 68k with
 * the C core linked in, and checks both against the golden checksums every
 * back end must give. Then it times the same batches both ways.
 *   OpenGPUCheck [QUICK]
 * Returns 0 when everything matches, 10 when something doesn't, 20 when
 * opengpu.library is missing. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/opengpu.h>
#include <stdio.h>
#include <string.h>

#include "../tests/golden_scenes.h"
#include "../include/opengpu/opengpu.h"
#include "../include/opengpu/build.h"

#define GOLDEN_G1  0xde825f7bUL
#define GOLDEN_V11 0x400d1333UL

struct Library *OpenGPUBase;
struct Device *TimerBase;
static struct timerequest treq;

static ogpu_u8 *lib_map(void *user, ogpu_u32 address, ogpu_u32 length) { (void)user; (void)length; return (ogpu_u8 *)address; }

/* Through opengpu.library: its driver, or its own CPU back end. */
static long run_library(void *user, const unsigned char *stream, long words)
{
    ULONG fence = 0;
    LONG r;
    (void)user;
    r = OGPU_Submit((APTR)stream, (ULONG)words, &fence);
    if (r == OGPU_OK) r = OGPU_Wait(fence);
    return r;
}

/* On this 68k, with the C core linked into this program. */
static long run_cpu(void *user, const unsigned char *stream, long words)
{
    static struct ogpu_core c;
    (void)user;
    ogpu_core_init(&c);
    c.map = lib_map;
    ogpu_core_run(&c, stream, words);
    return c.last_error;
}

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

/* A batch like a busy window's: a 320 x 240 ARGB32 fill, a scaled
 * composite and twenty lines, into Fast RAM. */
static unsigned char bench_buf[4096];
static long bench_words;
static void bench_build(ogpu_u8 *arena)
{
    struct OGPUBatch b;
    int i;
    ogpu_batch_init(&b, bench_buf, sizeof bench_buf / 4);
    ogpu_surface(&b, 0, (unsigned long)arena, 320 * 4, 320, 240, OGPU_FMT_ARGB32);
    ogpu_surface(&b, 1, (unsigned long)arena + 0x60000, 64 * 4, 64, 48, OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    ogpu_fill(&b, 0, 0, 320, 240, 0xFF336699UL);
    ogpu_composite(&b, 1, 0, 0, 64, 48, 20, 20, 256, 192, 200, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR);
    for (i = 0; i < 20; i++) ogpu_line(&b, 160, 120, i * 16, i & 1 ? 0 : 239, 0xFFFFFFFFUL, OGPU_JAM1);
    bench_words = b.words;
}

static double bench(long (*run)(void *, const unsigned char *, long), int n)
{
    double t0 = now_ms();
    int i;
    for (i = 0; i < n; i++) run(NULL, bench_buf, bench_words);
    return (now_ms() - t0) / n;
}

int main(int argc, char **argv)
{
    struct ogpu_scene_env env;
    ogpu_u8 *arena;
    unsigned long g1, v11, c1, c11;
    ULONG q;
    int bad = 0, i, quick = argc > 1;
    STRPTR name;
    if (!(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) {
        printf("OpenGPUCheck: opengpu.library is not installed\n");
        return 20;
    }
    printf("opengpu.library %d.%d\n", OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision);
    for (i = 0; i < 4; i++)
        if ((name = OGPU_BackEndName(i)) != NULL) printf("  back end %d: %s\n", i, name);
    q = OGPU_Query(OGPU_OP_COMPOSITE, OGPU_FMT_ARGB32);
    printf("  COMPOSITE into ARGB32: %s, back end %d\n", OGPU_ANSWER(q) == OGPU_FULL ? "full" : OGPU_ANSWER(q) == OGPU_PARTIAL ? "partly" : "no",
           OGPU_BACKEND(q));
    q = OGPU_Query(OGPU_OP_VIRGL, 0);
    printf("  virgl (Mesa on the PC's graphics chip): %s\n", OGPU_ANSWER(q) == OGPU_FULL ? "yes" : "no");

    if (!(arena = AllocVec(OGPU_SCENE_ARENA, MEMF_ANY | MEMF_CLEAR))) { printf("no memory\n"); CloseLibrary(OpenGPUBase); return 20; }
    memset(&env, 0, sizeof env);
    env.arena = arena; env.addr = (unsigned long)arena;
    env.run = run_library;
    g1 = ogpu_golden_g1(&env);
    v11 = ogpu_golden_v11(&env);
    printf("through the library: golden scene %08lx (%s), v1.1 scene %08lx (%s)%s\n",
           g1, g1 == GOLDEN_G1 ? "right" : "WRONG", v11, v11 == GOLDEN_V11 ? "right" : "WRONG",
           env.failures ? ", with errors" : "");
    bad += g1 != GOLDEN_G1 || v11 != GOLDEN_V11 || env.failures;
    memset(arena, 0, OGPU_SCENE_ARENA);
    env.failures = 0;
    env.run = run_cpu;
    c1 = ogpu_golden_g1(&env);
    c11 = ogpu_golden_v11(&env);
    printf("on this 68k:         golden scene %08lx (%s), v1.1 scene %08lx (%s)\n",
           c1, c1 == GOLDEN_G1 ? "right" : "WRONG", c11, c11 == GOLDEN_V11 ? "right" : "WRONG");
    bad += c1 != GOLDEN_G1 || c11 != GOLDEN_V11 || env.failures;

    if (!quick && !OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) {
        double lib, cpu;
        int n = 50;
        TimerBase = treq.tr_node.io_Device;
        bench_build(arena);
        for (i = 0; i < 64 * 48; i++) ((ULONG *)(arena + 0x60000))[i] = 0x80000000UL | (ULONG)(i * 2654435761UL & 0xFFFFFF);
        lib = bench(run_library, n);
        cpu = bench(run_cpu, n);
        printf("a batch (320 x 240 fill, scaled composite, 20 lines; %ld words): library %.2f ms, this 68k %.2f ms (%.1f times)\n",
               bench_words, lib, cpu, lib > 0 ? cpu / lib : 0.0);
        CloseDevice(&treq.tr_node);
    }
    FreeVec(arena);
    CloseLibrary(OpenGPUBase);
    printf(bad ? "OpenGPUCheck: something is WRONG\n" : "OpenGPUCheck: all right\n");
    return bad ? 10 : 0;
}
