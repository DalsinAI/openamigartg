/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The 3D range's GPU path without a GPU: host/vulkan/ogpu3d_rec.c records
 * as it does for the Vulkan back end, and each dispatch runs the shader's
 * own text (host/vulkan/ogpu3d_px.h, built here as C) on the CPU, tile by
 * tile, pixel by pixel. Every picture, Z and stencil buffer must be the
 * core's byte for byte: thousands of random scenes (tests/scenes3d.c: every
 * mode of the 3D range, buffers in video RAM and outside it, unaligned rows),
 * the bench scenes, the golden scene's checksum, textures changed between
 * runs, and small buffers that make the recorder send in the middle of a
 * command and empty its texture cache. Needs -fwrapv (GLSL's integers wrap).
 *   test_ogpu3d_emul [3d-golden] [rounds] */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../host/vulkan/ogpu3d_rec.h"
#include "../include/opengpu/build3d.h"
#include "golden_scenes.h"
#include "scenes3d.h"

/* ---- the shader, as C ---------------------------------------------------------------- */

typedef unsigned int uint;
typedef struct o3x_level o3x_level;
static uint *g_vram, *g_stage, *g_tex, *g_data;
static uint g_pc[16];
#define TOU(x) ((uint)(x))
#define TOI(x) ((int)(x))
#define INOUT
#define VRAM(i) g_vram[i]
#define STAGE(i) g_stage[i]
#define TEXB(i) g_tex[i]
#define DATA(i) g_data[i]
#define XOR_VRAM(i, v) (g_vram[i] ^= (v))
#define XOR_STAGE(i, v) (g_stage[i] ^= (v))
#define PC(n) g_pc[n]
#include "../host/vulkan/ogpu3d_px.h"

/* ---- memory ------------------------------------------------------------------------------- */

#define VBASE 0x40000000UL          /* video RAM's address */
#define FBASE 0x00100000UL          /* other memory's */
#define REG OGPU_SCENE3D_REGION

static ogpu_u8 *vram_ref, *fast_ref, *vram_gpu, *fast_gpu;
static ogpu_u8 *stage_buf, *tex_buf, *data_buf;

static ogpu_u8 *map_in(ogpu_u8 *v, ogpu_u8 *f, ogpu_u32 a, ogpu_u32 len) {
    if (a >= VBASE && a - VBASE <= (ogpu_u32)REG && len <= (ogpu_u32)REG - (a - VBASE)) return v + (a - VBASE);
    if (a >= FBASE && a - FBASE <= (ogpu_u32)REG && len <= (ogpu_u32)REG - (a - FBASE)) return f + (a - FBASE);
    return 0;
}
static ogpu_u8 *map_ref(void *u, ogpu_u32 a, ogpu_u32 len) { (void)u; return map_in(vram_ref, fast_ref, a, len); }
static ogpu_u8 *map_gpu(void *u, ogpu_u32 a, ogpu_u32 len) { (void)u; return map_in(vram_gpu, fast_gpu, a, len); }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; if (failures < 30) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- the emulated device -------------------------------------------------------------------- */

static struct ogpu3d_rec *rec;
static int recorded;                /* dispatches since the last flush */
static long dispatches;

static void emu_dispatch(void *user, const unsigned int *pc, unsigned gx, unsigned gy) {
    unsigned x, y, lx, ly;
    (void)user;
    memcpy(g_pc, pc, sizeof g_pc);
    for (y = 0; y < gy; y++)
        for (x = 0; x < gx; x++)
            for (ly = 0; ly < 8; ly++)
                for (lx = 0; lx < 8; lx++) o3x_tile(x, y, lx, ly);
    recorded++;
    dispatches++;
}
static void emu_flush(void *user) {
    (void)user;
    ogpu3d_rec_close(rec);
    recorded = 0;
    ogpu3d_rec_done(rec, 0);
}
static int emu_pending(void *user) { (void)user; return recorded > 0; }

static void make_rec(const char *mode, unsigned long stage, unsigned long tex, unsigned long data) {
    struct ogpu3d_rec_cfg cfg;
    if (rec) ogpu3d_rec_destroy(rec);
    setenv("OGPU_VK_3D", mode, 1);
    memset(&cfg, 0, sizeof cfg);
    cfg.vram = vram_gpu; cfg.vram_size = REG;
    cfg.stage = stage_buf; cfg.stage_size = stage;
    cfg.tex = tex_buf; cfg.tex_size = tex;
    cfg.data = data_buf; cfg.data_size = data;
    cfg.dispatch = emu_dispatch; cfg.flush = emu_flush; cfg.pending = emu_pending;
    cfg.round_trip_ns = 90000;
    rec = ogpu3d_rec_create(&cfg);
    g_vram = (uint *)vram_gpu; g_stage = (uint *)stage_buf; g_tex = (uint *)tex_buf; g_data = (uint *)data_buf;
}

/* ---- the two paths --------------------------------------------------------------------------- */

static struct ogpu_core C_ref, C_gpu;
static struct ogpu3d T_ref, T_gpu;

static void fresh(void) {
    ogpu_core_init(&C_ref); C_ref.map = map_ref; C_ref.fence = 0; C_ref.ext = 0; C_ref.user = 0;
    ogpu3d_init(&T_ref, &C_ref); C_ref.d3 = &T_ref;
    ogpu_core_init(&C_gpu); C_gpu.map = map_gpu; C_gpu.fence = 0; C_gpu.ext = 0; C_gpu.user = 0;
    ogpu3d_init(&T_gpu, &C_gpu); C_gpu.d3 = &T_gpu;
}

static ogpu_u32 rd32(const ogpu_u8 *p) { return ((ogpu_u32)p[0] << 24) | ((ogpu_u32)p[1] << 16) | ((ogpu_u32)p[2] << 8) | p[3]; }

/* As ogpu_vk_run carries a stream: the 3D range through the recorder, the
 * rest on the core once the GPU has finished. */
static long gpu_run(const ogpu_u8 *s, long words, int *err, long *err_at) {
    long at = 0, done = 0;
    *err = OGPU_OK; *err_at = 0;
    ogpu3d_rec_cpu_wrote(rec);
    while (at < words) {
        ogpu_u32 hdr = rd32(s + at * 4);
        int op = (int)OGPU_HDR_OP(hdr), len = (int)OGPU_HDR_WORDS(hdr), r = OGPU_OK, handled = 0;
        if (len < 1 || at + len > words) { if (*err == OGPU_OK) { *err = OGPU_ERR_BADLEN; *err_at = at; } break; }
        if (op >= OGPU_OP_3D_FIRST && op <= OGPU_OP_3D_LAST) {
            if (op == OGPU_OP_TRIANGLES) handled = ogpu3d_rec_triangles(rec, &T_gpu, s + at * 4, len, &r);
            else if (op != OGPU_OP_CLEAR3D || (len >= 2 && (rd32(s + at * 4 + 4) & OGPU_KIND_READBACK))) {
                r = ogpu3d_op(&T_gpu, op, s + at * 4, len);
                handled = 1;
            }
        }
        if (handled) {
            if (r != OGPU_OK && *err == OGPU_OK) { *err = r; *err_at = at; }
            done++;
        } else {
            if (op != OGPU_OP_SURFACE && op != OGPU_OP_TARGET && op != OGPU_OP_CLIP && op != OGPU_OP_NOP) emu_flush(0);
            done += ogpu_core_run(&C_gpu, s + at * 4, len);
            if (C_gpu.last_error != OGPU_OK && *err == OGPU_OK) { *err = C_gpu.last_error; *err_at = at; }
            if (C_gpu.last_error == OGPU_ERR_BADLEN) break;
            if (op >= OGPU_OP_FILL) ogpu3d_rec_cpu_wrote(rec);
        }
        at += len;
    }
    emu_flush(0);
    return done;
}

/* The stream on both; 1 when memory and results agree. */
static int both(const ogpu_u8 *s_ref, const ogpu_u8 *s_gpu, long words, const char *what) {
    long d_ref, d_gpu, e_at, i;
    int e, ok = 1;
    d_ref = ogpu_core_run(&C_ref, s_ref, words);
    d_gpu = gpu_run(s_gpu, words, &e, &e_at);
    if (d_ref != d_gpu || C_ref.last_error != e || (e != OGPU_OK && C_ref.error_word != e_at)) {
        CHECK(0, "%s: done %ld/%ld, error %d/%d at %ld/%ld", what, d_ref, d_gpu, C_ref.last_error, e, C_ref.error_word, e_at);
        ok = 0;
    }
    if (memcmp(vram_ref, vram_gpu, REG) || memcmp(fast_ref, fast_gpu, REG)) {
        for (i = 0; i < REG && vram_ref[i] == vram_gpu[i]; i++) {}
        if (i < REG) CHECK(0, "%s: video RAM differs first at %#lx (%02x, gpu %02x)", what, i, vram_ref[i], vram_gpu[i]);
        else {
            for (i = 0; i < REG && fast_ref[i] == fast_gpu[i]; i++) {}
            CHECK(0, "%s: other memory differs first at %#lx (%02x, gpu %02x)", what, i, fast_ref[i], fast_gpu[i]);
        }
        if (getenv("OGPU3D_SEED")) {
            int nd = 0;
            for (i = 0; i < REG && nd < 40; i++)
                if (vram_ref[i] != vram_gpu[i]) { printf("  vram %#lx: %02x gpu %02x\n", i, vram_ref[i], vram_gpu[i]); nd++; }
            for (i = 0; i < REG && nd < 80; i++)
                if (fast_ref[i] != fast_gpu[i]) { printf("  fast %#lx: %02x gpu %02x\n", i, fast_ref[i], fast_gpu[i]); nd++; }
        }
        ok = 0;
    }
    return ok;
}

static struct ogpu_scene3d_env env_ref, env_gpu;
static ogpu_u8 sbuf_ref[1 << 16];

static int random_scene(unsigned long seed, const char *what) {
    static int dirty;
    long words, w2;
    const unsigned char *s;
    char name[96];
    if (dirty) { memset(vram_ref, 0, REG); memset(fast_ref, 0, REG); memset(vram_gpu, 0, REG); memset(fast_gpu, 0, REG); }
    s = ogpu_scene3d_random(&env_ref, seed, &words);
    memcpy(sbuf_ref, s, (size_t)words * 4);
    s = ogpu_scene3d_random(&env_gpu, seed, &w2);
    fresh();
    snprintf(name, sizeof name, "%s, random scene %lu", what, seed);
    dirty = !both(sbuf_ref, s, words, name);
    return !dirty;
}

static int bench_scene(int which, const char *what) {
    long words, w2;
    const unsigned char *s;
    const char *name;
    char nm[96];
    s = ogpu_scene3d_bench(&env_ref, which, &words, &name);
    memcpy(sbuf_ref, s, (size_t)words * 4);
    s = ogpu_scene3d_bench(&env_gpu, which, &w2, &name);
    snprintf(nm, sizeof nm, "%s, %s", what, name);
    return both(sbuf_ref, s, words, nm);
}

/* The golden scene on the GPU path: its checksum. */
static long golden_run(void *user, const unsigned char *s, long words) {
    int e;
    long at;
    (void)user;
    fresh();
    gpu_run(s, words, &e, &at);
    return e;
}

int main(int argc, char **argv) {
    long rounds = argc > 2 ? atol(argv[2]) : 2000, n;
    int k, bad = 0;
    struct ogpu3d_rec_stats st;
    vram_ref = calloc(1, REG); fast_ref = calloc(1, REG); vram_gpu = calloc(1, REG); fast_gpu = calloc(1, REG);
    stage_buf = calloc(1, 8 << 20); tex_buf = calloc(1, 8 << 20); data_buf = calloc(1, 8 << 20);
    if (!vram_ref || !fast_ref || !vram_gpu || !fast_gpu || !stage_buf || !tex_buf || !data_buf) { printf("out of memory\n"); return 1; }
    env_ref.mem[0] = vram_ref; env_ref.mem[1] = fast_ref; env_ref.addr[0] = VBASE; env_ref.addr[1] = FBASE;
    env_gpu.mem[0] = vram_gpu; env_gpu.mem[1] = fast_gpu; env_gpu.addr[0] = VBASE; env_gpu.addr[1] = FBASE;

    /* the golden scene through the GPU path */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        unsigned long want = 0, h;
        struct ogpu_scene_env e;
        if (!f || fscanf(f, "%lx", &want) != 1) { printf("cannot read %s\n", argv[1]); return 1; }
        fclose(f);
        make_rec("gpu", 8 << 20, 8 << 20, 8 << 20);
        memset(&e, 0, sizeof e);
        e.arena = vram_gpu; e.addr = VBASE; e.run = golden_run;
        h = ogpu_golden_3d(&e);
        CHECK(h == want && !e.failures, "golden scene on the GPU path: %08lx, golden file %08lx (%d batches failed)", h, want, e.failures);
        printf("ogpu3d emulated: golden scene %08lx (%s)\n", h, h == want ? "the golden checksum" : "WRONG");
        memset(vram_gpu, 0, REG);
    }
    if (getenv("OGPU3D_SEED")) {            /* one scene, with its stream listed */
        unsigned long seed = strtoul(getenv("OGPU3D_SEED"), 0, 0);
        long words, i;
        const unsigned char *s = ogpu_scene3d_random(&env_ref, seed, &words);
        for (i = 0; i < words; ) {
            ogpu_u32 h = rd32(s + i * 4);
            int len = (int)OGPU_HDR_WORDS(h), j;
            printf("%04lx op %02x:", i, (unsigned)OGPU_HDR_OP(h));
            for (j = 1; j < len && j < 24; j++) printf(" %08lx", rd32(s + (i + j) * 4));
            printf("\n");
            if (len < 1) break;
            i += len;
        }
        make_rec(getenv("OGPU_VK_3D") ? getenv("OGPU_VK_3D") : "gpu", 8 << 20, 8 << 20, 8 << 20);
        random_scene(seed, "one");
        return failures != 0;
    }

    /* every random scene drawn by the shader (OGPU_VK_3D=gpu), then with small buffers */
    make_rec("gpu", 8 << 20, 8 << 20, 8 << 20);
    for (n = 0; n < rounds; n++) if (!random_scene((unsigned long)n, "gpu") && ++bad > 5) break;
    ogpu3d_rec_stats(rec, &st);
    printf("ogpu3d emulated: %ld random scenes on the GPU path: %lu commands on the GPU, %lu declined, %lu triangles, %lu pixels, %lu groups, %ld dispatches\n",
           n, st.gpu, st.declined, st.triangles, st.pixels, st.groups, dispatches);
    CHECK(st.gpu > (unsigned long)rounds && st.lost == 0, "the GPU path took %lu commands, lost %lu", st.gpu, st.lost);
    make_rec("gpu", 96 << 10, 48 << 10, 160 << 10);
    for (n = 0; n < rounds / 2; n++) if (!random_scene(100000UL + (unsigned long)n, "small buffers") && ++bad > 10) break;
    ogpu3d_rec_stats(rec, &st);
    printf("ogpu3d emulated: %ld random scenes with small buffers: %lu on the GPU, %lu declined, %lu texture uploads, %lu hits\n",
           n, st.gpu, st.declined, st.tex_uploads, st.tex_hits);
    make_rec("auto", 8 << 20, 8 << 20, 8 << 20);
    for (n = 0; n < rounds / 4; n++) if (!random_scene(200000UL + (unsigned long)n, "auto") && ++bad > 15) break;

    /* the bench scenes, twice (the second time the texture cache is checked and used), then with the texture changed */
    for (k = 0; k < 2; k++) {
        int which;
        make_rec(k ? "auto" : "gpu", 8 << 20, 8 << 20, 8 << 20);
        memset(vram_ref, 0, REG); memset(fast_ref, 0, REG); memset(vram_gpu, 0, REG); memset(fast_gpu, 0, REG);
        for (which = 0; which < OGPU_SCENE3D_BENCHES; which++) {
            fresh();
            bench_scene(which, k ? "auto" : "gpu");
            bench_scene(which, k ? "auto, again" : "gpu, again");
        }
        ogpu3d_rec_stats(rec, &st);
        printf("ogpu3d emulated: bench scenes (%s): %lu commands on the GPU, %lu on the core, %lu texture uploads, %lu checked, %lu hits, %lu staged rows\n",
               k ? "auto" : "gpu", st.gpu, st.cpu, st.tex_uploads, st.tex_checks, st.tex_hits, st.staged_rows);
        if (!k) CHECK(st.tex_checks > 0 && st.staged_rows > 0, "the cache was checked (%lu) and surfaces staged (%lu rows)", st.tex_checks, st.staged_rows);
    }
    {
        /* a texture the 68k changed between runs: drawn with the new texels */
        long words;
        const unsigned char *s;
        const char *name;
        make_rec("gpu", 8 << 20, 8 << 20, 8 << 20);
        fresh();
        bench_scene(0, "texture before");
        s = ogpu_scene3d_bench(&env_ref, 0, &words, &name);
        memcpy(sbuf_ref, s, (size_t)words * 4);
        s = ogpu_scene3d_bench(&env_gpu, 0, &words, &name);
        memset(fast_ref + 0x100000L + 4000, 0x5A, 3000);
        memset(fast_gpu + 0x100000L + 4000, 0x5A, 3000);
        both(sbuf_ref, s, words, "texture changed between runs");
        ogpu3d_rec_stats(rec, &st);
        CHECK(st.tex_uploads == 2, "the changed texture went up again (%lu uploads)", st.tex_uploads);
    }
    if (failures) { printf("ogpu3d emulated: %d FAILED\n", failures); return 1; }
    printf("ogpu3d emulated: all tests passed\n");
    return 0;
}
