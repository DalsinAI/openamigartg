/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's Vulkan back end (G2) against the C core: the golden scene must
 * come out with tests/golden/opengpu-g1.txt's checksum, and thousands of
 * random streams must leave video RAM, the other memory, the results and the
 * fences byte for byte as the core leaves them. Runs on any Vulkan device;
 * with no GPU, Mesa's lavapipe (mesa-vulkan-drivers) does. OGPU_VK_DEVICE
 * picks a device by name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../host/vulkan/ogpu_vk.h"
#include "../include/opengpu/build.h"

#define RAM   (1L << 20)            /* "Fast RAM": Amiga addresses 0 .. RAM */
#define VBASE 0x40000000UL          /* video RAM's Amiga address */
#define VSIZE (1UL << 20)

static ogpu_u8 ram_ref[RAM], ram_vk[RAM], vram_ref[VSIZE];
static ogpu_u8 *vram_vk;
static long fences_ref[16], nf_ref, fences_vk[16], nf_vk;

static ogpu_u8 *map_ref(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a < RAM && len <= (ogpu_u32)RAM - a) return ram_ref + a;
    if (a >= VBASE && a - VBASE <= VSIZE && len <= VSIZE - (a - VBASE)) return vram_ref + (a - VBASE);
    return 0;
}
static ogpu_u8 *map_vk(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a < RAM && len <= (ogpu_u32)RAM - a) return ram_vk + a;
    return 0;
}
static void fence_ref(void *user, ogpu_u32 id) { (void)user; if (nf_ref < 16) fences_ref[nf_ref++] = (long)id; }
static void fence_vk(void *user, ogpu_u32 id) { (void)user; if (nf_vk < 16) fences_vk[nf_vk++] = (long)id; }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char sbuf[1 << 16];
static struct OGPUBatch B;
static struct ogpu_core C;
static struct ogpu_vk *V;

static unsigned long rnd_state = 777;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }

static void begin(void) { ogpu_batch_init(&B, sbuf, sizeof sbuf / 4); }

/* The same stream through both; 1 if they agree. */
static int run_both(const char *what) {
    long d_ref, d_vk;
    int ok = 1;
    nf_ref = nf_vk = 0;
    d_ref = ogpu_core_run(&C, sbuf, B.words);
    d_vk = ogpu_vk_run(V, sbuf, B.words);
    if (d_ref != d_vk || C.last_error != ogpu_vk_last_error(V)
        || (C.last_error != OGPU_OK && C.error_word != ogpu_vk_error_word(V))) {
        CHECK(0, "%s: done %ld/%ld, error %d/%d at %ld/%ld", what, d_ref, d_vk, C.last_error, ogpu_vk_last_error(V),
              C.error_word, ogpu_vk_error_word(V));
        ok = 0;
    }
    if (nf_ref != nf_vk || memcmp(fences_ref, fences_vk, sizeof(long) * (size_t)nf_ref)) {
        CHECK(0, "%s: fences differ", what);
        ok = 0;
    }
    if (memcmp(vram_ref, vram_vk, VSIZE)) {
        long i;
        if (getenv("OGPU_VK_DUMP")) {
            long at = 0;
            while (at < B.words) {
                unsigned long hdr = ((unsigned long)sbuf[at*4] << 24) | ((unsigned long)sbuf[at*4+1] << 16) | ((unsigned long)sbuf[at*4+2] << 8) | sbuf[at*4+3];
                long n = (long)(hdr & 0xFFFF), q;
                printf("  %3ld: op %02lx", at, hdr >> 16);
                for (q = 1; q < n && at + q < B.words; q++)
                    printf(" %08lx", ((unsigned long)sbuf[(at+q)*4] << 24) | ((unsigned long)sbuf[(at+q)*4+1] << 16) | ((unsigned long)sbuf[(at+q)*4+2] << 8) | sbuf[(at+q)*4+3]);
                printf("\n");
                if (n < 1) break;
                at += n;
            }
        }
        for (i = 0; i < (long)VSIZE && vram_ref[i] == vram_vk[i]; i++) ;
        CHECK(0, "%s: video RAM differs first at +%lx (core %02x, gpu %02x)", what, i, vram_ref[i], vram_vk[i]);
        ok = 0;
    }
    if (memcmp(ram_ref, ram_vk, RAM)) { CHECK(0, "%s: Fast RAM differs", what); ok = 0; }
    return ok;
}

static void noise_both(int in_vram, long off, long bytes) {
    long i;
    for (i = 0; i < bytes; i++) {
        ogpu_u8 v = (ogpu_u8)rnd();
        if (in_vram) { vram_ref[off + i] = v; vram_vk[off + i] = v; }
        else { ram_ref[off + i] = v; ram_vk[off + i] = v; }
    }
}

static int bpp_of(int f) { return f == OGPU_FMT_RGB565 ? 2 : f == OGPU_FMT_ARGB32 ? 4 : 1; }

/* ---- the golden scene (as tests/test_opengpu.c draws it) -------------------------- */

static unsigned long fnv(const ogpu_u8 *p, long n, unsigned long h) {
    while (n--) { h ^= *p++; h = (h * 16777619UL) & 0xFFFFFFFFUL; }
    return h;
}

/* T in video RAM; the second surface and the data in video RAM or Fast RAM. */
static unsigned long golden_scene(int data_in_vram) {
    static const int fmts[3] = { OGPU_FMT_CLUT8, OGPU_FMT_RGB565, OGPU_FMT_ARGB32 };
    const unsigned long T0 = VBASE + 0x1000;
    const unsigned long S0 = data_in_vram ? VBASE + 0x40000 : 0x40000;
    const unsigned long D0 = data_in_vram ? VBASE + 0x80000 : 0x80000;
    ogpu_u8 *s0 = data_in_vram ? vram_vk + 0x40000 : ram_vk + 0x40000;
    ogpu_u8 *d0 = data_in_vram ? vram_vk + 0x80000 : ram_vk + 0x80000;
    unsigned long h = 2166136261UL;
    int k, i;
    rnd_state = 2026;
    for (i = 0; i < 0x4000; i++) d0[i] = (ogpu_u8)rnd();
    for (i = 0; i < 64 * 48; i++) {
        unsigned long v = ((unsigned long)(i * 37) & 255) << 24 | (unsigned long)(i * 2654435761UL & 0xFFFFFFUL);
        ogpu_u8 *p = s0 + (long)(i / 64) * 256 + (i % 64) * 4;
        p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
    }
    for (k = 0; k < 3; k++) {
        int f = fmts[k];
        long bpr = 160 * bpp_of(f);
        unsigned long white = f == OGPU_FMT_CLUT8 ? 2 : f == OGPU_FMT_RGB565 ? 0xFFFF : 0xFFFFFFFFUL;
        unsigned long blue = f == OGPU_FMT_CLUT8 ? 3 : f == OGPU_FMT_RGB565 ? 0x2B5F : 0xFF2A5DB0UL;
        memset(vram_vk + 0x1000, 0, (size_t)(bpr * 100));
        begin();
        ogpu_surface(&B, 0, T0, (unsigned long)bpr, 160, 100, f);
        ogpu_surface(&B, 1, S0, 64 * 4, 64, 48, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_fill(&B, 0, 0, 160, 100, white);
        ogpu_fill(&B, 0, 0, 160, 11, blue);
        ogpu_template(&B, D0, 20, 3, 4, 2, 150, 8, 1, 0, OGPU_JAM1);
        ogpu_pattern(&B, D0 + 0x100, 8, 10, 20, 60, 30, blue, white, OGPU_JAM2);
        for (i = 0; i < 12; i++) ogpu_line(&B, 80, 55, 80 + (i - 6) * 13, i & 1 ? 99 : 12, blue, OGPU_JAM1);
        ogpu_invert(&B, 100, 60, 40, 30, f == OGPU_FMT_ARGB32 ? 0x00FFFFFFUL : 0xFFFF);
        ogpu_copy(&B, 0, 0, 0, 30, 70, 80, 25);
        ogpu_clip(&B, 2, 2, 150, 90);
        if (f != OGPU_FMT_CLUT8) {
            ogpu_composite(&B, 1, 0, 0, 64, 48, 90, 15, 96, 72, 200, OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR);
            ogpu_composite(&B, 1, 8, 8, 16, 16, 5, 60, 40, 30, 255, 0);
            ogpu_pixels(&B, S0, 64 * 4, OGPU_FMT_ARGB32, 0, 120, 70, 64, 48);
        } else {
            ogpu_pixels(&B, D0 + 0x200, 32, OGPU_FMT_CLUT8, 0, 120, 70, 32, 32);
        }
        ogpu_fence(&B, (unsigned long)k);
        nf_vk = 0;
        ogpu_vk_run(V, sbuf, B.words);
        CHECK(ogpu_vk_last_error(V) == OGPU_OK && nf_vk == 1, "golden scene fmt %d ran (%d)", f, ogpu_vk_last_error(V));
        h = fnv(vram_vk + 0x1000, bpr * 100, h);
    }
    return h;
}

/* v1.1's scene (as tests/test_opengpu.c's golden_scene_v11): the target and
 * the clip mask in video RAM, the rest in video RAM or Fast RAM. */
static unsigned long golden_scene_v11(int data_in_vram) {
    static const int fmts[2] = { OGPU_FMT_RGB565, OGPU_FMT_ARGB32 };
    const unsigned long T0 = VBASE + 0x1000;
    const unsigned long S0 = data_in_vram ? VBASE + 0x40000 : 0x40000;
    const unsigned long D0 = data_in_vram ? VBASE + 0x80000 : 0x80000;
    ogpu_u8 *s0 = data_in_vram ? vram_vk + 0x40000 : ram_vk + 0x40000;
    ogpu_u8 *d0 = data_in_vram ? vram_vk + 0x80000 : ram_vk + 0x80000;
    unsigned long h = 2166136261UL;
    int k, i, x, y;
    for (y = 0; y < 48; y++)
        for (x = 0; x < 64; x++) {
            long dx = x * 2 - 63, dy = y * 2 - 47, r2 = dx * dx + dy * dy, c = (2200 - r2) / 4;
            d0[0x2000 + y * 64 + x] = (ogpu_u8)(c < 0 ? 0 : c > 255 ? 255 : c);
        }
    for (i = 0; i < 64 * 48; i++) d0[0x3000 + i] = (ogpu_u8)((i * 7) ^ (i >> 5));
    for (i = 0; i < 64 * 48; i++) {
        unsigned long v = 0xFF000000UL | (unsigned long)(i * 2654435761UL & 0xFFFFFFUL);
        ogpu_u8 *p = s0 + (long)(i / 64) * 256 + (i % 64) * 4;
        p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
    }
    for (k = 0; k < 2; k++) {
        int f = fmts[k];
        long bpr = 160 * bpp_of(f);
        memset(vram_vk + 0x1000, 0, (size_t)(bpr * 100));
        memset(vram_vk + 0x11000, 0, 64 * 48);
        begin();
        ogpu_surface(&B, 0, T0, (unsigned long)bpr, 160, 100, f);
        ogpu_surface(&B, 1, S0, 64 * 4, 64, 48, OGPU_FMT_ARGB32);
        ogpu_surface(&B, 2, D0 + 0x2000, 64, 64, 48, OGPU_FMT_A8);
        ogpu_surface(&B, 3, D0 + 0x3000, 64, 64, 48, OGPU_FMT_A8);
        ogpu_surface(&B, 4, T0 + 0x10000, 64, 64, 48, OGPU_FMT_A8);
        ogpu_target(&B, 4);
        ogpu_composite(&B, 2, 0, 0, 64, 48, 0, 0, 64, 48, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_ADD);
        ogpu_composite(&B, 3, 0, 0, 64, 48, 0, 0, 64, 48, 128, OGPU_COMP_SRCALPHA | OGPU_COMP_ADD);
        ogpu_composite(&B, 2, 0, 0, 64, 48, 0, 0, 64, 48, 255, OGPU_COMP_SRCALPHA | OGPU_COMP_IN);
        ogpu_target(&B, 0);
        ogpu_fill(&B, 0, 0, 160, 100, f == OGPU_FMT_RGB565 ? 0xC618 : 0xFFC0C0C0UL);
        for (i = 0; i < 6; i++) ogpu_mask(&B, D0 + 0x3000 + (unsigned long)i * 3, 64, 4 + i * 25, 4, 20, 14, 0xFF000000UL | (unsigned long)(i * 0x2A1F37));
        ogpu_composite_masked(&B, 1, 0, 0, 64, 48, 20, 30, 64, 48, 255, 0, 4, 0, 0);
        ogpu_clip(&B, 90, 25, 60, 70);
        ogpu_composite_masked(&B, 1, 0, 0, 32, 24, 80, 30, 64, 48, 200, OGPU_COMP_BILINEAR, 2, 0, 0);
        ogpu_fence(&B, (unsigned long)k);
        nf_vk = 0;
        ogpu_vk_run(V, sbuf, B.words);
        CHECK(ogpu_vk_last_error(V) == OGPU_OK && nf_vk == 1, "v1.1 scene fmt %d ran (%d)", f, ogpu_vk_last_error(V));
        h = fnv(vram_vk + 0x1000, bpr * 100, h);
        h = fnv(vram_vk + 0x11000, 64 * 48, h);
    }
    return h;
}

/* ---- random streams ------------------------------------------------------------- */

/* An address for a surface or data: video RAM or Fast RAM, usually aligned. */
static unsigned long some_address(long span) {
    unsigned long off = (unsigned long)(rnd() % (unsigned long)(0x30000 - span > 0 ? 0x30000 - span : 1));
    if (rnd() % 8) off &= ~3UL;
    return rnd() % 6 ? VBASE + 0x10000 + off : 0x10000 + off;
}

static int some_mode(void) { static const int m[] = { 0, 1, 2, 4, 5, 6 }; return m[rnd() % 6]; }
static unsigned long some_colour(void) { return rnd() << 8 | (rnd() & 255); }

static void random_command(void) {
    int x = (int)(rnd() % 140) - 20, y = (int)(rnd() % 100) - 20, w = (int)(rnd() % 120), h = (int)(rnd() % 80);
    switch (rnd() % 15) {
    case 0: {
        int f = rnd() % 5 ? 1 + (int)(rnd() % 3) : OGPU_FMT_A8, sw = 1 + (int)(rnd() % 120), sh = 1 + (int)(rnd() % 90);
        long bpr = (long)sw * bpp_of(f) + (rnd() % 3 ? 0 : (long)(rnd() % 9));
        if (rnd() % 4 == 0) bpr &= ~3L;
        ogpu_surface(&B, (int)(rnd() % 4), some_address(bpr * sh), (unsigned long)bpr, sw, sh, f);
        break;
    }
    case 1: ogpu_target(&B, (int)(rnd() % 4)); break;
    case 2: if (rnd() % 2) ogpu_clip(&B, x, y, w, h); else ogpu_clip(&B, 0, 0, 0, 0); break;
    case 3: ogpu_fill(&B, x, y, w, h, some_colour()); break;
    case 4: ogpu_invert(&B, x, y, w, h, some_colour()); break;
    case 5: ogpu_copy(&B, (int)(rnd() % 4), (int)(rnd() % 120) - 10, (int)(rnd() % 90) - 10, x, y, w, h); break;
    case 6: ogpu_template(&B, some_address(0x2000), 1 + rnd() % 40, rnd() % 24, x, y, w % 100, h % 60,
                          some_colour(), some_colour(), some_mode()); break;
    case 7: ogpu_pattern(&B, some_address(512), 1UL << (rnd() % 9), x, y, w, h, some_colour(), some_colour(), some_mode()); break;
    case 8: ogpu_line(&B, x, y, (int)(rnd() % 160) - 20, (int)(rnd() % 120) - 20, some_colour(), some_mode()); break;
    case 9: {
        int f = rnd() % 6 ? 1 + (int)(rnd() % 4) : OGPU_FMT_A8;
        unsigned long table = f == OGPU_FMT_INDEX8 && rnd() % 4 ? some_address(1024) : 0;
        w %= 64; h %= 48;
        ogpu_pixels(&B, some_address(0x4000), (unsigned long)w * bpp_of(f) + rnd() % 8, f, table, x, y, w, h);
        break;
    }
    case 10: case 11:
        ogpu_composite(&B, (int)(rnd() % 4), (int)(rnd() % 40), (int)(rnd() % 30), (int)(rnd() % 80), (int)(rnd() % 60),
                       x, y, w, h, (int)(rnd() % 256), rnd() % 4 ? rnd() % 4 : rnd() % 32);
        break;
    case 12: ogpu_fence(&B, rnd()); break;
    case 13:        /* v1.1 */
        ogpu_composite_masked(&B, (int)(rnd() % 4), (int)(rnd() % 40), (int)(rnd() % 30), (int)(rnd() % 80), (int)(rnd() % 60),
                              x, y, w % 70, h % 50, (int)(rnd() % 256), rnd() % 32, (int)(rnd() % 4),
                              (int)(rnd() % 20) - 2, (int)(rnd() % 20) - 2);
        break;
    case 14: ogpu_mask(&B, some_address(0x2000), (unsigned long)(w % 64) + rnd() % 8, x, y, w % 64, h % 48, some_colour()); break;
    }
}

static void test_random(int rounds) {
    int round, bad = 0;
    noise_both(1, 0, (long)VSIZE);
    noise_both(0, 0, RAM);
    for (round = 0; round < rounds && bad < 5; round++) {
        int i, n = 1 + (int)(rnd() % 30);
        char what[64];
        begin();
        if (round % 8 == 0) {
            /* Sometimes a stream of raw words that are mostly commands. */
            ogpu_u8 *p = sbuf;
            long k;
            for (k = 0; k < n * 6 * 4; k++) p[k] = (ogpu_u8)rnd();
            for (k = 0; k < n * 6; ) {
                static const int ops[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x20, 0x01, 0x02, 0x03, 0xF0, 0x99, 0x17, 0x20 };
                static const int lens[] = { 4, 4, 5, 9, 8, 5, 7, 8, 6, 2, 3, 2, 3, 6, 10 };
                int j = (int)(rnd() % 15), len = lens[j] + (rnd() % 10 == 0 ? (int)(rnd() % 3) - 1 : 0), q;
                if (len < 1) len = 1;
                if (k + len > n * 6) break;
                p[k * 4] = 0; p[k * 4 + 1] = (ogpu_u8)ops[j]; p[k * 4 + 2] = 0; p[k * 4 + 3] = (ogpu_u8)len;
                for (q = 1; q < len; q++) if (rnd() % 4) { p[(k + q) * 4] = 0; p[(k + q) * 4 + 2] = 0; p[(k + q) * 4 + 1] &= 0x7F; }
                k += len;
            }
            B.words = n * 6;
        } else {
            for (i = 0; i < n; i++) random_command();
        }
        if (getenv("OGPU_VK_STEP")) {
            /* One command at a time, to name the first that differs. */
            static unsigned char whole[sizeof sbuf];
            long at = 0, words = B.words;
            memcpy(whole, sbuf, sizeof sbuf);
            while (at < words) {
                long n = (long)(((unsigned long)whole[at * 4 + 2] << 8) | whole[at * 4 + 3]);
                if (n < 1 || at + n > words) n = words - at;
                memcpy(sbuf, whole + at * 4, (size_t)n * 4);
                B.words = n;
                snprintf(what, sizeof what, "random stream %d, word %ld", round, at);
                if (!run_both(what)) { bad = 5; break; }
                at += n;
            }
            continue;
        }
        snprintf(what, sizeof what, "random stream %d", round);
        if (!run_both(what)) bad++;
    }
}

int main(int argc, char **argv) {
    struct ogpu_vk_config cfg;
    struct ogpu_vk_stats st;
    char err[200];
    unsigned long golden_v, golden_r, want = 0;
    int rounds = argc > 2 ? atoi(argv[2]) : 3000;

    memset(&cfg, 0, sizeof cfg);
    cfg.vram_base = VBASE;
    cfg.vram_size = VSIZE;
    cfg.arena_size = 1u << 20;
    cfg.map = map_vk;
    cfg.fence = fence_vk;
    V = ogpu_vk_create(&cfg, err, sizeof err);
    if (!V) {
        /* Only a machine with no Vulkan device skips; anything else is a failure. */
        int none = !strncmp(err, "no Vulkan device", 16) || !strncmp(err, "no device with compute", 22)
                   || !strncmp(err, "vkCreateInstance", 16);
        printf("opengpu vulkan: %s (%s)\n", none ? "no device; skipped" : "FAILED to start", err);
        return none ? 0 : 1;
    }
    vram_vk = ogpu_vk_vram(V);
    printf("opengpu vulkan: on %s\n", ogpu_vk_device(V));

    golden_v = golden_scene(1);
    golden_r = golden_scene(0);
    {
        unsigned long before = 0, g11v, g11r;
        ogpu_vk_stats(V, &st);
        before = st.gpu;
        g11v = golden_scene_v11(1);
        g11r = golden_scene_v11(0);
        ogpu_vk_stats(V, &st);
        if (argc > 3) {
            FILE *f = fopen(argv[3], "r");
            unsigned long w11 = 0;
            CHECK(f && fscanf(f, "%lx", &w11) == 1, "golden file %s", argv[3]);
            if (f) fclose(f);
            CHECK(g11v == w11 && g11r == w11, "v1.1 scene %08lx and %08lx, golden file says %08lx", g11v, g11r, w11);
        }
        CHECK(st.gpu - before >= 30, "the v1.1 scene drew on the GPU (%lu commands)", st.gpu - before);
        printf("opengpu vulkan: v1.1 scene %08lx and %08lx, %lu commands on the GPU\n", g11v, g11r, st.gpu - before);
    }
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        CHECK(f && fscanf(f, "%lx", &want) == 1, "golden file %s", argv[1]);
        if (f) fclose(f);
        CHECK(golden_v == want, "golden scene, all in video RAM: %08lx, golden file says %08lx", golden_v, want);
        CHECK(golden_r == want, "golden scene, data in Fast RAM: %08lx, golden file says %08lx", golden_r, want);
    }
    ogpu_vk_stats(V, &st);
    CHECK(st.gpu > 40, "the golden scene drew on the GPU (%lu commands, %lu on the core)", st.gpu, st.cpu);

    ogpu_vk_reset(V);
    ogpu_core_init(&C);
    C.map = map_ref; C.fence = fence_ref; C.user = 0;
    test_random(rounds);
    ogpu_vk_stats(V, &st);
    printf("opengpu vulkan: golden scene %08lx and %08lx; %lu commands on the GPU, %lu on the core, %lu submits; %s\n",
           golden_v, golden_r, st.gpu, st.cpu, st.submits, failures ? "FAILED" : "all tests passed");
    ogpu_vk_destroy(V);
    return failures != 0;
}
