/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's Vulkan back end (ogpu_vk.h). The stream is read here; state
 * commands (SURFACE, TARGET, CLIP) and anything the GPU path doesn't take go
 * to a C core that lives inside this back end, so slots, the clip and every
 * error are the core's own. Drawing commands the GPU takes become one
 * compute dispatch each, recorded into one command buffer and sent when the
 * run ends, a FENCE comes, the C core needs the memory, or the upload arena
 * is full. A barrier between dispatches keeps them in stream order.
 *
 * Built with OGPU_WITH_3D (as ogpu_core.c, with ogpu_3d.c linked), the core
 * here has its own 3D state (struct ogpu3d), so stream v1.2's 3D range
 * (TRIANGLES, RENDER3D, TEXENV, TEXTURE, DEPTH, CLEAR3D) runs on it as on
 * the CPU path: the state lasts from run to run, and a reset starts it
 * afresh. Built without it, those opcodes are OGPU_ERR_BADOP, and
 * ogpu_vk_supports says so.
 *
 * Stream v1.2's FILL_BLEND, COMPOSITE's blend modes (OGPU_COMP_BLENDMODE),
 * COMPOSITE_AFFINE and YUV are on the GPU path too; LINES_BLEND and
 * POINTS_BLEND stay on the core (one GPU thread walking them waits on every
 * pixel, as LINE does).
 *
 * Routing (docs/design/Design-OpenGPU-2D-On-The-GPU.md 5.2, in amigachrome):
 * a command the GPU path takes is drawn there only where that is faster,
 * its wait included (config's route, route_gpu below).
 *
 * The composite list (the same design, 5.3): a run of FILL, INVERT,
 * COMPOSITE, COMPOSITE_AFFINE and FILL_BLEND on one target becomes one
 * dispatch with no barriers inside, each GPU thread applying the entries
 * that cover its pixels in stream order (ogpu.comp's list_main). A list
 * ends before an entry that reads bytes an earlier entry writes, or writes
 * bytes an earlier one reads, since threads of one dispatch don't wait for
 * each other; and before anything else is drawn. */
#define _DEFAULT_SOURCE  /* mmap's MAP_ANONYMOUS under -std=c99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define OGPU_VK_MMAP 1
#endif
#include "ogpu_vk.h"
#ifdef OGPU_WITH_3D
#include "../../library/opengpu/ogpu_3d.h"
#endif
#include "ogpu_vk_spv.h"    /* ogpu_comp_spv[]: ogpu.comp compiled (host/vulkan/build_spv.sh) */

struct ogpu_vk {
    struct ogpu_vk_config cfg;
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue queue;
    VkBuffer vram_buf, arena_buf;
    VkDeviceMemory vram_mem, arena_mem;
    ogpu_u8 *vram, *arena;
    const char *vram_mode;      /* "own", "import" or "map" */
    int vram_mapped;            /* "map": host_vram is our mapping of the GPU's memory */
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkShaderModule shader;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet dset;
    VkCommandPool cpool;
    VkCommandBuffer cmd;
    VkFence done;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

    struct ogpu_core core;      /* the state, and the CPU path */
#ifdef OGPU_WITH_3D
    struct ogpu3d d3;           /* the core's 3D state: the 3D range runs on the core */
#endif
    int recording;              /* cmd is open */
    long dispatches;            /* recorded since it opened */
    ogpu_u32 arena_used;
    int failed;                 /* a submit failed: the GPU path is off */
    int route;                  /* OGPU_VK_ROUTE_ in force */
    double trip;                /* microseconds a submit and its wait take here (route_gpu) */
    struct vk_entry *list;      /* the composite list being gathered (LIST_MAX entries) */
    int list_n;
    unsigned long list_reserve; /* arena bytes it takes when it is sent (list_bytes) */
    int lost_now;               /* ... during this run, so its result is OGPU_ERR_DEVICE */
    int last_error;
    long error_word;
    struct ogpu_vk_stats stats;
};

/* A composite list's entry: its arguments as the shader takes them, and the
 * bytes of video RAM it writes (d) and reads (s, its source; m, its mask),
 * or ranges that are empty. */
#define LIST_MAX 256
#define LIST_TILE 64
#define OP_LIST 0x100
struct vk_entry { ogpu_u32 args[32]; unsigned long d0, d1, s0, s1, m0, m1; };

/* The shader's push constants (ogpu.comp's A_ names). */
enum {
    A_OP, A_DST, A_DBPR, A_DFMT, A_X, A_Y, A_W, A_H, A_OX, A_OY, A_SRC, A_SBUF, A_SBPR, A_SFMT,
    A_FG, A_BG, A_MODE, A_FIRST, A_TABLE, A_PAD19, A_SX, A_SY, A_SW, A_SH, A_STEPX, A_STEPY,
    A_ALPHA, A_FLAGS, A_X0, A_Y0, A_X1, A_Y1, A_COUNT
};
/* COMPOSITE's mask (v1.1) shares LINE's words. */
#define A_MSRC A_X0
#define A_MBPR A_Y0
#define A_MX   A_X1
#define A_MY   A_Y1
#define SBUF_SRC_ARENA   1u
#define SBUF_DST_ARENA   2u
#define SBUF_TABLE_ARENA 4u
#define SBUF_MASK_ARENA  8u

/* ---- the stream's words and the core's rules (as ogpu_core.c) ---------------------- */

static ogpu_u32 rd32(const ogpu_u8 *p) {
    return ((ogpu_u32)p[0] << 24) | ((ogpu_u32)p[1] << 16) | ((ogpu_u32)p[2] << 8) | (ogpu_u32)p[3];
}
static int s16(ogpu_u32 v) { v &= 0xFFFFUL; return v >= 0x8000UL ? (int)v - 0x10000 : (int)v; }
static int hi_s(ogpu_u32 w) { return s16(w >> 16); }
static int lo_s(ogpu_u32 w) { return s16(w); }
static int hi_u(ogpu_u32 w) { return (int)((w >> 16) & 0xFFFFUL); }
static int lo_u(ogpu_u32 w) { return (int)(w & 0xFFFFUL); }

static int bytes_per_pixel(int format) {
    switch (format) {
    case OGPU_FMT_CLUT8: case OGPU_FMT_INDEX8: case OGPU_FMT_A8: return 1;
    case OGPU_FMT_RGB565: return 2;
    case OGPU_FMT_ARGB32: return 4;
    }
    return 0;
}

static ogpu_u32 flip_mask(int format) {
    return format == OGPU_FMT_RGB565 ? 0xFFFFUL : format == OGPU_FMT_ARGB32 ? 0x00FFFFFFUL : 0xFFUL;
}

static int args_for(int op) {
    switch (op) {
    case OGPU_OP_NOP: return 0;
    case OGPU_OP_SURFACE: return 5;
    case OGPU_OP_TARGET: return 1;
    case OGPU_OP_CLIP: return 2;
    case OGPU_OP_FILL: case OGPU_OP_INVERT: return 3;
    case OGPU_OP_COPY: return 4;
    case OGPU_OP_TEMPLATE: return 8;
    case OGPU_OP_PATTERN: return 7;
    case OGPU_OP_LINE: return 4;
    case OGPU_OP_PIXELS: return 6;
    case OGPU_OP_MASK: return 5;
    case OGPU_OP_COMPOSITE: return 7;
    case OGPU_OP_FILL_BLEND: case OGPU_OP_LINES_BLEND: case OGPU_OP_POINTS_BLEND: return 4;
    case OGPU_OP_COMPOSITE_AFFINE: return 11;
    case OGPU_OP_YUV: return 9;
    case OGPU_OP_FENCE: return 1;
    }
    return -1;     /* the 3D range, the GPU APIs' and the rest: the core's, length and all */
}

/* The drawing commands the GPU path takes (gpu_draw). */
static int gpu_op(int op) {
    switch (op) {
    case OGPU_OP_FILL: case OGPU_OP_INVERT: case OGPU_OP_COPY: case OGPU_OP_TEMPLATE: case OGPU_OP_PATTERN:
    case OGPU_OP_LINE: case OGPU_OP_PIXELS: case OGPU_OP_MASK: case OGPU_OP_COMPOSITE:
    case OGPU_OP_FILL_BLEND: case OGPU_OP_COMPOSITE_AFFINE: case OGPU_OP_YUV:
        return 1;
    }
    return 0;
}

static int clip_rect(const struct ogpu_core *c, int *x, int *y, int *w, int *h, int *ox, int *oy) {
    const struct ogpu_surface *t = &c->slot[c->target];
    int x0 = 0, y0 = 0, x1 = t->w, y1 = t->h, nx, ny, nx1, ny1;
    if (c->cx1 > c->cx0) {
        if (c->cx0 > x0) x0 = c->cx0;
        if (c->cy0 > y0) y0 = c->cy0;
        if (c->cx1 < x1) x1 = c->cx1;
        if (c->cy1 < y1) y1 = c->cy1;
    }
    nx = *x < x0 ? x0 : *x;
    ny = *y < y0 ? y0 : *y;
    nx1 = *x + *w > x1 ? x1 : *x + *w;
    ny1 = *y + *h > y1 ? y1 : *y + *h;
    if (nx1 <= nx || ny1 <= ny) return 0;
    *ox = nx - *x; *oy = ny - *y;
    *x = nx; *y = ny; *w = nx1 - nx; *h = ny1 - ny;
    return 1;
}

/* ---- memory ------------------------------------------------------------------------ */

/* Where a host pointer the core mapped lies in video RAM: its offset, or -1. */
static long vram_off(const struct ogpu_vk *vk, const ogpu_u8 *p, unsigned long len) {
    if (p < vk->vram || p > vk->vram + vk->cfg.vram_size) return -1;
    if ((unsigned long)(p - vk->vram) + len > vk->cfg.vram_size) return -1;
    return (long)(p - vk->vram);
}

/* The core's map: video RAM by its address, everything else as the caller maps it. */
static ogpu_u8 *core_map(void *user, ogpu_u32 address, ogpu_u32 length) {
    struct ogpu_vk *vk = user;
    ogpu_u32 base = vk->cfg.vram_base, size = vk->cfg.vram_size;
    if (address >= base && address - base <= size && length <= size - (address - base))
        return vk->vram + (address - base);
    return vk->cfg.map ? vk->cfg.map(vk->cfg.user, address, length) : 0;
}

static void flush(struct ogpu_vk *vk);

static int core_ext(void *user, int op, const ogpu_u8 *cmd, long words) {
    struct ogpu_vk *vk = user;
    return vk->cfg.ext ? vk->cfg.ext(vk->cfg.user, op, cmd, words) : OGPU_ERR_BADOP;
}

static void core_fence(void *user, ogpu_u32 id) {
    struct ogpu_vk *vk = user;
    flush(vk);
    if (vk->cfg.fence) vk->cfg.fence(vk->cfg.user, id);
}

/* Room for `bytes` more in the arena: sends what is recorded when it's full.
 * 0 when it can never fit. */
static int arena_room(struct ogpu_vk *vk, unsigned long bytes) {
    unsigned long size = vk->cfg.arena_size;
    if (bytes > size) return 0;
    /* the composite list's own bytes stay free for it: a flush sends it first */
    if (vk->arena_used + bytes + vk->list_reserve > size) flush(vk);
    return 1;
}

/* Take `bytes` of the arena (room already made): its offset, 4-aligned. */
static ogpu_u32 arena_take(struct ogpu_vk *vk, unsigned long bytes) {
    ogpu_u32 at = vk->arena_used;
    vk->arena_used += (ogpu_u32)((bytes + 3) & ~3UL);
    return at;
}

/* ---- recording -------------------------------------------------------------------- */

static void begin(struct ogpu_vk *vk) {
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vk->cmd, &bi);
    vkCmdBindPipeline(vk->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipe);
    vkCmdBindDescriptorSets(vk->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk->layout, 0, 1, &vk->dset, 0, 0);
    vk->recording = 1;
    vk->dispatches = 0;
}

static void list_send(struct ogpu_vk *vk);
static int list_add(struct ogpu_vk *vk, const ogpu_u32 *args, int bpp);

static void dispatch(struct ogpu_vk *vk, const ogpu_u32 *args, unsigned gx, unsigned gy) {
    uint32_t pc[A_COUNT];
    int i;
    if (vk->list_n) list_send(vk);      /* the list was first in the stream */
    if (!vk->recording) begin(vk);
    if (vk->dispatches) {
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(vk->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, 0, 0, 0);
    }
    for (i = 0; i < A_COUNT; i++) pc[i] = (uint32_t)args[i];
    vkCmdPushConstants(vk->cmd, vk->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(vk->cmd, gx ? gx : 1, gy ? gy : 1, 1);
    vk->dispatches++;
}

/* A rectangle w x h of bpp-byte pixels: a word per invocation, 64 to a group. */
static void dispatch_rect(struct ogpu_vk *vk, const ogpu_u32 *args, int w, int h, int bpp) {
    unsigned words = (unsigned)(((long)w * bpp + 3) / 4 + 1);
    vk->stats.gpu++;
    if (list_add(vk, args, bpp)) return;
    dispatch(vk, args, (words + 63) / 64, (unsigned)h);
}

/* ---- the composite list --------------------------------------------------------------- */

static int listable(ogpu_u32 op) {
    return op == OGPU_OP_FILL || op == OGPU_OP_INVERT || op == OGPU_OP_COMPOSITE || op == OGPU_OP_COMPOSITE_AFFINE
           || op == OGPU_OP_FILL_BLEND;
}

static int ranges_meet(unsigned long a0, unsigned long a1, unsigned long b0, unsigned long b1) {
    return a0 < a1 && b0 < b1 && a0 < b1 && b0 < a1;
}

/* The list's bounding box over its first n entries and e (or NULL). */
static void list_box(const struct ogpu_vk *vk, int n, const struct vk_entry *e, int *x0, int *y0, int *x1, int *y1) {
    int i;
    *x0 = *y0 = 0x7FFFFFFF; *x1 = *y1 = -0x7FFFFFFF;
    for (i = 0; i <= n; i++) {
        const ogpu_u32 *a = i < n ? vk->list[i].args : e ? e->args : 0;
        if (!a) break;
        if ((int)a[A_X] < *x0) *x0 = (int)a[A_X];
        if ((int)a[A_Y] < *y0) *y0 = (int)a[A_Y];
        if ((int)(a[A_X] + a[A_W]) > *x1) *x1 = (int)(a[A_X] + a[A_W]);
        if ((int)(a[A_Y] + a[A_H]) > *y1) *y1 = (int)(a[A_Y] + a[A_H]);
    }
}

/* The arena bytes a list of the first n entries and e takes when it is
 * sent: the entries, the tile table and each tile's entries. */
static unsigned long list_bytes(const struct ogpu_vk *vk, int n, const struct vk_entry *e) {
    int x0, y0, x1, y1, i;
    unsigned long refs = 0, across, down;
    list_box(vk, n, e, &x0, &y0, &x1, &y1);
    across = (unsigned long)(x1 - x0 + LIST_TILE - 1) / LIST_TILE;
    down = (unsigned long)(y1 - y0 + LIST_TILE - 1) / LIST_TILE;
    for (i = 0; i <= n; i++) {
        const ogpu_u32 *a = i < n ? vk->list[i].args : e ? e->args : 0;
        if (!a) break;
        refs += (unsigned long)(((int)(a[A_X] + a[A_W] - 1) - x0) / LIST_TILE - ((int)a[A_X] - x0) / LIST_TILE + 1)
              * (unsigned long)(((int)(a[A_Y] + a[A_H] - 1) - y0) / LIST_TILE - ((int)a[A_Y] - y0) / LIST_TILE + 1);
    }
    return (unsigned long)(n + (e ? 1 : 0)) * 128 + (across * down + 1 + refs) * 4;
}

/* Send the list as one dispatch (a list of one as its own). Its bytes were
 * kept free in the arena. */
static void list_send(struct ogpu_vk *vk) {
    int n = vk->list_n, x0, y0, x1, y1, i;
    ogpu_u32 args[A_COUNT], at, table, refs;
    unsigned long across, down, tiles, k, *fill;
    if (!n) return;
    vk->list_n = 0;
    vk->list_reserve = 0;
    if (n == 1) {
        const ogpu_u32 *a = vk->list[0].args;
        unsigned words = (unsigned)(((long)a[A_W] * bytes_per_pixel((int)a[A_DFMT]) + 3) / 4 + 1);
        dispatch(vk, a, (words + 63) / 64, a[A_H]);
        return;
    }
    list_box(vk, n, 0, &x0, &y0, &x1, &y1);
    across = (unsigned long)(x1 - x0 + LIST_TILE - 1) / LIST_TILE;
    down = (unsigned long)(y1 - y0 + LIST_TILE - 1) / LIST_TILE;
    tiles = across * down;
    at = arena_take(vk, list_bytes(vk, n, 0));
    table = at + (ogpu_u32)n * 128;
    refs = table + (ogpu_u32)(tiles + 1) * 4;
    fill = calloc(tiles + 1, sizeof *fill);
    for (i = 0; i < n; i++) {           /* the entries, as words, and how many each tile has */
        const ogpu_u32 *a = vk->list[i].args;
        uint32_t *w = (uint32_t *)(void *)(vk->arena + at + (unsigned long)i * 128);   /* the host's order: the GPU's */
        unsigned long tx, ty;
        int q;
        for (q = 0; q < 32; q++) w[q] = (uint32_t)a[q];
        for (ty = (unsigned long)((int)a[A_Y] - y0) / LIST_TILE; ty <= (unsigned long)((int)(a[A_Y] + a[A_H] - 1) - y0) / LIST_TILE; ty++)
            for (tx = (unsigned long)((int)a[A_X] - x0) / LIST_TILE; tx <= (unsigned long)((int)(a[A_X] + a[A_W] - 1) - x0) / LIST_TILE; tx++)
                if (fill) fill[ty * across + tx + 1]++;
    }
    if (fill) {
        ogpu_u32 *start = (ogpu_u32 *)(void *)(vk->arena + table);
        for (k = 1; k <= tiles; k++) fill[k] += fill[k - 1];
        for (k = 0; k <= tiles; k++) start[k] = refs / 4 + (ogpu_u32)fill[k];
        for (i = 0; i < n; i++) {       /* each tile's entries, in stream order */
            const ogpu_u32 *a = vk->list[i].args;
            unsigned long tx, ty;
            for (ty = (unsigned long)((int)a[A_Y] - y0) / LIST_TILE; ty <= (unsigned long)((int)(a[A_Y] + a[A_H] - 1) - y0) / LIST_TILE; ty++)
                for (tx = (unsigned long)((int)a[A_X] - x0) / LIST_TILE; tx <= (unsigned long)((int)(a[A_X] + a[A_W] - 1) - x0) / LIST_TILE; tx++)
                    ((ogpu_u32 *)(void *)(vk->arena + refs))[fill[ty * across + tx]++] = (at + (ogpu_u32)i * 128) / 4;
        }
        free(fill);
    }
    memset(args, 0, sizeof args);
    args[A_OP] = OP_LIST;
    args[A_DST] = vk->list[0].args[A_DST]; args[A_DBPR] = vk->list[0].args[A_DBPR]; args[A_DFMT] = vk->list[0].args[A_DFMT];
    args[A_X] = (ogpu_u32)x0; args[A_Y] = (ogpu_u32)y0; args[A_W] = (ogpu_u32)(x1 - x0); args[A_H] = (ogpu_u32)(y1 - y0);
    args[A_FIRST] = table / 4;
    args[A_SW] = (ogpu_u32)across;
    {
        unsigned words = (unsigned)(((long)(x1 - x0) * bytes_per_pixel((int)args[A_DFMT]) + 3) / 4 + 1);
        dispatch(vk, args, (words + 63) / 64, (unsigned)(y1 - y0));
    }
    vk->stats.lists++;
    vk->stats.listed += (unsigned long)n;
}

/* The command (args, as for dispatch_rect) onto the list, ending the list
 * first when it can't join it. 0: not a list's (it is dispatched alone). */
static int list_add(struct ogpu_vk *vk, const ogpu_u32 *args, int bpp) {
    struct vk_entry e;
    unsigned long size = vk->cfg.arena_size;
    int i, sbpp;
    if (!vk->list || !listable(args[A_OP]) || (args[A_SBUF] & SBUF_DST_ARENA)) return 0;
    memcpy(e.args, args, sizeof e.args);
    e.d0 = args[A_DST] + args[A_Y] * args[A_DBPR] + args[A_X] * (unsigned long)bpp;
    e.d1 = args[A_DST] + (args[A_Y] + args[A_H] - 1) * args[A_DBPR] + (args[A_X] + args[A_W]) * (unsigned long)bpp;
    e.s0 = e.s1 = e.m0 = e.m1 = 0;
    if ((args[A_OP] == OGPU_OP_COMPOSITE || args[A_OP] == OGPU_OP_COMPOSITE_AFFINE) && !(args[A_SBUF] & SBUF_SRC_ARENA)) {
        sbpp = bytes_per_pixel((int)args[A_SFMT]);
        e.s0 = args[A_SRC] + args[A_SY] * args[A_SBPR] + args[A_SX] * (unsigned long)sbpp;
        e.s1 = args[A_SRC] + (args[A_SY] + args[A_SH] - 1) * args[A_SBPR] + (args[A_SX] + args[A_SW]) * (unsigned long)sbpp;
    }
    if (args[A_OP] == OGPU_OP_COMPOSITE && (args[A_FLAGS] & OGPU_COMP_MASK) && !(args[A_SBUF] & SBUF_MASK_ARENA)) {
        e.m0 = args[A_MSRC] + (args[A_MY] + args[A_OY]) * args[A_MBPR] + args[A_MX] + args[A_OX];
        e.m1 = args[A_MSRC] + (args[A_MY] + args[A_OY] + args[A_H] - 1) * args[A_MBPR] + args[A_MX] + args[A_OX] + args[A_W];
    }
    if (vk->list_n) {
        int end = vk->list_n == LIST_MAX || vk->list[0].args[A_DST] != args[A_DST] || vk->list[0].args[A_DBPR] != args[A_DBPR]
                  || vk->list[0].args[A_DFMT] != args[A_DFMT];
        for (i = 0; !end && i < vk->list_n; i++) {
            const struct vk_entry *o = &vk->list[i];
            end = ranges_meet(e.s0, e.s1, o->d0, o->d1) || ranges_meet(e.m0, e.m1, o->d0, o->d1)     /* reads what it wrote */
                  || ranges_meet(e.d0, e.d1, o->s0, o->s1) || ranges_meet(e.d0, e.d1, o->m0, o->m1);   /* writes what it read */
        }
        if (!end && vk->arena_used + list_bytes(vk, vk->list_n, &e) > size) end = 1;
        if (end) list_send(vk);
    }
    if (vk->arena_used + list_bytes(vk, vk->list_n, &e) > size) return 0;
    vk->list[vk->list_n++] = e;
    vk->list_reserve = list_bytes(vk, vk->list_n, 0);
    return 1;
}

static void flush(struct ogpu_vk *vk) {
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    if (vk->list_n) list_send(vk);
    if (!vk->recording) { vk->arena_used = 0; return; }
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    vkCmdPipelineBarrier(vk->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, 0, 0, 0);
    vkEndCommandBuffer(vk->cmd);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &vk->cmd;
    if (vkQueueSubmit(vk->queue, 1, &si, vk->done) != VK_SUCCESS
        || vkWaitForFences(vk->dev, 1, &vk->done, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        /* The device is lost: what was recorded may be partly drawn, and the
         * run says so; from here on the C core draws everything. */
        vk->failed = 1;
        vk->lost_now = 1;
    }
    vkResetFences(vk->dev, 1, &vk->done);
    vkResetCommandPool(vk->dev, vk->cpool, 0);
    vk->recording = 0;
    vk->arena_used = 0;
    vk->stats.submits++;
}

/* ---- the drawing commands on the GPU ------------------------------------------------ */

/* The target, when the GPU can draw in it: in video RAM, its address and
 * row length multiples of its pixel size (so no pixel straddles a word). */
static const struct ogpu_surface *gpu_target(const struct ogpu_vk *vk, ogpu_u32 *off) {
    const struct ogpu_surface *t;
    long o, bpp;
    if (vk->core.target < 0) return 0;
    t = &vk->core.slot[vk->core.target];
    if (!t->pixels) return 0;
    bpp = bytes_per_pixel(t->format);
    if (!bpp) return 0;
    o = vram_off(vk, t->pixels, (unsigned long)(t->bpr * (t->h - 1) + (long)t->w * bpp));
    if (o < 0 || o % bpp || t->bpr % bpp) return 0;
    *off = (ogpu_u32)o;
    return t;
}

/* A source the core can map: in video RAM (read in place, in stream order)
 * or copied into the arena (room already made). Its place in args. */
static int place(struct ogpu_vk *vk, const ogpu_u8 *p, unsigned long len, ogpu_u32 *off, ogpu_u32 *in_arena) {
    long o = vram_off(vk, p, len);
    if (o >= 0) { *off = (ogpu_u32)o; *in_arena = 0; return 1; }
    *off = arena_take(vk, len);
    memcpy(vk->arena + *off, p, len);
    *in_arena = 1;
    return 1;
}

static void base_args(ogpu_u32 *args, int op, ogpu_u32 dst, const struct ogpu_surface *t) {
    memset(args, 0, sizeof(ogpu_u32) * A_COUNT);
    args[A_OP] = (ogpu_u32)op;
    args[A_DST] = dst;
    args[A_DBPR] = (ogpu_u32)t->bpr;
    args[A_DFMT] = (ogpu_u32)t->format;
    args[A_TABLE] = 0xFFFFFFFFUL;
}

static void set_rect(ogpu_u32 *args, int x, int y, int w, int h, int ox, int oy) {
    args[A_X] = (ogpu_u32)x; args[A_Y] = (ogpu_u32)y; args[A_W] = (ogpu_u32)w; args[A_H] = (ogpu_u32)h;
    args[A_OX] = (ogpu_u32)ox; args[A_OY] = (ogpu_u32)oy;
}

/* Byte ranges [a0, a1) and [b0, b1) overlap. */
static int overlap(unsigned long a0, unsigned long a1, unsigned long b0, unsigned long b1) {
    return a0 < b1 && b0 < a1;
}

/* Each returns 1 when the command is done (drawn on the GPU, or nothing to
 * draw), 0 to leave it to the C core. They check exactly what the core
 * checks, in the same order, and leave anything that would fail to it. */

static int gpu_fill(struct ogpu_vk *vk, const ogpu_u8 *a, int invert) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), v = rd32(a + 8), dst, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    if (!t) return 0;
    if (invert) v &= t->format == OGPU_FMT_ARGB32 ? 0xFFFFFFFFUL : flip_mask(t->format);
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    base_args(args, invert ? OGPU_OP_INVERT : OGPU_OP_FILL, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_FG] = v;
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

static int gpu_copy(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), dxy = rd32(a + 8), wh = rd32(a + 12), dst, args[A_COUNT];
    int sx = hi_s(sxy), sy = lo_s(sxy), x = hi_s(dxy), y = lo_s(dxy), w = hi_u(wh), h = lo_u(wh), ox, oy, bpp, r;
    const struct ogpu_surface *t = gpu_target(vk, &dst), *s;
    long so;
    if (!t || slot >= OGPU_MAX_SLOTS || !vk->core.slot[slot].pixels) return 0;
    s = &vk->core.slot[slot];
    if (s->format != t->format) return 0;
    if (sx < 0) { x -= sx; w += sx; sx = 0; }
    if (sy < 0) { y -= sy; h += sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return 1;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    sx += ox; sy += oy;
    bpp = bytes_per_pixel(t->format);
    base_args(args, OGPU_OP_COPY, dst, t);
    set_rect(args, x, y, w, h, 0, 0);
    so = vram_off(vk, s->pixels, (unsigned long)(s->bpr * (s->h - 1) + (long)s->w * bpp));
    if (so >= 0) {
        unsigned long s0 = (unsigned long)so + (unsigned long)(sy * s->bpr + (long)sx * bpp);
        unsigned long s1 = (unsigned long)so + (unsigned long)((sy + h - 1) * s->bpr + (long)(sx + w) * bpp);
        unsigned long d0 = dst + (unsigned long)(y * t->bpr + (long)x * bpp);
        unsigned long d1 = dst + (unsigned long)((y + h - 1) * t->bpr + (long)(x + w) * bpp);
        if (!overlap(s0, s1, d0, d1)) {
            args[A_SRC] = (ogpu_u32)so; args[A_SBPR] = (ogpu_u32)s->bpr;
            args[A_SX] = (ogpu_u32)sx; args[A_SY] = (ogpu_u32)sy;
        } else if (s != t) {
            /* Two slots over the same bytes: the core copies them in its own
             * order, which only the core reproduces. */
            return 0;
        } else {
            /* Overlapping: the source goes to the arena first, by the GPU (so
             * it sees what earlier commands drew), then comes back. */
            ogpu_u32 stage[A_COUNT], at;
            unsigned long bytes = (unsigned long)w * bpp * (unsigned long)h;
            if (!arena_room(vk, bytes)) return 0;
            at = arena_take(vk, bytes);
            base_args(stage, OGPU_OP_COPY, at, t);
            stage[A_DBPR] = (ogpu_u32)((long)w * bpp);
            stage[A_SBUF] = SBUF_DST_ARENA;
            set_rect(stage, 0, 0, w, h, 0, 0);
            stage[A_SRC] = (ogpu_u32)so; stage[A_SBPR] = (ogpu_u32)s->bpr;
            stage[A_SX] = (ogpu_u32)sx; stage[A_SY] = (ogpu_u32)sy;
            dispatch_rect(vk, stage, w, h, bpp);
            vk->stats.gpu--;
            args[A_SRC] = at; args[A_SBUF] = SBUF_SRC_ARENA; args[A_SBPR] = (ogpu_u32)((long)w * bpp);
        }
    } else {
        /* A source outside video RAM: its rectangle, packed, into the arena. */
        unsigned long rowb = (unsigned long)w * bpp;
        ogpu_u32 at;
        if (!arena_room(vk, rowb * (unsigned long)h)) return 0;
        at = arena_take(vk, rowb * (unsigned long)h);
        for (r = 0; r < h; r++)
            memcpy(vk->arena + at + (unsigned long)r * rowb, s->pixels + (long)(sy + r) * s->bpr + (long)sx * bpp, rowb);
        args[A_SRC] = at; args[A_SBUF] = SBUF_SRC_ARENA; args[A_SBPR] = (ogpu_u32)rowb;
    }
    dispatch_rect(vk, args, w, h, bpp);
    return 1;
}

/* Arena bytes a source at an Amiga address needs: none when it is in video RAM. */
static unsigned long upload_size(struct ogpu_vk *vk, ogpu_u32 address, unsigned long len) {
    const ogpu_u8 *p = core_map(vk, address, (ogpu_u32)len);
    return p && vram_off(vk, p, len) >= 0 ? 0 : len;
}

/* A source at an Amiga address: mapped as the core maps it, then placed. */
static int map_place(struct ogpu_vk *vk, ogpu_u32 address, unsigned long len, ogpu_u32 *off, ogpu_u32 *in_arena) {
    const ogpu_u8 *p = core_map(vk, address, (ogpu_u32)len);
    if (!p) return 0;
    return place(vk, p, len, off, in_arena);
}

/* A source in video RAM that shares bytes with the rectangle about to be
 * drawn: the core reads it as it draws, so only the core gives its result. */
static int reads_what_it_draws(struct ogpu_vk *vk, ogpu_u32 address, unsigned long len, ogpu_u32 dst,
                               const struct ogpu_surface *t, int x, int y, int w, int h) {
    const ogpu_u8 *p = core_map(vk, address, (ogpu_u32)len);
    long o = p ? vram_off(vk, p, len) : -1, bpp = bytes_per_pixel(t->format);
    unsigned long d0 = dst + (unsigned long)(y * t->bpr + x * bpp);
    unsigned long d1 = dst + (unsigned long)((y + h - 1) * t->bpr + (x + w) * bpp);
    return o >= 0 && overlap((unsigned long)o, (unsigned long)o + len, d0, d1);
}

static int gpu_template(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), first = rd32(a + 8), xy = rd32(a + 12), wh = rd32(a + 16);
    ogpu_u32 dst, src, in_arena, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    unsigned long len;
    if (!t) return 0;
    if (!w || !h) return 1;
    if (first > 0xFFFFUL || bpr > 0xFFFFUL) return 0;
    len = bpr * (unsigned long)(h - 1) + (first + (unsigned long)w + 7) / 8;
    if (!core_map(vk, address, (ogpu_u32)len) || !arena_room(vk, upload_size(vk, address, len))) return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    if (reads_what_it_draws(vk, address, len, dst, t, x, y, w, h)) return 0;
    map_place(vk, address, len, &src, &in_arena);
    base_args(args, OGPU_OP_TEMPLATE, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SRC] = src; args[A_SBUF] = in_arena ? SBUF_SRC_ARENA : 0; args[A_SBPR] = bpr;
    args[A_FIRST] = first; args[A_FG] = rd32(a + 20); args[A_BG] = rd32(a + 24); args[A_MODE] = rd32(a + 28);
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

static int gpu_pattern(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), rows = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12);
    ogpu_u32 dst, src, in_arena, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    if (!t) return 0;
    if (!rows || rows > 256 || (rows & (rows - 1))) return 0;
    if (!core_map(vk, address, rows * 2) || !arena_room(vk, rows * 2)) return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    if (reads_what_it_draws(vk, address, rows * 2, dst, t, x, y, w, h)) return 0;
    map_place(vk, address, rows * 2, &src, &in_arena);
    base_args(args, OGPU_OP_PATTERN, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SRC] = src; args[A_SBUF] = in_arena ? SBUF_SRC_ARENA : 0;
    args[A_FIRST] = rows; args[A_FG] = rd32(a + 16); args[A_BG] = rd32(a + 20); args[A_MODE] = rd32(a + 24);
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

static int gpu_line(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 p0 = rd32(a), p1 = rd32(a + 4), dst, args[A_COUNT];
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    const struct ogpu_core *c = &vk->core;
    int x0 = 0, y0 = 0, x1, y1;
    if (!t) return 0;
    x1 = t->w; y1 = t->h;
    if (c->cx1 > c->cx0) {
        if (c->cx0 > x0) x0 = c->cx0;
        if (c->cy0 > y0) y0 = c->cy0;
        if (c->cx1 < x1) x1 = c->cx1;
        if (c->cy1 < y1) y1 = c->cy1;
    }
    base_args(args, OGPU_OP_LINE, dst, t);
    args[A_X] = (ogpu_u32)x0; args[A_Y] = (ogpu_u32)y0; args[A_W] = (ogpu_u32)x1; args[A_H] = (ogpu_u32)y1;
    args[A_X0] = (ogpu_u32)hi_s(p0); args[A_Y0] = (ogpu_u32)lo_s(p0);
    args[A_X1] = (ogpu_u32)hi_s(p1); args[A_Y1] = (ogpu_u32)lo_s(p1);
    args[A_FG] = rd32(a + 8); args[A_MODE] = rd32(a + 12);
    dispatch(vk, args, 1, 1);
    vk->stats.gpu++;
    return 1;
}

static int gpu_pixels(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), format = rd32(a + 8), table = rd32(a + 12);
    ogpu_u32 xy = rd32(a + 16), wh = rd32(a + 20), dst, src, in_arena, toff = 0, tin = 0, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy;
    int sbpp = bytes_per_pixel((int)format), use_table = 0;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    unsigned long len, need;
    if (!t || !sbpp) return 0;
    if (!w || !h) return 1;
    if (t->format == OGPU_FMT_CLUT8) {
        if (format != OGPU_FMT_CLUT8 && !(format == OGPU_FMT_INDEX8 && !table)) return 0;
    } else {
        if (format == OGPU_FMT_CLUT8) return 0;
        if ((format == OGPU_FMT_A8) != (t->format == OGPU_FMT_A8)) return 0;
        if (format == OGPU_FMT_INDEX8) {
            if (!table || !core_map(vk, table, 1024)) return 0;
            use_table = 1;
        }
    }
    if (bpr < (ogpu_u32)w * (ogpu_u32)sbpp || bpr > 0x01000000UL) return 0;
    len = bpr * (unsigned long)(h - 1) + (unsigned long)w * (unsigned long)sbpp;
    if (len > 0xFFFFFFFFUL) return 0;       /* the core refuses what passes 32 bits */
    if (!core_map(vk, address, (ogpu_u32)len)) return 0;
    need = upload_size(vk, address, len) + (use_table ? upload_size(vk, table, 1024) : 0);
    if (!arena_room(vk, need)) return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    if (reads_what_it_draws(vk, address, len, dst, t, x, y, w, h)
        || (use_table && reads_what_it_draws(vk, table, 1024, dst, t, x, y, w, h))) return 0;
    if (use_table) map_place(vk, table, 1024, &toff, &tin);
    map_place(vk, address, len, &src, &in_arena);
    base_args(args, OGPU_OP_PIXELS, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SRC] = src; args[A_SBPR] = bpr; args[A_SFMT] = format;
    args[A_SBUF] = (in_arena ? SBUF_SRC_ARENA : 0) | (tin ? SBUF_TABLE_ARENA : 0);
    if (use_table) args[A_TABLE] = toff;
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

static int gpu_composite(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), swh = rd32(a + 8), dxy = rd32(a + 12), dwh = rd32(a + 16);
    ogpu_u32 flags = rd32(a + 24), dst, args[A_COUNT];
    int sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh);
    int x = hi_s(dxy), y = lo_s(dxy), w = hi_u(dwh), h = lo_u(dwh), ox, oy, sbpp, dbpp, r, mx = 0, my = 0, w0, h0;
    const struct ogpu_surface *t = gpu_target(vk, &dst), *s, *m = 0;
    long so;
    if (!t || slot >= OGPU_MAX_SLOTS || !vk->core.slot[slot].pixels) return 0;
    s = &vk->core.slot[slot];
    if (t->format == OGPU_FMT_CLUT8 || s->format == OGPU_FMT_CLUT8) return 0;
    if (!bytes_per_pixel(s->format)) return 0;      /* a format the shader doesn't read: the core's */
    if ((flags & OGPU_COMP_ADD) && (flags & OGPU_COMP_IN)) return 0;
    /* v1.2's blend modes (SDL's copies), as the core's blend12 */
    if ((flags & OGPU_COMP_BLENDMODE) && OGPU_COMP_MODE_OF(flags) > OGPU_BLEND_MUL) return 0;
    if (flags & ~(ogpu_u32)(OGPU_COMP_SRCALPHA | OGPU_COMP_BILINEAR | OGPU_COMP_MASK | OGPU_COMP_ADD | OGPU_COMP_IN
                            | (flags & OGPU_COMP_BLENDMODE ? OGPU_COMP_BLENDMODE | OGPU_COMP_MODE(7) : 0)))
        return 0;
    if (flags & OGPU_COMP_MASK) {
        ogpu_u32 mslot = rd32(a + 28), mxy = rd32(a + 32);
        if (mslot >= OGPU_MAX_SLOTS || !vk->core.slot[mslot].pixels) return 0;
        m = &vk->core.slot[mslot];
        mx = hi_s(mxy); my = lo_s(mxy);
        if (m->format != OGPU_FMT_A8 || mx < 0 || my < 0 || mx + w > m->w || my + h > m->h) return 0;
    }
    if (!sw || !sh || !w || !h) return 1;
    if (sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return 0;
    base_args(args, OGPU_OP_COMPOSITE, dst, t);
    args[A_STEPX] = ((ogpu_u32)sw << 16) / (ogpu_u32)w;
    args[A_STEPY] = ((ogpu_u32)sh << 16) / (ogpu_u32)h;
    sbpp = bytes_per_pixel(s->format); dbpp = bytes_per_pixel(t->format);
    w0 = w; h0 = h;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    {
        unsigned long d0 = dst + (unsigned long)(y * t->bpr + (long)x * dbpp);
        unsigned long d1 = dst + (unsigned long)((y + h - 1) * t->bpr + (long)(x + w) * dbpp);
        long mo = -1;
        /* The core blends in place, reading as it writes; when the source or
         * the mask shares bytes with what is drawn, only the core gives its result. */
        so = vram_off(vk, s->pixels, (unsigned long)(s->bpr * (s->h - 1) + (long)s->w * sbpp));
        if (so >= 0) {
            unsigned long s0 = (unsigned long)so + (unsigned long)(sy * s->bpr + (long)sx * sbpp);
            unsigned long s1 = (unsigned long)so + (unsigned long)((sy + sh - 1) * s->bpr + (long)(sx + sw) * sbpp);
            if (overlap(s0, s1, d0, d1)) return 0;
        }
        if (m) {
            mo = vram_off(vk, m->pixels, (unsigned long)(m->bpr * (m->h - 1) + (long)m->w));
            if (mo >= 0) {
                unsigned long m0 = (unsigned long)mo + (unsigned long)(my * m->bpr + mx);
                unsigned long m1 = (unsigned long)mo + (unsigned long)((my + h0 - 1) * m->bpr + mx + w0);
                if (overlap(m0, m1, d0, d1)) return 0;
            } else if (!arena_room(vk, (((unsigned long)w0 * (unsigned long)h0 + 3) & ~3UL)   /* as arena_take rounds it */
                                       + (so >= 0 ? 0 : (unsigned long)sw * sbpp * (unsigned long)sh)))
                return 0;
            if (mo >= 0) {
                args[A_MSRC] = (ogpu_u32)mo; args[A_MBPR] = (ogpu_u32)m->bpr;
                args[A_MX] = (ogpu_u32)mx; args[A_MY] = (ogpu_u32)my;
            }
        }
        if (so < 0 && !arena_room(vk, (unsigned long)sw * sbpp * (unsigned long)sh)) return 0;
        if (m && mo < 0) {
            /* The mask's part under the rectangle, packed, into the arena. */
            ogpu_u32 at = arena_take(vk, (unsigned long)w0 * (unsigned long)h0);
            for (r = 0; r < h0; r++)
                memcpy(vk->arena + at + (unsigned long)r * (unsigned long)w0, m->pixels + (long)(my + r) * m->bpr + mx, (size_t)w0);
            args[A_MSRC] = at; args[A_MBPR] = (ogpu_u32)w0; args[A_MX] = 0; args[A_MY] = 0;
            args[A_SBUF] |= SBUF_MASK_ARENA;
        }
    }
    if (so >= 0) {
        args[A_SRC] = (ogpu_u32)so; args[A_SBPR] = (ogpu_u32)s->bpr;
        args[A_SX] = (ogpu_u32)sx; args[A_SY] = (ogpu_u32)sy;
    } else {
        unsigned long rowb = (unsigned long)sw * sbpp;
        ogpu_u32 at = arena_take(vk, rowb * (unsigned long)sh);
        for (r = 0; r < sh; r++)
            memcpy(vk->arena + at + (unsigned long)r * rowb, s->pixels + (long)(sy + r) * s->bpr + (long)sx * sbpp, rowb);
        args[A_SRC] = at; args[A_SBUF] |= SBUF_SRC_ARENA; args[A_SBPR] = (ogpu_u32)rowb;
    }
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SFMT] = (ogpu_u32)s->format;
    args[A_SW] = (ogpu_u32)sw; args[A_SH] = (ogpu_u32)sh;
    args[A_ALPHA] = rd32(a + 20) & 255; args[A_FLAGS] = flags;
    dispatch_rect(vk, args, w, h, dbpp);
    return 1;
}

/* MASK (v1.1): an A8 mask in memory, placed as TEMPLATE's bits are. */
static int gpu_mask(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12);
    ogpu_u32 dst, src, in_arena, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    unsigned long len;
    if (!t || t->format == OGPU_FMT_CLUT8) return 0;
    if (!w || !h) return 1;
    if (bpr < (ogpu_u32)w || bpr > 0x01000000UL) return 0;
    len = bpr * (unsigned long)(h - 1) + (unsigned long)w;
    if (len > 0xFFFFFFFFUL) return 0;
    if (!core_map(vk, address, (ogpu_u32)len) || !arena_room(vk, upload_size(vk, address, len))) return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    if (reads_what_it_draws(vk, address, len, dst, t, x, y, w, h)) return 0;
    map_place(vk, address, len, &src, &in_arena);
    base_args(args, OGPU_OP_MASK, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SRC] = src; args[A_SBUF] = in_arena ? SBUF_SRC_ARENA : 0; args[A_SBPR] = bpr;
    args[A_FG] = rd32(a + 16);
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

/* ---- v1.2 on the GPU --------------------------------------------------------------- */

/* Formats v1.2's blends draw into on the GPU: the core's colour formats
 * that the shader reads and writes. */
static int blend_format(int format) { return format == OGPU_FMT_RGB565 || format == OGPU_FMT_ARGB32; }

/* FILL_BLEND: an ARGB32 colour over a rectangle by a blend mode. */
static int gpu_fill_blend(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), dst, args[A_COUNT];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, mode = (int)(rd32(a + 12) & OGPU_BLEND_MASK);
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    if (!t || !blend_format(t->format) || mode > OGPU_BLEND_MUL) return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    base_args(args, OGPU_OP_FILL_BLEND, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_FG] = rd32(a + 8);
    args[A_MODE] = (ogpu_u32)mode;
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

/* The drawable rectangle: the target, less the clip (as the core's bounds). */
static void vk_bounds(const struct ogpu_core *c, int *x0, int *y0, int *x1, int *y1) {
    const struct ogpu_surface *t = &c->slot[c->target];
    *x0 = 0; *y0 = 0; *x1 = t->w; *y1 = t->h;
    if (c->cx1 > c->cx0) {
        if (c->cx0 > *x0) *x0 = c->cx0;
        if (c->cy0 > *y0) *y0 = c->cy0;
        if (c->cx1 < *x1) *x1 = c->cx1;
        if (c->cy1 < *y1) *y1 = c->cy1;
    }
}

typedef long long vk_s64;
static long word_s32(ogpu_u32 v) { return v > 0x7FFFFFFFUL ? (long)((vk_s64)v - 0x100000000LL) : (long)v; }
static int fits32(vk_s64 v) { return v > -0x7FFFFFFFLL && v < 0x7FFFFFFFLL; }

/* COMPOSITE_AFFINE: the core's inverse matrix, bounds and each row's first
 * source position, worked out here in 64 bits exactly as the core does; the
 * rows' starts go to the arena, and the shader steps along each row. Left
 * to the core whenever a step could leave 32 bits, the source shares bytes
 * with what is drawn, or anything would be an error. */
static int gpu_affine(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), swh = rd32(a + 8), colour = rd32(a + 36), flags = rd32(a + 40), dst, args[A_COUNT];
    long m00 = word_s32(rd32(a + 12)), m01 = word_s32(rd32(a + 16)), m02 = word_s32(rd32(a + 20));
    long m10 = word_s32(rd32(a + 24)), m11 = word_s32(rd32(a + 28)), m12 = word_s32(rd32(a + 32));
    const struct ogpu_surface *t = gpu_target(vk, &dst), *s;
    int sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh), mode = (int)(flags & OGPU_BLEND_MASK);
    int bx0, by0, bx1, by1, y, k, sbpp, dbpp, rows, cols;
    vk_s64 det, i00, i01, i10, i11;
    long minx = 0x7FFFFFFFL, miny = 0x7FFFFFFFL, maxx = -0x7FFFFFFFL, maxy = -0x7FFFFFFFL, so;
    unsigned long need;
    ogpu_u32 table, src, in_arena = 0;
    if (!t || slot >= OGPU_MAX_SLOTS || !vk->core.slot[slot].pixels) return 0;
    s = &vk->core.slot[slot];
    if (!blend_format(t->format) || !blend_format(s->format) || mode > OGPU_BLEND_MUL) return 0;
    if (!sw || !sh) return 1;
    if (sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return 0;
    if (sw >= 0x8000 || sh >= 0x8000) return 0;                      /* the shader's 16.16 sizes are 32-bit */
    if (m00 >= 0x800000L || m00 <= -0x800000L || m01 >= 0x800000L || m01 <= -0x800000L
        || m10 >= 0x800000L || m10 <= -0x800000L || m11 >= 0x800000L || m11 <= -0x800000L) return 0;
    det = (vk_s64)m00 * m11 - (vk_s64)m01 * m10;
    if (det == 0) return 1;
    i00 = (vk_s64)m11 * 4294967296LL / det; i01 = -((vk_s64)m01 * 4294967296LL) / det;
    i10 = -((vk_s64)m10 * 4294967296LL) / det; i11 = (vk_s64)m00 * 4294967296LL / det;
    if (i00 >= 0x40000000LL || i00 <= -0x40000000LL || i01 >= 0x40000000LL || i01 <= -0x40000000LL
        || i10 >= 0x40000000LL || i10 <= -0x40000000LL || i11 >= 0x40000000LL || i11 <= -0x40000000LL) return 0;
    for (k = 0; k < 4; k++) {
        long u = k & 1 ? (long)sw << 16 : 0, v = k & 2 ? (long)sh << 16 : 0;
        long tx = (long)(((vk_s64)m00 * u + (vk_s64)m01 * v) / 65536) + m02;
        long ty = (long)(((vk_s64)m10 * u + (vk_s64)m11 * v) / 65536) + m12;
        if (tx < minx) minx = tx;
        if (tx > maxx) maxx = tx;
        if (ty < miny) miny = ty;
        if (ty > maxy) maxy = ty;
    }
    vk_bounds(&vk->core, &bx0, &by0, &bx1, &by1);
    if ((minx >> 16) > bx0) bx0 = (int)(minx >> 16);
    if ((miny >> 16) > by0) by0 = (int)(miny >> 16);
    if ((maxx >> 16) + 1 < bx1) bx1 = (int)(maxx >> 16) + 1;
    if ((maxy >> 16) + 1 < by1) by1 = (int)(maxy >> 16) + 1;
    if (bx1 <= bx0 || by1 <= by0) return 1;
    rows = by1 - by0; cols = bx1 - bx0;
    /* every step of every row in 32 bits: the ends of each row, and the steps */
    if (!fits32(i00 * (cols - 1)) || !fits32(i10 * (cols - 1))) return 0;
    sbpp = bytes_per_pixel(s->format); dbpp = bytes_per_pixel(t->format);
    so = vram_off(vk, s->pixels, (unsigned long)(s->bpr * (s->h - 1) + (long)s->w * sbpp));
    if (so >= 0) {
        unsigned long s0 = (unsigned long)so + (unsigned long)(sy * s->bpr + (long)sx * sbpp);
        unsigned long s1 = (unsigned long)so + (unsigned long)((sy + sh - 1) * s->bpr + (long)(sx + sw) * sbpp);
        unsigned long d0 = dst + (unsigned long)(by0 * t->bpr + (long)bx0 * dbpp);
        unsigned long d1 = dst + (unsigned long)((by1 - 1) * t->bpr + (long)bx1 * dbpp);
        if (overlap(s0, s1, d0, d1)) return 0;      /* the core reads it as it draws */
    }
    need = (unsigned long)rows * 8 + (so >= 0 ? 0 : (unsigned long)sw * sbpp * (unsigned long)sh);
    if (!arena_room(vk, need)) return 0;
    /* two words a row, u and v, in the host's order (the GPU's: both little-endian) */
    table = arena_take(vk, (unsigned long)rows * 8);
    for (y = by0; y < by1; y++) {
        vk_s64 dx = ((vk_s64)bx0 << 16) + 0x8000 - m02, dy = ((vk_s64)y << 16) + 0x8000 - m12;
        vk_s64 u = (i00 * dx + i01 * dy) >> 16, v = (i10 * dx + i11 * dy) >> 16;
        ogpu_u8 *e = vk->arena + table + (unsigned long)(y - by0) * 8;
        uint32_t uv[2];
        if (!fits32(u) || !fits32(v) || !fits32(u + i00 * (cols - 1)) || !fits32(v + i10 * (cols - 1))) {
            vk->arena_used = table;                 /* give the table back: the core draws it */
            return 0;
        }
        uv[0] = (uint32_t)(long)u; uv[1] = (uint32_t)(long)v;
        memcpy(e, uv, 8);
    }
    if (so >= 0) {
        src = (ogpu_u32)so;
    } else {
        unsigned long rowb = (unsigned long)sw * sbpp;
        int r;
        src = arena_take(vk, rowb * (unsigned long)sh);
        for (r = 0; r < sh; r++)
            memcpy(vk->arena + src + (unsigned long)r * rowb, s->pixels + (long)(sy + r) * s->bpr + (long)sx * sbpp, rowb);
        in_arena = 1;
    }
    base_args(args, OGPU_OP_COMPOSITE_AFFINE, dst, t);
    set_rect(args, bx0, by0, cols, rows, 0, 0);
    args[A_SRC] = src; args[A_SBUF] = in_arena ? SBUF_SRC_ARENA : 0; args[A_SFMT] = (ogpu_u32)s->format;
    args[A_SBPR] = in_arena ? (ogpu_u32)((long)sw * sbpp) : (ogpu_u32)s->bpr;
    args[A_SX] = in_arena ? 0 : (ogpu_u32)sx; args[A_SY] = in_arena ? 0 : (ogpu_u32)sy;
    args[A_SW] = (ogpu_u32)sw; args[A_SH] = (ogpu_u32)sh;
    args[A_STEPX] = (ogpu_u32)(long)i00; args[A_STEPY] = (ogpu_u32)(long)i10;
    args[A_FIRST] = table; args[A_FG] = colour; args[A_MODE] = (ogpu_u32)mode;
    args[A_FLAGS] = flags & (OGPU_AFF_SRCALPHA | OGPU_AFF_BILINEAR);
    dispatch_rect(vk, args, cols, rows, dbpp);
    return 1;
}

/* The bytes h rows of `last` bytes take at bpr apart, or 0 past 32 bits (as the core's span_len). */
static int vk_span(ogpu_u32 bpr, int h, ogpu_u32 last, unsigned long *len) {
    ogpu_u32 rows = (ogpu_u32)(h - 1);
    if (rows && bpr > (0xFFFFFFFFUL - last) / rows) return 0;
    *len = (unsigned long)bpr * rows + last;
    return 1;
}

/* YUV: the core's checks, in its order; the planes read in place in video
 * RAM or copied into the arena; the conversion per pixel on the GPU. */
static int gpu_yuv(struct ogpu_vk *vk, const ogpu_u8 *a) {
    ogpu_u32 ya = rd32(a), ybpr = rd32(a + 4), ua = rd32(a + 8), ubpr = rd32(a + 12), va = rd32(a + 16), vbpr = rd32(a + 20);
    ogpu_u32 wh = rd32(a + 24), xy = rd32(a + 28), fmt = rd32(a + 32), dst, args[A_COUNT];
    ogpu_u32 yo = 0, uo = 0, vo = 0, yin = 0, uin = 0, vin = 0;
    const struct ogpu_surface *t = gpu_target(vk, &dst);
    int layout = (int)(fmt & 255), space = (int)((fmt >> 8) & 255);
    int w = hi_u(wh), h = lo_u(wh), x = hi_s(xy), y = lo_s(xy), ox, oy;
    int packed = layout >= OGPU_YUV_YUY2 && layout <= OGPU_YUV_YVYU, semi = layout == OGPU_YUV_NV12 || layout == OGPU_YUV_NV21;
    unsigned long ylen = 0, ulen = 0, vlen = 0;
    if (!t || !blend_format(t->format) || layout < OGPU_YUV_I420 || layout > OGPU_YUV_YVYU || space > OGPU_YUV_JPEG) return 0;
    if (!w || !h) return 1;
    if ((w | h) & 1) return 0;
    if (packed) {
        if (ybpr < (ogpu_u32)w * 2 || ybpr > 0x01000000UL || !vk_span(ybpr, h, (ogpu_u32)w * 2, &ylen)) return 0;
    } else {
        if (ybpr < (ogpu_u32)w || ybpr > 0x01000000UL || !vk_span(ybpr, h, (ogpu_u32)w, &ylen)) return 0;
        if (semi) {
            if (ubpr < (ogpu_u32)w || ubpr > 0x01000000UL || !vk_span(ubpr, h / 2, (ogpu_u32)w, &ulen)) return 0;
        } else {
            if (ubpr < (ogpu_u32)w / 2 || ubpr > 0x01000000UL || !vk_span(ubpr, h / 2, (ogpu_u32)w / 2, &ulen)) return 0;
            if (vbpr < (ogpu_u32)w / 2 || vbpr > 0x01000000UL || !vk_span(vbpr, h / 2, (ogpu_u32)w / 2, &vlen)) return 0;
        }
    }
    if (!core_map(vk, ya, (ogpu_u32)ylen) || (ulen && !core_map(vk, ua, (ogpu_u32)ulen)) || (vlen && !core_map(vk, va, (ogpu_u32)vlen)))
        return 0;
    if (!arena_room(vk, upload_size(vk, ya, ylen) + (ulen ? upload_size(vk, ua, ulen) : 0) + (vlen ? upload_size(vk, va, vlen) : 0)))
        return 0;
    if (!clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy)) return 1;
    if (reads_what_it_draws(vk, ya, ylen, dst, t, x, y, w, h) || (ulen && reads_what_it_draws(vk, ua, ulen, dst, t, x, y, w, h))
        || (vlen && reads_what_it_draws(vk, va, vlen, dst, t, x, y, w, h))) return 0;
    map_place(vk, ya, ylen, &yo, &yin);
    if (ulen) map_place(vk, ua, ulen, &uo, &uin);
    if (vlen) map_place(vk, va, vlen, &vo, &vin);
    base_args(args, OGPU_OP_YUV, dst, t);
    set_rect(args, x, y, w, h, ox, oy);
    args[A_SRC] = yo; args[A_SBPR] = ybpr;
    args[A_TABLE] = uo; args[A_BG] = ubpr;
    args[A_PAD19] = vo; args[A_X0] = vbpr;           /* the shader's A_VSRC and A_VBPR */
    args[A_SBUF] = (yin ? SBUF_SRC_ARENA : 0) | (uin ? SBUF_TABLE_ARENA : 0) | (vin ? SBUF_MASK_ARENA : 0);
    args[A_MODE] = (ogpu_u32)layout | (ogpu_u32)space << 8;
    dispatch_rect(vk, args, w, h, bytes_per_pixel(t->format));
    return 1;
}

/* ---- routing ---------------------------------------------------------------------- */

/* What a command costs, by kind: nanoseconds a pixel (a byte for FILL and
 * COPY, which are bound by memory) on the core and on the GPU, and the
 * microseconds a dispatch adds once others are recorded in the same
 * command buffer. Measured on daletop's AMD RX 460 (RADV, video RAM mapped)
 * and its host by amigachrome's tests/gpu/test_opengpu_2d_bench.sh, 17
 * October 2026; a Pi 5 (V3DV) gets its own figures with GPU-5. */
enum { K_FILL, K_COPY, K_BITS, K_PIXELS, K_COMP, K_BLEND, K_BILINEAR, K_YUV, K_LINE, K_COUNT };
static const struct { double core, gpu, chain; } k_cost[K_COUNT] = {
    { 0.13, 0.19, 5 },      /* FILL, INVERT: a byte (SetRast 1920x1080 32-bit: 1.0 ms core, 1.5 ms GPU) */
    { 0.5, 0.42, 5 },       /* COPY: a byte (a 640x480 32-bit window back on screen: 0.52 ms core, 0.5 ms GPU) */
    { 2, 0.5, 5 },          /* TEMPLATE, PATTERN: a pixel (a 300x200 area pattern: 0.11 ms core) */
    { 3, 1.8, 5 },          /* PIXELS: a pixel, with its upload */
    { 6, 2, 40 },           /* COMPOSITE nearest, MASK, with the source's upload; chained dispatches cost about 40 us */
    { 9, 0.6, 40 },         /* FILL_BLEND, COMPOSITE's blend modes, COMPOSITE_AFFINE nearest (300x200 FILL_BLEND: 0.54 ms core) */
    { 17.5, 1.1, 40 },      /* bilinear COMPOSITE and COMPOSITE_AFFINE (a 640x480 stretch: 5.4 ms core, 0.34 ms GPU) */
    { 6, 1, 5 },            /* YUV (a 640x480 I420 frame: 1.9 ms core, 0.3 ms GPU) */
    { 7, 1400, 5 },         /* LINE, a pixel of its length: one GPU thread walks it, 140 us a line of 100 */
};

static double now_us(void) {
#if defined(__unix__) || defined(__APPLE__)
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
#else
    return (double)clock() * 1e6 / CLOCKS_PER_SEC;
#endif
}

/* The pixels of a rectangle once clipped. */
static double area(const struct ogpu_vk *vk, int x, int y, int w, int h) {
    int ox, oy;
    return clip_rect(&vk->core, &x, &y, &w, &h, &ox, &oy) ? (double)w * h : 0;
}

/* How much work a drawing command is (pixels, or bytes for FILL and COPY),
 * and its kind. */
static double work(const struct ogpu_vk *vk, int op, const ogpu_u8 *a, int *kind) {
    const struct ogpu_surface *t = &vk->core.slot[vk->core.target];
    double bpp = bytes_per_pixel(t->format) ? bytes_per_pixel(t->format) : 4;
    ogpu_u32 f;
    switch (op) {
    case OGPU_OP_FILL: case OGPU_OP_INVERT:
        *kind = K_FILL; return area(vk, hi_s(rd32(a)), lo_s(rd32(a)), hi_u(rd32(a + 4)), lo_u(rd32(a + 4))) * bpp;
    case OGPU_OP_COPY:
        *kind = K_COPY; return area(vk, hi_s(rd32(a + 8)), lo_s(rd32(a + 8)), hi_u(rd32(a + 12)), lo_u(rd32(a + 12))) * bpp;
    case OGPU_OP_TEMPLATE:
        *kind = K_BITS; return area(vk, hi_s(rd32(a + 12)), lo_s(rd32(a + 12)), hi_u(rd32(a + 16)), lo_u(rd32(a + 16)));
    case OGPU_OP_PATTERN:
        *kind = K_BITS; return area(vk, hi_s(rd32(a + 8)), lo_s(rd32(a + 8)), hi_u(rd32(a + 12)), lo_u(rd32(a + 12)));
    case OGPU_OP_MASK:
        *kind = K_COMP; return area(vk, hi_s(rd32(a + 8)), lo_s(rd32(a + 8)), hi_u(rd32(a + 12)), lo_u(rd32(a + 12)));
    case OGPU_OP_PIXELS:
        *kind = K_PIXELS; return area(vk, hi_s(rd32(a + 16)), lo_s(rd32(a + 16)), hi_u(rd32(a + 20)), lo_u(rd32(a + 20)));
    case OGPU_OP_LINE: {
        int dx = hi_s(rd32(a + 4)) - hi_s(rd32(a)), dy = lo_s(rd32(a + 4)) - lo_s(rd32(a));
        *kind = K_LINE;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        return (double)(dx > dy ? dx : dy) + 1;
    }
    case OGPU_OP_COMPOSITE:
        f = rd32(a + 24);
        *kind = f & OGPU_COMP_BILINEAR ? K_BILINEAR : f & OGPU_COMP_BLENDMODE ? K_BLEND : K_COMP;
        return area(vk, hi_s(rd32(a + 12)), lo_s(rd32(a + 12)), hi_u(rd32(a + 16)), lo_u(rd32(a + 16)));
    case OGPU_OP_FILL_BLEND:
        *kind = K_BLEND; return area(vk, hi_s(rd32(a)), lo_s(rd32(a)), hi_u(rd32(a + 4)), lo_u(rd32(a + 4)));
    case OGPU_OP_COMPOSITE_AFFINE: {
        /* the transformed source's bounding box, at most the target */
        double sw = hi_u(rd32(a + 8)), sh = lo_u(rd32(a + 8)), w, h;
        double m00 = (double)word_s32(rd32(a + 12)), m01 = (double)word_s32(rd32(a + 16));
        double m10 = (double)word_s32(rd32(a + 24)), m11 = (double)word_s32(rd32(a + 28));
        w = ((m00 < 0 ? -m00 : m00) * sw + (m01 < 0 ? -m01 : m01) * sh) / 65536;
        h = ((m10 < 0 ? -m10 : m10) * sw + (m11 < 0 ? -m11 : m11) * sh) / 65536;
        if (w > t->w) w = t->w;
        if (h > t->h) h = t->h;
        *kind = rd32(a + 40) & OGPU_AFF_BILINEAR ? K_BILINEAR : K_BLEND;
        return w * h;
    }
    case OGPU_OP_YUV:
        *kind = K_YUV; return area(vk, hi_s(rd32(a + 28)), lo_s(rd32(a + 28)), hi_u(rd32(a + 24)), lo_u(rd32(a + 24)));
    }
    *kind = K_FILL;
    return 0;
}

/* 1 when the command, which the GPU path may take, is to go there: always
 * with OGPU_VK_ROUTE_GPU, never with OGPU_VK_ROUTE_CORE, and by cost
 * otherwise. With nothing recorded, the GPU costs a round trip and the core
 * no wait; with something recorded, the GPU only its dispatch, and the core
 * a wait for what is recorded first. */
static int route_gpu(struct ogpu_vk *vk, int op, const ogpu_u8 *a) {
    double n, core, gpu;
    int kind;
    if (vk->route == OGPU_VK_ROUTE_GPU) return 1;
    if (vk->route == OGPU_VK_ROUTE_CORE) return 0;
    n = work(vk, op, a, &kind);
    core = n * k_cost[kind].core / 1000;
    gpu = n * k_cost[kind].gpu / 1000;
    if (vk->recording || vk->list_n) {
        /* joining the composite list costs no dispatch of its own */
        gpu += vk->list_n && (op == OGPU_OP_FILL || op == OGPU_OP_INVERT || op == OGPU_OP_COMPOSITE || op == OGPU_OP_COMPOSITE_AFFINE
                              || op == OGPU_OP_FILL_BLEND) ? 2 : k_cost[kind].chain;
        core += vk->trip;
    } else gpu += vk->trip;
    return gpu < core;
}

/* This device's round trip: a 1-pixel fill in the arena, submitted and
 * waited for; the median of five, after one to warm up. */
static void measure_trip(struct ogpu_vk *vk) {
    ogpu_u32 args[A_COUNT];
    double t[5], x;
    int i, j;
    struct ogpu_vk_stats keep = vk->stats;
    memset(args, 0, sizeof args);
    args[A_OP] = OGPU_OP_FILL; args[A_DST] = 0; args[A_DBPR] = 4; args[A_DFMT] = OGPU_FMT_ARGB32;
    args[A_W] = 1; args[A_H] = 1; args[A_SBUF] = SBUF_DST_ARENA; args[A_TABLE] = 0xFFFFFFFFUL;
    for (i = -1; i < 5; i++) {
        double t0 = now_us();
        dispatch(vk, args, 1, 1);
        flush(vk);
        if (i >= 0) t[i] = now_us() - t0;
    }
    for (i = 1; i < 5; i++)
        for (j = i; j > 0 && t[j - 1] > t[j]; j--) { x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    vk->trip = t[2];
    vk->stats = keep;
    vk->stats.trip_us = (unsigned long)(vk->trip + 0.5);
}

static int gpu_draw(struct ogpu_vk *vk, int op, const ogpu_u8 *a) {
    switch (op) {
    case OGPU_OP_FILL: return gpu_fill(vk, a, 0);
    case OGPU_OP_INVERT: return gpu_fill(vk, a, 1);
    case OGPU_OP_COPY: return gpu_copy(vk, a);
    case OGPU_OP_TEMPLATE: return gpu_template(vk, a);
    case OGPU_OP_PATTERN: return gpu_pattern(vk, a);
    case OGPU_OP_LINE: return gpu_line(vk, a);
    case OGPU_OP_PIXELS: return gpu_pixels(vk, a);
    case OGPU_OP_MASK: return gpu_mask(vk, a);
    case OGPU_OP_COMPOSITE: return gpu_composite(vk, a);
    case OGPU_OP_FILL_BLEND: return gpu_fill_blend(vk, a);
    case OGPU_OP_COMPOSITE_AFFINE: return gpu_affine(vk, a);
    case OGPU_OP_YUV: return gpu_yuv(vk, a);
    }
    return 0;
}

/* ---- the run ---------------------------------------------------------------------- */

long ogpu_vk_run(struct ogpu_vk *vk, const ogpu_u8 *stream, long words) {
    long at = 0, done = 0;
    vk->last_error = OGPU_OK;
    vk->error_word = 0;
    vk->lost_now = 0;
    while (at < words) {
        ogpu_u32 hdr = rd32(stream + at * 4);
        int op = (int)OGPU_HDR_OP(hdr), len = (int)OGPU_HDR_WORDS(hdr), need = args_for(op);
        if (len < 1 || at + len > words
            || (need >= 0 && (len - 1 < need
                              || (op == OGPU_OP_COMPOSITE && (rd32(stream + at * 4 + 28) & OGPU_COMP_MASK) && len - 1 < 9)))) {
            if (vk->last_error == OGPU_OK) { vk->last_error = OGPU_ERR_BADLEN; vk->error_word = at; }
            break;
        }
        int drawable = !vk->failed && gpu_op(op) && vk->core.target >= 0 && vk->core.slot[vk->core.target].pixels;
        ogpu_u32 dst;
        if (drawable && gpu_target(vk, &dst) && !route_gpu(vk, op, stream + at * 4 + 4)) {
            drawable = 0;           /* cheaper on the core */
            vk->stats.routed++;
        }
        if (drawable && gpu_draw(vk, op, stream + at * 4 + 4)) {
            done++;
        } else {
            /* State commands change no pixels; anything else waits for the GPU. */
            if (op != OGPU_OP_SURFACE && op != OGPU_OP_TARGET && op != OGPU_OP_CLIP && op != OGPU_OP_NOP)
                flush(vk);
            done += ogpu_core_run(&vk->core, stream + at * 4, len);
            if (op >= OGPU_OP_FILL && op < OGPU_OP_FENCE) vk->stats.cpu++;
            if (vk->core.last_error != OGPU_OK && vk->last_error == OGPU_OK) {
                vk->last_error = vk->core.last_error;
                vk->error_word = at;
            }
        }
        at += len;
    }
    flush(vk);
    if (vk->lost_now && vk->last_error == OGPU_OK) { vk->last_error = OGPU_ERR_DEVICE; vk->error_word = 0; }
    return done;
}

/* The core with no slots, target or clip (its hooks stay) and, built with
 * the 3D range, its 3D state as new: as the CPU path's core after a reset. */
static void core_afresh(struct ogpu_vk *vk) {
    ogpu_core_init(&vk->core);
#ifdef OGPU_WITH_3D
    ogpu3d_init(&vk->d3, &vk->core);
    vk->core.d3 = &vk->d3;
#endif
}

void ogpu_vk_reset(struct ogpu_vk *vk) {
    flush(vk);
    core_afresh(vk);
}

/* What a run carries out, wherever it is drawn: the GPU what it takes, the
 * core here the rest, so the answer is the core's; and the GPU APIs'
 * range (OGPU_OP_VIRGL) goes to the caller's ext, when there is one. */
int ogpu_vk_supports(const struct ogpu_vk *vk, int op, int format) {
    if (op >= OGPU_OP_EXT_FIRST && op <= OGPU_OP_EXT_LAST) return vk->cfg.ext ? OGPU_FULL : OGPU_NONE;
#ifndef OGPU_WITH_3D
    if (op >= 0x0030 && op <= 0x003F) return OGPU_NONE;   /* no 3D state on this core */
#endif
    return ogpu_core_supports(op, format);
}

int ogpu_vk_last_error(const struct ogpu_vk *vk) { return vk->last_error; }
long ogpu_vk_error_word(const struct ogpu_vk *vk) { return vk->error_word; }
int ogpu_vk_lost(const struct ogpu_vk *vk) { return vk->failed; }
ogpu_u8 *ogpu_vk_vram(struct ogpu_vk *vk) { return vk->vram; }
const char *ogpu_vk_vram_mode(struct ogpu_vk *vk) { return vk->vram_mode; }
const char *ogpu_vk_device(struct ogpu_vk *vk) { return vk->name; }
void ogpu_vk_stats(const struct ogpu_vk *vk, struct ogpu_vk_stats *s) { *s = vk->stats; s->route = vk->route; }

/* ---- setting up ------------------------------------------------------------------- */

static int fail(char *err, int errlen, const char *what, VkResult r) {
    if (err && errlen > 0) snprintf(err, (size_t)errlen, "%s (VkResult %d)", what, (int)r);
    return 0;
}

/* A memory type: host-visible and coherent. Video RAM (cpu_reads) is read
 * by the CPU all the time (the 68k, the C core, the picture), so it wants
 * memory the CPU caches: a card's own memory behind the PCI bar is uncached
 * and very slow to read (2 minutes against 1 s for the check on daletop's
 * RX 460). The upload arena, which the CPU only writes, prefers the
 * device's memory. A Pi's GPU shares its memory either way. */
static int memory_type(VkPhysicalDevice phys, uint32_t bits, int cpu_reads) {
    VkPhysicalDeviceMemoryProperties mp;
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags cached = want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    uint32_t i;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    if (cpu_reads)
        for (i = 0; i < mp.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & cached) == cached) return (int)i;
    for (i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & (want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                                      == (want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return (int)i;
    for (i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

static int make_buffer(struct ogpu_vk *vk, VkDeviceSize size, int cpu_reads, VkBuffer *buf, VkDeviceMemory *mem, ogpu_u8 **p,
                       char *err, int errlen) {
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    VkResult r;
    void *m;
    int type;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if ((r = vkCreateBuffer(vk->dev, &bi, 0, buf)) != VK_SUCCESS) return fail(err, errlen, "vkCreateBuffer", r);
    vkGetBufferMemoryRequirements(vk->dev, *buf, &req);
    type = memory_type(vk->phys, req.memoryTypeBits, cpu_reads);
    if (type < 0) return fail(err, errlen, "no host-visible coherent memory", VK_ERROR_FEATURE_NOT_PRESENT);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if ((r = vkAllocateMemory(vk->dev, &ai, 0, mem)) != VK_SUCCESS) return fail(err, errlen, "vkAllocateMemory", r);
    if ((r = vkBindBufferMemory(vk->dev, *buf, *mem, 0)) != VK_SUCCESS) return fail(err, errlen, "vkBindBufferMemory", r);
    if ((r = vkMapMemory(vk->dev, *mem, 0, VK_WHOLE_SIZE, 0, &m)) != VK_SUCCESS) return fail(err, errlen, "vkMapMemory", r);
    *p = m;
    memset(*p, 0, (size_t)size);
    return 1;
}


static int has_extension(VkPhysicalDevice phys, const char *name) {
    VkExtensionProperties ext[512];
    uint32_t n = 512, i;
    VkResult r = vkEnumerateDeviceExtensionProperties(phys, 0, &n, ext);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return 0;
    for (i = 0; i < n; i++) if (!strcmp(ext[i].extensionName, name)) return 1;
    return 0;
}

/* The caller's video RAM imported as the GPU's memory. */
static int import_host_vram(struct ogpu_vk *vk) {
    VkExternalMemoryBufferCreateInfo ebi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryHostPointerPropertiesEXT hp = { .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
    VkImportMemoryHostPointerInfoEXT imp = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    PFN_vkGetMemoryHostPointerPropertiesEXT props =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(vk->dev, "vkGetMemoryHostPointerPropertiesEXT");
    int type;
    if (!props) return 0;
    if (props(vk->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, vk->cfg.host_vram, &hp) != VK_SUCCESS) return 0;
    ebi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    bi.pNext = &ebi;
    bi.size = vk->cfg.vram_size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk->dev, &bi, 0, &vk->vram_buf) != VK_SUCCESS) return 0;
    vkGetBufferMemoryRequirements(vk->dev, vk->vram_buf, &req);
    type = memory_type(vk->phys, req.memoryTypeBits & hp.memoryTypeBits, 1);
    if (type < 0 || req.size > vk->cfg.vram_size) goto no;
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer = vk->cfg.host_vram;
    ai.pNext = &imp;
    ai.allocationSize = vk->cfg.vram_size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(vk->dev, &ai, 0, &vk->vram_mem) != VK_SUCCESS) goto no;
    if (vkBindBufferMemory(vk->dev, vk->vram_buf, vk->vram_mem, 0) != VK_SUCCESS) goto no;
    vk->vram = vk->cfg.host_vram;
    vk->vram_mode = "import";
    return 1;
no:
    if (vk->vram_mem) vkFreeMemory(vk->dev, vk->vram_mem, 0);
    vkDestroyBuffer(vk->dev, vk->vram_buf, 0);
    vk->vram_mem = 0;
    vk->vram_buf = 0;
    return 0;
}

#ifdef OGPU_VK_MMAP
/* Plain memory back under the caller's video RAM (its contents undefined). */
static void unplace(struct ogpu_vk *vk) {
    if (vk->cfg.place) vk->cfg.place(vk->cfg.user, -1, vk->cfg.vram_size);
    else mmap(vk->cfg.host_vram, vk->cfg.vram_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
}
#endif

/* The GPU's own memory, exported and mapped over the caller's video RAM:
 * what was there is copied in first, so nothing is lost. */
static int map_host_vram(struct ogpu_vk *vk, int dma_buf) {
#ifdef OGPU_VK_MMAP
    VkExternalMemoryHandleTypeFlagBits ht = dma_buf ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
                                                    : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalMemoryBufferCreateInfo ebi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkExportMemoryAllocateInfo exp = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryGetFdInfoKHR gfi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    VkMemoryRequirements req;
    PFN_vkGetMemoryFdKHR get_fd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(vk->dev, "vkGetMemoryFdKHR");
    void *m = 0, *at, *keep = 0;
    volatile ogpu_u8 *probe;
    int type, fd = -1, same;
    if (!get_fd) return 0;
    ebi.handleTypes = (VkExternalMemoryHandleTypeFlags)ht;
    bi.pNext = &ebi;
    bi.size = vk->cfg.vram_size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk->dev, &bi, 0, &vk->vram_buf) != VK_SUCCESS) return 0;
    vkGetBufferMemoryRequirements(vk->dev, vk->vram_buf, &req);
    type = memory_type(vk->phys, req.memoryTypeBits, 1);
    if (type < 0) goto no;
    exp.handleTypes = (VkExternalMemoryHandleTypeFlags)ht;
    ai.pNext = &exp;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(vk->dev, &ai, 0, &vk->vram_mem) != VK_SUCCESS) goto no;
    if (vkBindBufferMemory(vk->dev, vk->vram_buf, vk->vram_mem, 0) != VK_SUCCESS) goto no;
    gfi.memory = vk->vram_mem;
    gfi.handleType = ht;
    if (get_fd(vk->dev, &gfi, &fd) != VK_SUCCESS || fd < 0) goto no;
    if (vkMapMemory(vk->dev, vk->vram_mem, 0, VK_WHOLE_SIZE, 0, &m) != VK_SUCCESS) goto no;
    memcpy(m, vk->cfg.host_vram, vk->cfg.vram_size);
    if (!(keep = malloc(vk->cfg.vram_size))) goto no;
    memcpy(keep, vk->cfg.host_vram, vk->cfg.vram_size);
    if (vk->cfg.place)
        at = vk->cfg.place(vk->cfg.user, fd, vk->cfg.vram_size) ? MAP_FAILED : (void *)vk->cfg.host_vram;
    else
        at = mmap(vk->cfg.host_vram, vk->cfg.vram_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    close(fd);
    fd = -1;
    /* A failed MAP_FIXED leaves the old pages; check a write through the new
     * mapping reaches the GPU's memory too, else put the old pages back. */
    probe = (volatile ogpu_u8 *)m;
    same = at == (void *)vk->cfg.host_vram;
    if (same) {
        vk->cfg.host_vram[0] ^= 0xFF;
        same = probe[0] == vk->cfg.host_vram[0];
        vk->cfg.host_vram[0] ^= 0xFF;
    }
    vkUnmapMemory(vk->dev, vk->vram_mem);
    if (!same) {
        if (at == (void *)vk->cfg.host_vram) unplace(vk);
        memcpy(vk->cfg.host_vram, keep, vk->cfg.vram_size);
        goto no;
    }
    free(keep);
    vk->vram = vk->cfg.host_vram;
    vk->vram_mode = "map";
    vk->vram_mapped = 1;
    return 1;
no:
    free(keep);
    if (fd >= 0) close(fd);
    if (vk->vram_mem) vkFreeMemory(vk->dev, vk->vram_mem, 0);
    vkDestroyBuffer(vk->dev, vk->vram_buf, 0);
    vk->vram_mem = 0;
    vk->vram_buf = 0;
    return 0;
#else
    (void)vk; (void)dma_buf;
    return 0;
#endif
}

static int pick_device(struct ogpu_vk *vk, char *err, int errlen) {
    VkPhysicalDevice devs[16];
    uint32_t n = 16, i, best = 0;
    int best_score = -1;
    const char *want = vk->cfg.device ? vk->cfg.device : getenv("OGPU_VK_DEVICE");
    VkResult r = vkEnumeratePhysicalDevices(vk->inst, &n, devs);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) return fail(err, errlen, "no Vulkan device", r);
    for (i = 0; i < n; i++) {
        VkPhysicalDeviceProperties pr;
        VkQueueFamilyProperties qf[16];
        uint32_t nq = 16, q;
        int score, has_compute = 0;
        vkGetPhysicalDeviceProperties(devs[i], &pr);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
        for (q = 0; q < nq; q++) if (qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) has_compute = 1;
        if (!has_compute) continue;
        if (want && *want) score = strstr(pr.deviceName, want) ? 10 : -1;
        else score = pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                   : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                   : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 1 : 2;
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best_score < 0) return fail(err, errlen, want && *want ? "no Vulkan device by that name" : "no device with compute", VK_ERROR_INITIALIZATION_FAILED);
    vk->phys = devs[best];
    return 1;
}

struct ogpu_vk *ogpu_vk_create(const struct ogpu_vk_config *cfg, char *err, int errlen) {
    struct ogpu_vk *vk = calloc(1, sizeof *vk);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDescriptorSetLayoutBinding b[2];
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPushConstantRange pcr;
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    VkDescriptorPoolSize ps;
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkDescriptorBufferInfo dbi[2];
    VkWriteDescriptorSet wr[2];
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkPhysicalDeviceProperties pr;
    VkQueueFamilyProperties qf[16];
    uint32_t nq = 16, qfam = 0, q;
    float prio = 1.0f;
    VkResult r;
    const char *ext[3];
    int next = 0, can_import = 0, can_map = 0, can_dma_buf = 0;
    const char *hostmem = getenv("OGPU_VK_HOSTMEM");

    if (!vk) { fail(err, errlen, "out of memory", VK_ERROR_OUT_OF_HOST_MEMORY); return 0; }
    vk->cfg = *cfg;
    if (!vk->cfg.arena_size) vk->cfg.arena_size = 4u << 20;
    vk->cfg.arena_size = (vk->cfg.arena_size + 3) & ~3u;
    if (!vk->cfg.vram_size || (vk->cfg.vram_size & 3)) { fail(err, errlen, "vram_size must be a multiple of 4", VK_ERROR_INITIALIZATION_FAILED); goto bad; }

    app.pApplicationName = "OpenGPU";
    app.apiVersion = VK_API_VERSION_1_1;
    ii.pApplicationInfo = &app;
    if ((r = vkCreateInstance(&ii, 0, &vk->inst)) != VK_SUCCESS) { fail(err, errlen, "vkCreateInstance", r); goto bad; }
    if (!pick_device(vk, err, errlen)) goto bad;
    vkGetPhysicalDeviceProperties(vk->phys, &pr);
    snprintf(vk->name, sizeof vk->name, "%s", pr.deviceName);
    if (pr.limits.maxStorageBufferRange < vk->cfg.vram_size || pr.limits.maxStorageBufferRange < vk->cfg.arena_size) {
        fail(err, errlen, "video RAM is larger than the device's biggest storage buffer", VK_ERROR_FEATURE_NOT_PRESENT);
        goto bad;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, qf);
    for (q = 0; q < nq; q++) if (qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfam = q; break; }

    qi.queueFamilyIndex = qfam;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    if (hostmem && !*hostmem) hostmem = 0;
    if (vk->cfg.host_vram) {
        if (!hostmem || !strcmp(hostmem, "import")) {
            if (has_extension(vk->phys, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)) {
                ext[next++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
                can_import = 1;
            }
        }
        if (!hostmem || !strcmp(hostmem, "map")) {
            if (has_extension(vk->phys, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)) {
                ext[next++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
                can_map = 1;
                if (has_extension(vk->phys, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)) {
                    ext[next++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
                    can_dma_buf = 1;
                }
            }
        }
        di.enabledExtensionCount = (uint32_t)next;
        di.ppEnabledExtensionNames = ext;
    }
    if ((r = vkCreateDevice(vk->phys, &di, 0, &vk->dev)) != VK_SUCCESS) { fail(err, errlen, "vkCreateDevice", r); goto bad; }
    vkGetDeviceQueue(vk->dev, qfam, 0, &vk->queue);

    if (vk->cfg.host_vram) {
        /* The GPU's memory mapped over the caller's first, then the
         * caller's imported: a round trip costs a quarter as much mapped
         * (90 us against 340 us on daletop's RX 460). */
        if (!(can_dma_buf && map_host_vram(vk, 1))
            && !(can_map && map_host_vram(vk, 0))
            && !(can_import && import_host_vram(vk))) {
            fail(err, errlen, "the device can't draw in the caller's video RAM", VK_ERROR_FEATURE_NOT_PRESENT);
            goto bad;
        }
    } else {
        if (!make_buffer(vk, vk->cfg.vram_size, 1, &vk->vram_buf, &vk->vram_mem, &vk->vram, err, errlen)) goto bad;
        vk->vram_mode = "own";
    }
    if (!make_buffer(vk, vk->cfg.arena_size, 0, &vk->arena_buf, &vk->arena_mem, &vk->arena, err, errlen)) goto bad;

    memset(b, 0, sizeof b);
    for (q = 0; q < 2; q++) {
        b[q].binding = q;
        b[q].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[q].descriptorCount = 1;
        b[q].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    dli.bindingCount = 2;
    dli.pBindings = b;
    if ((r = vkCreateDescriptorSetLayout(vk->dev, &dli, 0, &vk->dsl)) != VK_SUCCESS) { fail(err, errlen, "vkCreateDescriptorSetLayout", r); goto bad; }
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = A_COUNT * 4;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &vk->dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if ((r = vkCreatePipelineLayout(vk->dev, &pli, 0, &vk->layout)) != VK_SUCCESS) { fail(err, errlen, "vkCreatePipelineLayout", r); goto bad; }
    smi.codeSize = sizeof ogpu_comp_spv;
    smi.pCode = ogpu_comp_spv;
    if ((r = vkCreateShaderModule(vk->dev, &smi, 0, &vk->shader)) != VK_SUCCESS) { fail(err, errlen, "vkCreateShaderModule", r); goto bad; }
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = vk->shader;
    cpi.stage.pName = "main";
    cpi.layout = vk->layout;
    if ((r = vkCreateComputePipelines(vk->dev, VK_NULL_HANDLE, 1, &cpi, 0, &vk->pipe)) != VK_SUCCESS) { fail(err, errlen, "vkCreateComputePipelines", r); goto bad; }

    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps.descriptorCount = 2;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if ((r = vkCreateDescriptorPool(vk->dev, &dpi, 0, &vk->dpool)) != VK_SUCCESS) { fail(err, errlen, "vkCreateDescriptorPool", r); goto bad; }
    dai.descriptorPool = vk->dpool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &vk->dsl;
    if ((r = vkAllocateDescriptorSets(vk->dev, &dai, &vk->dset)) != VK_SUCCESS) { fail(err, errlen, "vkAllocateDescriptorSets", r); goto bad; }
    memset(wr, 0, sizeof wr);
    dbi[0].buffer = vk->vram_buf; dbi[0].offset = 0; dbi[0].range = VK_WHOLE_SIZE;
    dbi[1].buffer = vk->arena_buf; dbi[1].offset = 0; dbi[1].range = VK_WHOLE_SIZE;
    for (q = 0; q < 2; q++) {
        wr[q].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[q].dstSet = vk->dset;
        wr[q].dstBinding = q;
        wr[q].descriptorCount = 1;
        wr[q].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr[q].pBufferInfo = &dbi[q];
    }
    vkUpdateDescriptorSets(vk->dev, 2, wr, 0, 0);

    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex = qfam;
    if ((r = vkCreateCommandPool(vk->dev, &cpci, 0, &vk->cpool)) != VK_SUCCESS) { fail(err, errlen, "vkCreateCommandPool", r); goto bad; }
    cbi.commandPool = vk->cpool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    if ((r = vkAllocateCommandBuffers(vk->dev, &cbi, &vk->cmd)) != VK_SUCCESS) { fail(err, errlen, "vkAllocateCommandBuffers", r); goto bad; }
    if ((r = vkCreateFence(vk->dev, &fi, 0, &vk->done)) != VK_SUCCESS) { fail(err, errlen, "vkCreateFence", r); goto bad; }

    vk->list = malloc(LIST_MAX * sizeof *vk->list);    /* none: every command is its own dispatch */
    vk->route = vk->cfg.route;
    if (vk->route == OGPU_VK_ROUTE_COST && getenv("OGPU_VK_ROUTE")) {
        const char *r = getenv("OGPU_VK_ROUTE");
        vk->route = !strcmp(r, "gpu") ? OGPU_VK_ROUTE_GPU : !strcmp(r, "core") ? OGPU_VK_ROUTE_CORE : OGPU_VK_ROUTE_COST;
    }
    if (vk->route == OGPU_VK_ROUTE_COST) measure_trip(vk);
    core_afresh(vk);
    vk->core.map = core_map;
    vk->core.fence = core_fence;
    vk->core.ext = vk->cfg.ext ? core_ext : 0;
    vk->core.user = vk;
    return vk;
bad:
    ogpu_vk_destroy(vk);
    return 0;
}

void ogpu_vk_destroy(struct ogpu_vk *vk) {
    if (!vk) return;
    if (vk->dev) {
        vkDeviceWaitIdle(vk->dev);
        if (vk->done) vkDestroyFence(vk->dev, vk->done, 0);
        if (vk->cpool) vkDestroyCommandPool(vk->dev, vk->cpool, 0);
        if (vk->dpool) vkDestroyDescriptorPool(vk->dev, vk->dpool, 0);
        if (vk->pipe) vkDestroyPipeline(vk->dev, vk->pipe, 0);
        if (vk->shader) vkDestroyShaderModule(vk->dev, vk->shader, 0);
        if (vk->layout) vkDestroyPipelineLayout(vk->dev, vk->layout, 0);
        if (vk->dsl) vkDestroyDescriptorSetLayout(vk->dev, vk->dsl, 0);
        if (vk->vram_buf) vkDestroyBuffer(vk->dev, vk->vram_buf, 0);
        if (vk->arena_buf) vkDestroyBuffer(vk->dev, vk->arena_buf, 0);
#ifdef OGPU_VK_MMAP
        /* "map": put plain memory back under the caller's video RAM with
         * what the GPU left there, so the caller can carry on. */
        if (vk->vram_mapped) {
            void *keep = malloc(vk->cfg.vram_size);
            if (keep) memcpy(keep, vk->cfg.host_vram, vk->cfg.vram_size);
            unplace(vk);
            if (keep) { memcpy(vk->cfg.host_vram, keep, vk->cfg.vram_size); free(keep); }
        }
#endif
        if (vk->vram_mem) vkFreeMemory(vk->dev, vk->vram_mem, 0);
        if (vk->arena_mem) vkFreeMemory(vk->dev, vk->arena_mem, 0);
        vkDestroyDevice(vk->dev, 0);
    }
    if (vk->inst) vkDestroyInstance(vk->inst, 0);
    free(vk->list);
    free(vk);
}
