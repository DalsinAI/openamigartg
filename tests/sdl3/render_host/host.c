/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The Amiga around SDL3.module's opengpu renderer, on a Linux PC, for
 * tests/sdl3/rendercompare_host.sh:
 *   - opengpu.library is OpenGPU's own core (library/opengpu, with the 3D
 *     range), as the CPU back end is on an Amiga: OGPU_Query answers what
 *     the core answers, and OGPU_Submit runs the batch there and then;
 *   - a stream's addresses are 32 bits, so SDL's memory comes from an arena
 *     below 2 GB (SDL_SetMemoryFunctions, before main), and an address that
 *     isn't in it is reported: the renderer handed OpenGPU memory that isn't
 *     SDL's;
 *   - the window's present (SDL_os3gpu.c's Amiga part) answers "can't", so
 *     SDL_UpdateWindowSurface shows the frame, as without a screen OpenGPU
 *     draws on.
 * With OPENGPU_HOST_LIBRARY=0.18 it is a library before stream v1.7: it
 * answers OGPU_Query as 0.18 did, and each command that carries v1.7 (a
 * PALETTE, a v1.7 format, blend mode, filter or clamp, P010 or a new colour
 * space) is reported, as 0.18 would have refused it.
 * At the end it prints how many batches ran, how many met an error, and
 * how many commands a 0.18 library would have refused. */
#define _GNU_SOURCE
#include "SDL_internal.h"
#include "video/amigaos3/SDL_os3gpu.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../../../library/opengpu/ogpu_core.h"
#include "../../../library/opengpu/ogpu_3d.h"

/* ---- memory below 2 GB --------------------------------------------------------- */

static unsigned char *arena;
static size_t arena_size, arena_used;
static volatile int arena_lock;

typedef struct { size_t size, pad; } hdr; /* 16 bytes before each block */

static void *SDLCALL a_malloc(size_t n)
{
    hdr *h = NULL;
    n = (n + 15) & ~(size_t)15;
    if (!n) {
        n = 16;
    }
    while (__sync_lock_test_and_set(&arena_lock, 1)) {
    }
    if (arena_used + sizeof(hdr) + n <= arena_size) {
        h = (hdr *)(arena + arena_used);
        arena_used += sizeof(hdr) + n;
    }
    __sync_lock_release(&arena_lock);
    if (!h) {
        fprintf(stderr, "opengpu host: the arena is full\n");
        return NULL;
    }
    h->size = n;
    return h + 1;
}

static void *SDLCALL a_calloc(size_t count, size_t size)
{
    void *p = a_malloc(count * size);
    if (p) {
        memset(p, 0, count * size);
    }
    return p;
}

static void *SDLCALL a_realloc(void *p, size_t n)
{
    void *q;
    size_t old;
    if (!p) {
        return a_malloc(n);
    }
    old = ((hdr *)p - 1)->size;
    if (n <= old) {
        return p;
    }
    q = a_malloc(n);
    if (q) {
        memcpy(q, p, old);
    }
    return q;
}

static void SDLCALL a_free(void *p)
{
    (void)p; /* the test is short: nothing comes back */
}

static unsigned long batches, failed_batches, outside, newer;
static int v16; /* OPENGPU_HOST_LIBRARY=0.18 */

static void report(void)
{
    printf("opengpu host: %lu batches, %lu with an error, %lu addresses outside SDL's memory; %lu MB of memory used\n",
           batches, failed_batches, outside, (unsigned long)(arena_used >> 20));
    if (v16) {
        printf("opengpu host (as 0.18): %lu commands a 0.18 library would have refused\n", newer);
    }
}

__attribute__((constructor)) static void host_start(void)
{
    static const size_t sizes[] = { (size_t)1 << 30, (size_t)512 << 20, (size_t)256 << 20 };
    size_t i;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && !arena; i++) {
        void *p = mmap(NULL, sizes[i], PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_NORESERVE, -1, 0);
        if (p != MAP_FAILED) {
            arena = (unsigned char *)p;
            arena_size = sizes[i];
        }
    }
    if (!arena || !SDL_SetMemoryFunctions(a_malloc, a_calloc, a_realloc, a_free)) {
        fprintf(stderr, "opengpu host: no memory below 2 GB\n");
        exit(20);
    }
    v16 = getenv("OPENGPU_HOST_LIBRARY") && strcmp(getenv("OPENGPU_HOST_LIBRARY"), "0.18") == 0;
    atexit(report);
}

/* ---- a library before v1.7 ------------------------------------------------------- */

static int v17_format(unsigned long f)
{
    return f == OGPU_FMT_INDEX8 || f == OGPU_FMT_RGBA32 || f == OGPU_FMT_ABGR32;
}

/* What 0.18's OGPU_Query answered: nothing for v1.7's opcode and formats,
   and TRIANGLES only into ARGB32, RGB565 and CLUT8. */
static ULONG query_v16(ULONG op, ULONG format)
{
    if (op == OGPU_OP_PALETTE || v17_format(format)) {
        return OGPU_NONE;
    }
    if (op == OGPU_OP_TRIANGLES && format != OGPU_FMT_ARGB32 && format != OGPU_FMT_RGB565 && format != OGPU_FMT_CLUT8) {
        return OGPU_NONE;
    }
    return (ULONG)ogpu_core_supports((int)op, (int)format);
}

static unsigned long be32(const ogpu_u8 *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* The commands in a stream that carry anything of v1.7, or that 0.18 drew
   wrong: TRIANGLES read only ARGB32, RGB565, A8 and CLUT8 slots before. */
static void scan_v17(const ogpu_u8 *s, long words)
{
    unsigned long slot_fmt[OGPU_MAX_SLOTS] = { 0 };
    long i = 0;
    while (i < words) {
        const unsigned long h = be32(s + i * 4), op = h >> 16, len = h & 0xFFFF;
        const ogpu_u8 *a = s + i * 4 + 4;
        int bad = 0;
        if (!len || i + (long)len > words) {
            break;
        }
#define ARG(n) ((long)(n) < (long)len - 1 ? be32(a + (n) * 4) : 0UL)
        switch (op) {
        case OGPU_OP_PALETTE: bad = 1; break;
        case OGPU_OP_SURFACE:
            bad = v17_format(ARG(4));
            if (ARG(0) < OGPU_MAX_SLOTS) {
                slot_fmt[ARG(0)] = ARG(4);
            }
            break;
        case OGPU_OP_FILL_BLEND: case OGPU_OP_LINES_BLEND: case OGPU_OP_POINTS_BLEND:
            bad = (ARG(3) & OGPU_BLEND_MASK) > OGPU_BLEND_MUL; break;
        case OGPU_OP_COMPOSITE:
            bad = (ARG(6) & OGPU_COMP_PIXELART) || ((ARG(6) & OGPU_COMP_BLENDMODE) && OGPU_COMP_MODE_OF(ARG(6)) > OGPU_BLEND_MUL); break;
        case OGPU_OP_COMPOSITE_AFFINE:
            bad = (ARG(10) & OGPU_AFF_PIXELART) || (ARG(10) & OGPU_BLEND_MASK) > OGPU_BLEND_MUL; break;
        case OGPU_OP_YUV: case OGPU_OP_YUV_SCALED: {
            const unsigned long f = ARG(op == OGPU_OP_YUV ? 8 : 7);
            bad = (f & 255) == OGPU_YUV_P010 || ((f >> 8) & 255) > OGPU_YUV_JPEG;
            break;
        }
        case OGPU_OP_TRIANGLES: {
            const unsigned long tex = ARG(6), f = tex < OGPU_MAX_SLOTS ? slot_fmt[tex] : OGPU_FMT_ARGB32;
            bad = (ARG(7) & OGPU_TRI_BLEND_MASK) > OGPU_TRI_BLEND_MUL ||
                  (ARG(7) & (OGPU_TRI_PIXELART | OGPU_TRI_CLAMP_U | OGPU_TRI_CLAMP_V)) ||
                  (f != OGPU_FMT_ARGB32 && f != OGPU_FMT_RGB565 && f != OGPU_FMT_A8 && f != OGPU_FMT_CLUT8);
            break;
        }
        default: break;
        }
#undef ARG
        if (bad && ++newer <= 10) {
            fprintf(stderr, "opengpu host (as 0.18): batch %lu: opcode 0x%04lx at word %ld is v1.7's\n", batches + 1, op, i);
        }
        i += (long)len;
    }
}

/* ---- opengpu.library ------------------------------------------------------------ */

static struct ogpu_core core;
static struct ogpu3d core3d;

static ogpu_u8 *host_map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    uintptr_t a = (uintptr_t)address;
    (void)user;
    if (!address) {
        return 0;
    }
    if (a < (uintptr_t)arena || a + length > (uintptr_t)arena + arena_used) {
        if (++outside <= 10) {
            fprintf(stderr, "opengpu host: address 0x%08lx (%lu bytes) isn't SDL's memory\n",
                    (unsigned long)address, (unsigned long)length);
        }
        return 0;
    }
    return (ogpu_u8 *)a;
}

ULONG host_OGPU_Query(ULONG op, ULONG format)
{
    if (v16) {
        return query_v16(op, format);
    }
    return (ULONG)ogpu_core_supports((int)op, (int)format); /* back end 0, the CPU */
}

LONG host_OGPU_Submit(APTR stream, ULONG words, ULONG *fence)
{
    const ogpu_u8 *s = (const ogpu_u8 *)stream;
    if (v16) {
        scan_v17(s, (long)words);
    }
    ogpu_core_init(&core);
    core.map = host_map;
    core.fence = 0;
    core.ext = 0;
    core.user = 0;
    ogpu3d_init(&core3d, &core);
    ogpu3d_run(&core3d, s, (long)words);
    batches++;
    if (core.last_error != OGPU_OK) {
        if (++failed_batches <= 20) {
            const long w = core.error_word;
            const unsigned long hdrw = (w >= 0 && w < (long)words) ?
                ((unsigned long)s[w * 4] << 24 | (unsigned long)s[w * 4 + 1] << 16 | (unsigned long)s[w * 4 + 2] << 8 | s[w * 4 + 3]) : 0;
            fprintf(stderr, "opengpu host: batch %lu: error %d at word %ld (opcode 0x%04lx)\n",
                    batches, core.last_error, w, hdrw >> 16);
        }
    }
    if (fence) {
        *fence = (ULONG)batches;
    }
    return core.last_error;
}

LONG host_OGPU_Wait(ULONG fence)
{
    (void)fence; /* every batch is done when Submit returns */
    return OGPU_OK;
}

/* ---- exec and graphics ------------------------------------------------------------- */

static struct Library opengpu_base;

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    (void)version;
    return (name && strcmp(name, OPENGPU_NAME) == 0) ? &opengpu_base : NULL;
}

void CloseLibrary(struct Library *lib)
{
    (void)lib;
}

void WaitTOF(void)
{
}

/* ---- the window: no screen to draw on ----------------------------------------------- */

int SDL_OS3_GPU_PresentAppend(SDL_Window *window, SDL_Surface *surface, struct OGPUBatch *b,
                              int dst_slot, int src_slot, const SDL_Rect *rects, int numrects,
                              SDL_OS3_GPUPresent *p)
{
    (void)window; (void)surface; (void)b; (void)dst_slot; (void)src_slot; (void)rects; (void)numrects; (void)p;
    return 0;
}

void SDL_OS3_GPU_PresentEnd(SDL_OS3_GPUPresent *p)
{
    (void)p;
}

void SDL_OS3_GPU_Abandon(SDL_OS3_GPUPresent *p)
{
    (void)p;
}

bool SDL_OS3_GPU_Present(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects)
{
    (void)window; (void)surface; (void)rects; (void)numrects;
    return false;
}
