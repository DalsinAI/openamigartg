/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The 3D range on the GPU, the host's half: see ogpu3d_rec.h. ogpu_vk.c
 * includes this file (the runtime builds the back end from ogpu_vk.c
 * alone); tests/test_ogpu3d_emul.c builds it by itself. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L  /* clock_gettime */
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ogpu3d_rec.h"
#include "ogpu3d_layout.h"

typedef unsigned int o3r_u32;               /* a GPU word */
typedef unsigned long long o3r_u64;

#define O3R_STAGED   8                      /* surfaces staged in one submit */
#define O3R_RANGES   64                     /* pending writes tracked one by one */
#define O3R_TEX      256                    /* texture cache entries */
#define O3R_MODE_AUTO 0
#define O3R_MODE_GPU  1
#define O3R_MODE_CORE 2

/* Costs the first command of a submit is routed by (ns). The round trip is
 * measured by the back end; the core's cost a pixel and the GPU's are
 * learned as commands run (these are the starting points, from daletop's
 * RX 460 and its Ryzen). */
#define O3R_CORE_PX_NS   6.0
#define O3R_GPU_PX_NS    0.5
#define O3R_STAGE_NS_B   0.15               /* a staged byte, each way */
#define O3R_UPLOAD_NS_B  0.3                /* a texture byte into the cache */

/* A surface the GPU draws in: in video RAM, or staged. */
struct o3r_surf {
    const ogpu_u8 *mem;
    long bpr;
    int w, h, bpp, fmt;
    o3r_u32 buf, off, pitch;                /* where the shader finds it */
};

struct o3r_staged {
    ogpu_u8 *mem;
    long bpr;
    int w, h, bpp;
    o3r_u32 off, pitch;
    int y0, y1;                             /* rows copied in */
};

struct o3r_range { const ogpu_u8 *lo, *hi; };

struct o3r_tex {
    const ogpu_u8 *lvl[OGPU_TEX_LEVELS];
    long bpr[OGPU_TEX_LEVELS];
    const ogpu_u8 *table;
    int levels, w, h, format, bpp;
    o3r_u32 off[OGPU_TEX_LEVELS], pitch[OGPU_TEX_LEVELS], table_off;
    o3r_u64 hash;
    int checked;                            /* its texels were checked since memory last changed */
    const ogpu_u8 *lo, *hi;                 /* the source's bounds, for writes that might touch it */
};

struct o3r_band { o3r_u32 tri; int ty, tx0, tx1; };

struct ogpu3d_rec {
    struct ogpu3d_rec_cfg cfg;
    int mode;
    /* the submit */
    unsigned long data_used, stage_used;    /* bytes */
    struct o3r_staged st[O3R_STAGED];
    int nst;
    struct o3r_range pw[O3R_RANGES];        /* memory pending GPU work writes */
    int npw, pw_all;
    int committed;                          /* 3D work waits to be sent */
    long submit_pixels;
    ogpu_u8 fog_last[OGPU3D_FOGTAB];
    o3r_u32 fog_off, stip_off, pen_off, pal_off;
    const ogpu_u8 *stip_p, *pen_p, *pal_p;
    /* the open group: one target, one dispatch */
    int open;
    struct o3r_surf tg, zb, sb;
    int has_z, has_s;
    struct o3r_band *bands;
    long nb, bcap;
    int gtx0, gty0, gtx1, gty1;
    long entries;
    o3r_u32 *scratch;
    long scap;
    /* the command */
    const struct ogpu3d *t;
    const struct o3_pipe *pp;
    int ax0, ay0, ax1, ay1;
    int began, flushed;
    int lost;                               /* out of host memory: a triangle wasn't recorded */
    int use_z, use_s;
    struct o3r_surf ctg, czb, csb;          /* this command's surfaces */
    struct o3r_tex *tx;
    o3r_u32 state_off;                      /* OGPU3D_NONE: not written in this submit */
    long cmd_pixels;
    double cmd_bytes_ns;                    /* staging and uploads, as time */
    /* the triangle */
    struct o3_tri tri;
    int t_ys, t_ye;
    o3r_u32 *spans;
    long spcap;
    /* the texture cache */
    struct o3r_tex te[O3R_TEX];
    int nte;
    unsigned long tex_used;
    const ogpu_u8 *te_lo, *te_hi;           /* every entry's source within these */
    /* routing */
    double core_px_ns, gpu_px_ns;
    struct ogpu3d_rec_stats stats;
};

static double o3r_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int o3r_bpp(int format) {
    switch (format) {
    case OGPU_FMT_CLUT8: case OGPU_FMT_INDEX8: case OGPU_FMT_A8: case OGPU_FMT_S8: return 1;
    case OGPU_FMT_RGB565: case OGPU_FMT_Z16: return 2;
    case OGPU_FMT_ARGB32: case OGPU_FMT_Z32: return 4;
    }
    return 0;
}

static o3r_u32 o3r_rd32(const ogpu_u8 *p) {
    return ((o3r_u32)p[0] << 24) | ((o3r_u32)p[1] << 16) | ((o3r_u32)p[2] << 8) | (o3r_u32)p[3];
}

static o3r_u32 o3r_pack(const int *c) {
    return ((o3r_u32)(c[3] & 255) << 24) | ((o3r_u32)(c[0] & 255) << 16) | ((o3r_u32)(c[1] & 255) << 8) | (o3r_u32)(c[2] & 255);
}

static int o3r_overlap(const ogpu_u8 *a0, const ogpu_u8 *a1, const ogpu_u8 *b0, const ogpu_u8 *b1) {
    return a0 < b1 && b0 < a1;
}

/* ---- memory pending GPU work writes ------------------------------------------------- */

static void o3r_note(struct ogpu3d_rec *r, const ogpu_u8 *lo, const ogpu_u8 *hi) {
    int i;
    if (lo >= hi) return;
    /* cached textures there are checked again before they are used */
    if (r->nte && o3r_overlap(lo, hi, r->te_lo, r->te_hi))
        for (i = 0; i < r->nte; i++)
            if (o3r_overlap(lo, hi, r->te[i].lo, r->te[i].hi)) r->te[i].checked = 0;
    for (i = 0; i < r->npw; i++)
        if (r->pw[i].lo == lo && r->pw[i].hi == hi) return;
    if (r->npw == O3R_RANGES) { r->pw_all = 1; return; }
    r->pw[r->npw].lo = lo;
    r->pw[r->npw].hi = hi;
    r->npw++;
}

static int o3r_pending_write(const struct ogpu3d_rec *r, const ogpu_u8 *lo, const ogpu_u8 *hi) {
    int i;
    if (r->pw_all) return 1;
    for (i = 0; i < r->npw; i++)
        if (o3r_overlap(lo, hi, r->pw[i].lo, r->pw[i].hi)) return 1;
    return 0;
}

static void o3r_flush(struct ogpu3d_rec *r) {
    r->flushed = 1;
    r->cfg.flush(r->cfg.user);
}

/* The CPU is about to read these bytes: what pending GPU work writes there
 * is drawn first. */
static void o3r_reads(struct ogpu3d_rec *r, const ogpu_u8 *p, unsigned long len) {
    if (p && len && o3r_pending_write(r, p, p + len)) o3r_flush(r);
}

/* ---- the data buffer ------------------------------------------------------------------- */

/* Bytes the open group's tile lists will take when it closes. */
static unsigned long o3r_bins_bytes(const struct ogpu3d_rec *r, int tx0, int ty0, int tx1, int ty1, long entries) {
    if (tx1 < tx0) return 0;
    return ((unsigned long)(tx1 - tx0 + 1) * (unsigned long)(ty1 - ty0 + 1) * 2 + (unsigned long)entries) * 4;
    (void)r;
}

static int o3r_room(const struct ogpu3d_rec *r, unsigned long bytes) {
    unsigned long bins = r->open ? o3r_bins_bytes(r, r->gtx0, r->gty0, r->gtx1, r->gty1, r->entries) : 0;
    return r->data_used + bytes + bins <= r->cfg.data_size;
}

static o3r_u32 o3r_take(struct ogpu3d_rec *r, unsigned long bytes) {
    o3r_u32 at = (o3r_u32)r->data_used;
    r->data_used += (bytes + 3) & ~3UL;
    return at;
}

static void o3r_put_words(struct ogpu3d_rec *r, o3r_u32 at, const o3r_u32 *w, unsigned long n) {
    memcpy(r->cfg.data + at, w, n * 4);
}

/* ---- hashing texels --------------------------------------------------------------------- */

static o3r_u64 o3r_mix(o3r_u64 h, o3r_u64 w) {
    h ^= w;
    h *= 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 32);
}

static o3r_u64 o3r_hash(o3r_u64 h, const ogpu_u8 *p, unsigned long n) {
    o3r_u64 a = h, b = h ^ 0x632BE59BD9B4E019ULL, c = h + 0x85EBCA77C2B2AE63ULL, d = ~h, w0, w1, w2, w3;
    while (n >= 32) {
        memcpy(&w0, p, 8); memcpy(&w1, p + 8, 8); memcpy(&w2, p + 16, 8); memcpy(&w3, p + 24, 8);
        a = o3r_mix(a, w0); b = o3r_mix(b, w1); c = o3r_mix(c, w2); d = o3r_mix(d, w3);
        p += 32; n -= 32;
    }
    while (n >= 8) { memcpy(&w0, p, 8); a = o3r_mix(a, w0); p += 8; n -= 8; }
    w0 = (o3r_u64)n << 56;
    if (n) { o3r_u64 t = 0; memcpy(&t, p, n); w0 ^= t; }
    a = o3r_mix(a, w0);
    return o3r_mix(o3r_mix(o3r_mix(a, b), c), d);
}

/* ---- the texture cache -------------------------------------------------------------------- */

static int o3r_level_w(const struct ogpu3d_tex *tx, int lv) { int w = tx->w >> lv; return w < 1 ? 1 : w; }
static int o3r_level_h(const struct ogpu3d_tex *tx, int lv) { int h = tx->h >> lv; return h < 1 ? 1 : h; }

static unsigned long o3r_tex_bytes(const struct ogpu3d_tex *tx) {
    unsigned long n = 0;
    int lv;
    for (lv = 0; lv < tx->levels; lv++)
        n += (((unsigned long)o3r_level_w(tx, lv) * (unsigned long)tx->bpp + 3) & ~3UL) * (unsigned long)o3r_level_h(tx, lv);
    return n + (tx->table ? 1024 : 0);
}

static int o3r_tex_same(const struct o3r_tex *e, const struct ogpu3d_tex *tx) {
    int lv;
    if (e->levels != tx->levels || e->w != tx->w || e->h != tx->h || e->format != tx->format || e->table != tx->table) return 0;
    for (lv = 0; lv < tx->levels; lv++)
        if (e->lvl[lv] != tx->level[lv] || e->bpr[lv] != tx->bpr[lv]) return 0;
    return 1;
}

/* The texels' hash; with copy, also copied into the cache at the entry's offsets. */
static o3r_u64 o3r_tex_pass(struct ogpu3d_rec *r, const struct o3r_tex *e, int copy) {
    o3r_u64 h = 0x5851F42D4C957F2DULL;
    int lv, y;
    for (lv = 0; lv < e->levels; lv++) {
        int w = e->w >> lv, hh = e->h >> lv;
        unsigned long row;
        if (w < 1) w = 1;
        if (hh < 1) hh = 1;
        row = (unsigned long)w * (unsigned long)e->bpp;
        for (y = 0; y < hh; y++) {
            const ogpu_u8 *src = e->lvl[lv] + (long)y * e->bpr[lv];
            h = o3r_hash(h, src, row);
            if (copy) memcpy(r->cfg.tex + e->off[lv] + (unsigned long)y * e->pitch[lv], src, row);
        }
    }
    if (e->table) {
        h = o3r_hash(h, e->table, 1024);
        if (copy) memcpy(r->cfg.tex + e->table_off, e->table, 1024);
    }
    return h;
}

static void o3r_tex_bounds(struct o3r_tex *e) {
    int lv;
    e->lo = e->hi = 0;
    for (lv = 0; lv < e->levels; lv++) {
        int hh = e->h >> lv, w = e->w >> lv;
        const ogpu_u8 *end;
        if (hh < 1) hh = 1;
        if (w < 1) w = 1;
        end = e->lvl[lv] + (long)(hh - 1) * e->bpr[lv] + (long)w * e->bpp;
        if (!e->lo || e->lvl[lv] < e->lo) e->lo = e->lvl[lv];
        if (end > e->hi) e->hi = end;
    }
    if (e->table) {
        if (e->table < e->lo) e->lo = e->table;
        if (e->table + 1024 > e->hi) e->hi = e->table + 1024;
    }
}

/* Reads a texture's memory: what pending GPU work writes there is drawn first. */
static void o3r_tex_reads(struct ogpu3d_rec *r, const struct ogpu3d_tex *tx) {
    struct o3r_tex probe;
    int lv;
    probe.levels = tx->levels; probe.w = tx->w; probe.h = tx->h; probe.bpp = tx->bpp; probe.table = tx->table;
    for (lv = 0; lv < tx->levels; lv++) { probe.lvl[lv] = tx->level[lv]; probe.bpr[lv] = tx->bpr[lv]; }
    o3r_tex_bounds(&probe);
    o3r_reads(r, probe.lo, (unsigned long)(probe.hi - probe.lo));
}

static void o3r_tex_reset(struct ogpu3d_rec *r) {
    r->nte = 0;
    r->tex_used = 0;
    r->te_lo = r->te_hi = 0;
}

/* The cache entry for a texture, its texels checked: 0 when it can't be cached. */
static struct o3r_tex *o3r_tex_bind(struct ogpu3d_rec *r, const struct ogpu3d_tex *tx) {
    struct o3r_tex *e = 0;
    unsigned long bytes = o3r_tex_bytes(tx), at;
    int i, lv;
    if (bytes > r->cfg.tex_size) return 0;
    for (i = 0; i < r->nte; i++)
        if (o3r_tex_same(&r->te[i], tx)) { e = &r->te[i]; break; }
    if (e && e->checked) { r->stats.tex_hits++; return e; }
    o3r_tex_reads(r, tx);
    if (e) {
        r->stats.tex_checks++;
        if (o3r_tex_pass(r, e, 0) == e->hash) { e->checked = 1; r->cmd_bytes_ns += (double)bytes * 0.1; return e; }
        /* changed: a new place, as pending work may still read the old one */
    }
    if (r->tex_used + bytes > r->cfg.tex_size || (!e && r->nte == O3R_TEX)) {
        if (r->committed || r->cfg.pending(r->cfg.user)) o3r_flush(r);
        o3r_tex_reset(r);
        e = 0;
    }
    if (!e) {
        e = &r->te[r->nte++];
        memset(e, 0, sizeof *e);
        e->levels = tx->levels; e->w = tx->w; e->h = tx->h; e->format = tx->format; e->bpp = tx->bpp; e->table = tx->table;
        for (lv = 0; lv < tx->levels; lv++) { e->lvl[lv] = tx->level[lv]; e->bpr[lv] = tx->bpr[lv]; }
        o3r_tex_bounds(e);
        if (!r->te_lo || e->lo < r->te_lo) r->te_lo = e->lo;
        if (e->hi > r->te_hi) r->te_hi = e->hi;
    }
    at = r->tex_used;
    for (lv = 0; lv < tx->levels; lv++) {
        e->off[lv] = (o3r_u32)at;
        e->pitch[lv] = (o3r_u32)(((unsigned long)o3r_level_w(tx, lv) * (unsigned long)tx->bpp + 3) & ~3UL);
        at += (unsigned long)e->pitch[lv] * (unsigned long)o3r_level_h(tx, lv);
    }
    e->table_off = OGPU3D_NONE;
    if (tx->table) { e->table_off = (o3r_u32)at; at += 1024; }
    r->tex_used = (at + 15) & ~15UL;
    e->hash = o3r_tex_pass(r, e, 1);
    e->checked = 1;
    r->stats.tex_uploads++;
    r->cmd_bytes_ns += (double)bytes * O3R_UPLOAD_NS_B;
    return e;
}

/* ---- staging --------------------------------------------------------------------------- */

static int o3r_in_vram(const struct ogpu3d_rec *r, const ogpu_u8 *p, unsigned long len) {
    return p >= r->cfg.vram && p <= r->cfg.vram + r->cfg.vram_size && (unsigned long)(p - r->cfg.vram) + len <= r->cfg.vram_size;
}

static unsigned long o3r_surf_len(const struct o3r_surf *s) {
    return (unsigned long)(s->bpr * (s->h - 1)) + (unsigned long)s->w * (unsigned long)s->bpp;
}

static struct o3r_staged *o3r_find_staged(struct ogpu3d_rec *r, const struct o3r_surf *s) {
    int i;
    for (i = 0; i < r->nst; i++)
        if (r->st[i].mem == s->mem && r->st[i].bpr == s->bpr && r->st[i].w == s->w && r->st[i].h == s->h && r->st[i].bpp == s->bpp)
            return &r->st[i];
    return 0;
}

/* What staging a surface needs: 0 nothing (in video RAM, or staged already),
 * 1 room (*bytes), -1 a flush first (pending work writes its memory). */
static int o3r_stage_need(struct ogpu3d_rec *r, const struct o3r_surf *s, unsigned long *bytes) {
    unsigned long len = o3r_surf_len(s);
    *bytes = 0;
    if (o3r_in_vram(r, s->mem, len) || o3r_find_staged(r, s)) return 0;
    if (o3r_pending_write(r, s->mem, s->mem + len)) return -1;
    *bytes = ((((unsigned long)s->w * (unsigned long)s->bpp + 3) & ~3UL) * (unsigned long)s->h + 15) & ~15UL;
    return 1;
}

/* Where the shader finds the surface; rows y0..y1-1 copied in if staged. */
static void o3r_place(struct ogpu3d_rec *r, struct o3r_surf *s, int y0, int y1) {
    unsigned long len = o3r_surf_len(s), row = (unsigned long)s->w * (unsigned long)s->bpp;
    struct o3r_staged *g;
    int y;
    if (o3r_in_vram(r, s->mem, len)) {
        s->buf = B3_VRAM; s->off = (o3r_u32)(s->mem - r->cfg.vram); s->pitch = (o3r_u32)s->bpr;
        o3r_note(r, s->mem, s->mem + len);
        return;
    }
    g = o3r_find_staged(r, s);
    if (!g) {
        g = &r->st[r->nst++];
        g->mem = (ogpu_u8 *)s->mem; g->bpr = s->bpr; g->w = s->w; g->h = s->h; g->bpp = s->bpp;
        g->pitch = (o3r_u32)((row + 3) & ~3UL);
        g->off = (o3r_u32)r->stage_used;
        r->stage_used += ((unsigned long)g->pitch * (unsigned long)s->h + 15) & ~15UL;
        g->y0 = g->y1 = 0;
        o3r_note(r, s->mem, s->mem + len);
    }
    if (y0 < 0) y0 = 0;
    if (y1 > s->h) y1 = s->h;
    if (g->y0 == g->y1) { g->y0 = y0; g->y1 = y0; }
    for (y = y0; y < g->y0; y++) memcpy(r->cfg.stage + g->off + (unsigned long)y * g->pitch, g->mem + (long)y * g->bpr, row);
    for (y = g->y1; y < y1; y++)          /* from the last row in: the rows between too */
        memcpy(r->cfg.stage + g->off + (unsigned long)y * g->pitch, g->mem + (long)y * g->bpr, row);
    if (y0 < g->y0) { r->stats.staged_rows += (unsigned long)(g->y0 - y0); r->cmd_bytes_ns += 2.0 * O3R_STAGE_NS_B * (double)row * (g->y0 - y0); g->y0 = y0; }
    if (y1 > g->y1) { r->stats.staged_rows += (unsigned long)(y1 - g->y1); r->cmd_bytes_ns += 2.0 * O3R_STAGE_NS_B * (double)row * (y1 - g->y1); g->y1 = y1; }
    s->buf = B3_STAGE; s->off = g->off; s->pitch = g->pitch;
}

/* ---- groups ---------------------------------------------------------------------------- */

static int o3r_same_surf(const struct o3r_surf *a, const struct o3r_surf *b) {
    return a->mem == b->mem && a->bpr == b->bpr && a->w == b->w && a->h == b->h && a->fmt == b->fmt;
}

void ogpu3d_rec_close(struct ogpu3d_rec *r) {
    o3r_u32 pc[P3_COUNT], *hdr, *ent;
    long tiles, i, k, tw, th;
    unsigned long need;
    o3r_u32 bins, first;
    if (!r || !r->open) return;
    r->open = 0;
    if (!r->nb) return;
    tw = r->gtx1 - r->gtx0 + 1;
    th = r->gty1 - r->gty0 + 1;
    tiles = tw * th;
    need = (unsigned long)(tiles * 2 + r->entries);
    if ((long)need > r->scap) {
        o3r_u32 *n = realloc(r->scratch, need * 4 + (unsigned long)tiles * 4);
        if (!n) { r->nb = 0; return; }      /* can't happen: sized on the way */
        r->scratch = n;
        r->scap = (long)need;
    }
    /* counts, then each tile's first entry, then the entries in stream order */
    hdr = r->scratch;
    ent = r->scratch + tiles * 2;
    for (i = 0; i < tiles; i++) hdr[i * 2 + 1] = 0;
    for (i = 0; i < r->nb; i++) {
        const struct o3r_band *b = &r->bands[i];
        long row = (long)(b->ty - r->gty0) * tw - r->gtx0;
        for (k = b->tx0; k <= b->tx1; k++) hdr[(row + k) * 2 + 1]++;
    }
    bins = (o3r_u32)(r->data_used / 4);
    first = bins + (o3r_u32)(tiles * 2);
    for (i = 0; i < tiles; i++) { hdr[i * 2] = first; first += hdr[i * 2 + 1]; hdr[i * 2 + 1] = 0; }
    for (i = 0; i < r->nb; i++) {
        const struct o3r_band *b = &r->bands[i];
        long row = (long)(b->ty - r->gty0) * tw - r->gtx0;
        for (k = b->tx0; k <= b->tx1; k++) {
            long tl = row + k;
            ent[hdr[tl * 2] - bins - (o3r_u32)(tiles * 2) + hdr[tl * 2 + 1]] = b->tri;
            hdr[tl * 2 + 1]++;
        }
    }
    o3r_put_words(r, o3r_take(r, need * 4), r->scratch, need);
    memset(pc, 0, sizeof pc);
    pc[P3_TBUF] = r->tg.buf; pc[P3_TOFF] = r->tg.off; pc[P3_TBPR] = r->tg.pitch; pc[P3_TFMT] = (o3r_u32)r->tg.fmt;
    if (r->has_z) { pc[P3_ZBUF] = r->zb.buf; pc[P3_ZOFF] = r->zb.off; pc[P3_ZBPR] = r->zb.pitch; pc[P3_ZFMT] = (o3r_u32)r->zb.fmt; }
    if (r->has_s) { pc[P3_SBUF] = r->sb.buf; pc[P3_SOFF] = r->sb.off; pc[P3_SBPR] = r->sb.pitch; }
    pc[P3_GX0] = (o3r_u32)(r->gtx0 * OGPU3D_TILE);
    pc[P3_GY0] = (o3r_u32)(r->gty0 * OGPU3D_TILE);
    pc[P3_GTW] = (o3r_u32)tw;
    pc[P3_BINS] = bins;
    r->cfg.dispatch(r->cfg.user, pc, (unsigned)tw, (unsigned)th);
    r->nb = 0;
    r->entries = 0;
    r->stats.groups++;
}

/* Everything this submit has recorded, as if never recorded: nothing was sent. */
static void o3r_forget(struct ogpu3d_rec *r) {
    r->open = 0; r->nb = 0; r->entries = 0;
    r->data_used = 0; r->stage_used = 0; r->nst = 0;
    r->npw = 0; r->pw_all = 0;
    r->committed = 0; r->submit_pixels = 0;
    r->fog_off = r->stip_off = r->pen_off = r->pal_off = OGPU3D_NONE;
    r->stip_p = r->pen_p = r->pal_p = 0;
    r->state_off = OGPU3D_NONE;
}

void ogpu3d_rec_done(struct ogpu3d_rec *r, double wait_ns) {
    int i, y;
    if (!r) return;
    for (i = 0; i < r->nst; i++) {
        struct o3r_staged *g = &r->st[i];
        unsigned long row = (unsigned long)g->w * (unsigned long)g->bpp;
        for (y = g->y0; y < g->y1; y++) memcpy(g->mem + (long)y * g->bpr, r->cfg.stage + g->off + (unsigned long)y * g->pitch, row);
    }
    if (r->submit_pixels > 20000 && wait_ns > 0) {
        double px = (wait_ns - r->cfg.round_trip_ns) / (double)r->submit_pixels;
        if (px < 0.01) px = 0.01;
        if (px > 50) px = 50;
        r->gpu_px_ns += (px - r->gpu_px_ns) * 0.25;
    }
    o3r_forget(r);
}

int ogpu3d_rec_staged(const struct ogpu3d_rec *r) { return r && r->nst > 0; }

void ogpu3d_rec_wrote(struct ogpu3d_rec *r, const ogpu_u8 *p, unsigned long len) {
    if (r && p && len) o3r_note(r, p, p + len);
}

void ogpu3d_rec_cpu_wrote(struct ogpu3d_rec *r) {
    int i;
    if (!r) return;
    for (i = 0; i < r->nte; i++) r->te[i].checked = 0;
}

/* ---- a command's draw state ---------------------------------------------------------------- */

/* The surfaces a command draws in, and whether the shader draws it exactly
 * as the core does. No side effects. */
static int o3r_can(struct ogpu3d_rec *r) {
    const struct ogpu3d *t = r->t;
    const struct o3_pipe *pp = r->pp;
    const struct ogpu_surface *d = pp->dst;
    const ogpu_u8 *wr[3][2], *rd[6][2];
    int nw = 0, nr = 0, i, j;
    unsigned long worst;
    r->ctg.mem = d->pixels; r->ctg.bpr = d->bpr; r->ctg.w = d->w; r->ctg.h = d->h; r->ctg.fmt = pp->fmt;
    r->ctg.bpp = pp->fmt == OGPU_FMT_ARGB32 ? 4 : pp->fmt == OGPU_FMT_RGB565 ? 2 : 1;
    if (r->ctg.bpr < (long)r->ctg.w * r->ctg.bpp || r->ctg.w > 65535) return 0;
    wr[nw][0] = r->ctg.mem; wr[nw][1] = r->ctg.mem + o3r_surf_len(&r->ctg); nw++;
    r->use_z = (pp->en & OGPU_R3D_ZTEST) && t->depth.mem;
    r->use_s = (pp->en & OGPU_R3D_STENCIL) && t->stencil.mem;
    if (r->use_z) {
        r->czb.mem = t->depth.mem; r->czb.bpr = t->depth.bpr; r->czb.w = t->depth.w; r->czb.h = t->depth.h;
        r->czb.fmt = t->depth.format; r->czb.bpp = o3r_bpp(t->depth.format);
        if (r->czb.bpr < (long)r->czb.w * r->czb.bpp) return 0;
        wr[nw][0] = r->czb.mem; wr[nw][1] = r->czb.mem + o3r_surf_len(&r->czb); nw++;
    }
    if (r->use_s) {
        r->csb.mem = t->stencil.mem; r->csb.bpr = t->stencil.bpr; r->csb.w = t->stencil.w; r->csb.h = t->stencil.h;
        r->csb.fmt = OGPU_FMT_S8; r->csb.bpp = 1;
        if (r->csb.bpr < (long)r->csb.w) return 0;
        wr[nw][0] = r->csb.mem; wr[nw][1] = r->csb.mem + o3r_surf_len(&r->csb); nw++;
    }
    if (pp->tx) {
        const struct ogpu3d_tex *tx = pp->tx;
        struct o3r_tex probe;
        int f = tx->format, lv;
        if (f != OGPU_FMT_ARGB32 && f != OGPU_FMT_RGB565 && f != OGPU_FMT_A8 && f != OGPU_FMT_INDEX8 && f != OGPU_FMT_CLUT8) return 0;
        if (tx->bpp != o3r_bpp(f) || tx->levels < 1 || tx->levels > OGPU_TEX_LEVELS) return 0;
        for (lv = 0; lv < tx->levels; lv++) if (!tx->level[lv] || tx->bpr[lv] < (long)o3r_level_w(tx, lv) * tx->bpp) return 0;
        if (o3r_tex_bytes(tx) > r->cfg.tex_size) return 0;
        probe.levels = tx->levels; probe.w = tx->w; probe.h = tx->h; probe.bpp = tx->bpp; probe.table = tx->table;
        for (lv = 0; lv < tx->levels; lv++) { probe.lvl[lv] = tx->level[lv]; probe.bpr[lv] = tx->bpr[lv]; }
        o3r_tex_bounds(&probe);
        rd[nr][0] = probe.lo; rd[nr][1] = probe.hi; nr++;
    }
    if ((pp->en & OGPU_R3D_STIPPLE) && t->stipple) { rd[nr][0] = t->stipple; rd[nr][1] = t->stipple + 128; nr++; }
    if (pp->fmt == OGPU_FMT_CLUT8 && t->pen_table) { rd[nr][0] = t->pen_table; rd[nr][1] = t->pen_table + 32768; nr++; }
    if (pp->fmt == OGPU_FMT_CLUT8 && t->palette) { rd[nr][0] = t->palette; rd[nr][1] = t->palette + 1024; nr++; }
    /* what one pixel writes, no other pixel's work may read or write */
    for (i = 0; i < nw; i++) {
        for (j = i + 1; j < nw; j++) if (o3r_overlap(wr[i][0], wr[i][1], wr[j][0], wr[j][1])) return 0;
        for (j = 0; j < nr; j++) if (o3r_overlap(wr[i][0], wr[i][1], rd[j][0], rd[j][1])) return 0;
    }
    /* the largest triangle the area holds fits an empty data buffer */
    worst = (unsigned long)(TR_WORDS + (r->ay1 - r->ay0)) * 4
          + o3r_bins_bytes(r, 0, 0, (r->ax1 - r->ax0) / OGPU3D_TILE + 1, (r->ay1 - r->ay0) / OGPU3D_TILE + 1,
                           ((long)(r->ax1 - r->ax0) / OGPU3D_TILE + 2) * ((r->ay1 - r->ay0) / OGPU3D_TILE + 2))
          + ST_WORDS * 4 + 1024 + 128 + 32768 + 1024;
    if (worst > r->cfg.data_size / 2) return 0;
    {
        unsigned long need = 0, b;
        if (o3r_stage_need(r, &r->ctg, &b) > 0) need += b;
        if (r->use_z) { r->czb.fmt = t->depth.format; if (o3r_stage_need(r, &r->czb, &b) > 0) need += b; }
        if (r->use_s && o3r_stage_need(r, &r->csb, &b) > 0) need += b;
        if (need > r->cfg.stage_size) return 0;
    }
    return 1;
}

static o3r_u32 o3r_blob(struct ogpu3d_rec *r, const ogpu_u8 *p, unsigned long len, const ogpu_u8 **last, o3r_u32 *last_off) {
    o3r_u32 at;
    if (*last == p && *last_off != OGPU3D_NONE) return *last_off;
    at = o3r_take(r, len);
    memcpy(r->cfg.data + at, p, len);
    *last = p; *last_off = at;
    return at;
}

/* Writes the command's draw state (o3_span's choices, worked out once). */
static void o3r_write_state(struct ogpu3d_rec *r) {
    const struct ogpu3d *t = r->t;
    const struct o3_pipe *pp = r->pp;
    o3r_u32 s[ST_WORDS], en = pp->en, m = 0;
    int fog, lv;
    memset(s, 0, sizeof s);
    if (en & OGPU_R3D_GOURAUD) m |= M_GOURAUD;
    if (pp->tx) m |= M_TEX;
    if ((en & OGPU_R3D_SPECULAR) && pp->has_spec) m |= M_SPEC;
    fog = (en & OGPU_R3D_FOG) ? t->fogmode : 0;
    if (!pp->has_w) fog = fog == OGPU_FOG_VERTEX ? fog : 0;
    if (fog == OGPU_FOG_VERTEX && !pp->has_spec) fog = 0;
    if (fog == OGPU_FOG_VERTEX) m |= M_FOGV;
    else if (fog) m |= M_FOGT;
    if (en & OGPU_R3D_ALPHATEST) m |= M_ATEST;
    if ((en & OGPU_R3D_CHROMA) && t->chroma > 1) m |= M_CHROMA;
    if (t->chroma == 2) m |= M_CHROMA_IN;
    if ((en & OGPU_R3D_LOGICOP) && t->logicop != 4) m |= M_LOGIC;
    if ((en & OGPU_R3D_BLEND) && !(pp->bsrc == 2 && pp->bdst == 1)) m |= M_BLEND;
    if (en & OGPU_R3D_ZWRITE) m |= M_ZWRITE;
    if (r->use_z) m |= M_ZTEST;
    if (r->use_s) m |= M_STENCIL;
    if ((en & OGPU_R3D_STIPPLE) && t->stipple) m |= M_STIPPLE;
    if (pp->mask_all) m |= M_MASKALL;
    s[ST_MODE] = m;
    s[ST_ENV] = (o3r_u32)pp->env; s[ST_BASE] = (o3r_u32)pp->base; s[ST_ENVC] = o3r_pack(pp->envc);
    s[ST_BSRC] = (o3r_u32)pp->bsrc; s[ST_BDST] = (o3r_u32)pp->bdst;
    s[ST_CUR] = o3r_pack(pp->cur); s[ST_FOGC] = o3r_pack(pp->fog); s[ST_KC] = o3r_pack(pp->kc);
    s[ST_WMASK] = pp->wmask;
    s[ST_ZFUNC] = (o3r_u32)t->zfunc; s[ST_AFUNC] = (o3r_u32)t->afunc; s[ST_AREF] = (o3r_u32)t->aref;
    s[ST_LOGICOP] = (o3r_u32)t->logicop;
    s[ST_CHLO] = (o3r_u32)t->chroma_lo; s[ST_CHHI] = (o3r_u32)t->chroma_hi;
    s[ST_SFUNC] = (o3r_u32)t->sfunc; s[ST_SREF] = (o3r_u32)t->sref; s[ST_SMASK] = (o3r_u32)t->smask;
    s[ST_SFAIL] = (o3r_u32)t->sfail; s[ST_SZFAIL] = (o3r_u32)t->szfail; s[ST_SZPASS] = (o3r_u32)t->szpass;
    s[ST_SWMASK] = (o3r_u32)t->swmask;
    s[ST_FOGTAB] = s[ST_STIPPLE] = s[ST_PEN] = s[ST_PAL] = s[ST_TABLE] = OGPU3D_NONE;
    if (m & M_FOGT) {
        if (r->fog_off == OGPU3D_NONE || memcmp(r->fog_last, t->fogtab, OGPU3D_FOGTAB)) {
            r->fog_off = o3r_take(r, OGPU3D_FOGTAB);
            memcpy(r->cfg.data + r->fog_off, t->fogtab, OGPU3D_FOGTAB);
            memcpy(r->fog_last, t->fogtab, OGPU3D_FOGTAB);
        }
        s[ST_FOGTAB] = r->fog_off;
    }
    if (m & M_STIPPLE) s[ST_STIPPLE] = o3r_blob(r, t->stipple, 128, &r->stip_p, &r->stip_off);
    if (pp->fmt == OGPU_FMT_CLUT8 && t->pen_table) s[ST_PEN] = o3r_blob(r, t->pen_table, 32768, &r->pen_p, &r->pen_off);
    if (pp->fmt == OGPU_FMT_CLUT8 && t->palette) s[ST_PAL] = o3r_blob(r, t->palette, 1024, &r->pal_p, &r->pal_off);
    if (pp->tx) {
        const struct ogpu3d_tex *tx = pp->tx;
        s[ST_TEXW] = (o3r_u32)tx->w; s[ST_TEXH] = (o3r_u32)tx->h; s[ST_TEXFMT] = (o3r_u32)tx->format;
        s[ST_TEXBPP] = (o3r_u32)tx->bpp; s[ST_WRAPS] = (o3r_u32)tx->wrap_s; s[ST_WRAPT] = (o3r_u32)tx->wrap_t;
        s[ST_BORDER] = (o3r_u32)tx->border;
        s[ST_TABLE] = r->tx->table_off;
        for (lv = 0; lv < tx->levels; lv++) { s[ST_LEVEL + lv] = r->tx->off[lv]; s[ST_PITCH + lv] = r->tx->pitch[lv]; }
    }
    s[ST_BLEND] = pp->bsrc == 7 && pp->bdst == 8 ? 1 : pp->bsrc == 2 && pp->bdst == 2 ? 2 : 3;
    r->state_off = o3r_take(r, sizeof s);
    o3r_put_words(r, r->state_off, s, ST_WORDS);
    r->state_off /= 4;
}

/* Bytes the state and its tables take, at most. */
static unsigned long o3r_state_bytes(const struct ogpu3d_rec *r) {
    unsigned long n = ST_WORDS * 4 + OGPU3D_FOGTAB + 128;
    if (r->pp->fmt == OGPU_FMT_CLUT8) n += 32768 + 1024;
    return n;
}

/* Gets a command ready: its texture cached, its surfaces placed (a group
 * open for its target), its state written. 0: the core draws it. */
static int o3r_prepare(struct ogpu3d_rec *r) {
    const struct ogpu3d *t = r->t;
    const struct o3_pipe *pp = r->pp;
    int attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        int again = 0;
        unsigned long stage = 0, b;
        int k;
        if (pp->tx && !(r->tx = o3r_tex_bind(r, pp->tx))) return 0;
        if ((pp->en & OGPU_R3D_STIPPLE) && t->stipple && o3r_pending_write(r, t->stipple, t->stipple + 128)) again = 1;
        if (pp->fmt == OGPU_FMT_CLUT8 && t->pen_table && o3r_pending_write(r, t->pen_table, t->pen_table + 32768)) again = 1;
        if (pp->fmt == OGPU_FMT_CLUT8 && t->palette && o3r_pending_write(r, t->palette, t->palette + 1024)) again = 1;
        k = o3r_stage_need(r, &r->ctg, &b); if (k < 0) again = 1; else stage += b;
        if (r->use_z) { k = o3r_stage_need(r, &r->czb, &b); if (k < 0) again = 1; else stage += b; }
        if (r->use_s) { k = o3r_stage_need(r, &r->csb, &b); if (k < 0) again = 1; else stage += b; }
        if (r->stage_used + stage > r->cfg.stage_size || r->nst + 3 > O3R_STAGED) again = 1;
        if (!o3r_room(r, o3r_state_bytes(r) + (unsigned long)(TR_WORDS + r->ay1 - r->ay0) * 4)) again = 1;
        if (!again) break;
        if (attempt) return 0;
        o3r_flush(r);
    }
    /* the group: one target; a Z or stencil buffer joins it if it has none yet */
    if (r->open && (!o3r_same_surf(&r->tg, &r->ctg) || (r->use_z && r->has_z && !o3r_same_surf(&r->zb, &r->czb))
                    || (r->use_s && r->has_s && !o3r_same_surf(&r->sb, &r->csb))))
        ogpu3d_rec_close(r);
    o3r_place(r, &r->ctg, r->ay0, r->ay1);
    if (r->use_z) o3r_place(r, &r->czb, r->ay0, r->ay1);
    if (r->use_s) o3r_place(r, &r->csb, r->ay0, r->ay1);
    if (!r->open) {
        r->open = 1;
        r->tg = r->ctg;
        r->has_z = r->has_s = 0;
        r->nb = 0; r->entries = 0;
        r->gtx0 = r->gty0 = 0x7FFFFFFF; r->gtx1 = r->gty1 = -1;
    }
    if (r->use_z && !r->has_z) { r->has_z = 1; r->zb = r->czb; }
    if (r->use_s && !r->has_s) { r->has_s = 1; r->sb = r->csb; }
    o3r_write_state(r);
    return 1;
}

/* ---- the sink ---------------------------------------------------------------------------- */

static int o3r_begin(void *user, const struct ogpu3d *t, const struct o3_pipe *pp, int x0, int y0, int x1, int y1) {
    struct ogpu3d_rec *r = user;
    r->began = 1;
    r->t = t; r->pp = pp;
    r->ax0 = x0; r->ay0 = y0; r->ax1 = x1; r->ay1 = y1;
    r->tx = 0;
    if (!o3r_can(r) || !o3r_prepare(r)) { r->stats.declined++; return 1; }
    return 0;
}

static void o3r_tri(void *user, const struct o3_tri *tr, int ystart, int yend) {
    struct ogpu3d_rec *r = user;
    long rows = yend - ystart;
    r->tri = *tr;
    r->t_ys = ystart; r->t_ye = yend;
    if (rows > r->spcap) {
        o3r_u32 *n = realloc(r->spans, (size_t)rows * 2 * sizeof *n);
        if (n) { r->spans = n; r->spcap = rows * 2; }
        else r->lost = 1;
    }
}

/* A triangle, its spans all in: written out, with its place in the tiles. */
static void o3r_commit(struct ogpu3d_rec *r) {
    long rows = r->t_ye - r->t_ys, i, entries = 0, px = 0;
    int tx0 = 0x7FFFFFFF, ty0 = 0x7FFFFFFF, tx1 = -1, ty1 = -1, ty, k;
    o3r_u32 rec[TR_WORDS], at;
    unsigned long need;
    /* the bands: each tile row's columns */
    for (i = 0; i < rows; i++) {
        o3r_u32 sp = r->spans[i];
        int x0 = (int)(sp & 0xFFFF), x1 = (int)(sp >> 16);
        if (x0 >= x1) continue;
        px += x1 - x0;
        ty = (r->t_ys + (int)i) / OGPU3D_TILE;
        if (ty < ty0) ty0 = ty;
        if (ty > ty1) ty1 = ty;
        if (x0 / OGPU3D_TILE < tx0) tx0 = x0 / OGPU3D_TILE;
        if ((x1 - 1) / OGPU3D_TILE > tx1) tx1 = (x1 - 1) / OGPU3D_TILE;
    }
    if (!px) return;
    for (ty = ty0; ty <= ty1; ty++) {
        int a = 0x7FFFFFFF, b = -1, y;
        for (y = ty * OGPU3D_TILE; y < (ty + 1) * OGPU3D_TILE; y++) {
            o3r_u32 sp;
            int x0, x1;
            if (y < r->t_ys || y >= r->t_ye) continue;
            sp = r->spans[y - r->t_ys];
            x0 = (int)(sp & 0xFFFF); x1 = (int)(sp >> 16);
            if (x0 >= x1) continue;
            if (x0 / OGPU3D_TILE < a) a = x0 / OGPU3D_TILE;
            if ((x1 - 1) / OGPU3D_TILE > b) b = (x1 - 1) / OGPU3D_TILE;
        }
        if (b >= a) entries += b - a + 1;
    }
    if (r->nb + (ty1 - ty0 + 1) > r->bcap) {
        long cap = (r->nb + (ty1 - ty0 + 1)) * 2;
        struct o3r_band *n = realloc(r->bands, (size_t)cap * sizeof *n);
        if (!n) { r->lost = 1; return; }
        r->bands = n;
        r->bcap = cap;
    }
    /* room: the record, its spans and the group's tile lists with it */
    for (k = 0; k < 2; k++) {
        int gx0 = r->gtx0 < tx0 ? r->gtx0 : tx0, gy0 = r->gty0 < ty0 ? r->gty0 : ty0;
        int gx1 = r->gtx1 > tx1 ? r->gtx1 : tx1, gy1 = r->gty1 > ty1 ? r->gty1 : ty1;
        need = (unsigned long)(TR_WORDS + rows) * 4 + o3r_bins_bytes(r, gx0, gy0, gx1, gy1, r->entries + entries);
        if (r->data_used + need <= r->cfg.data_size) break;
        /* full: send what is recorded, then carry on with this command
         * (o3r_can made sure its largest triangle fits an empty buffer) */
        if (k || (o3r_flush(r), !o3r_prepare(r))) { r->lost = 1; return; }
    }
    memset(rec, 0, sizeof rec);
    rec[TR_STATE] = r->state_off;
    rec[TR_YS] = (o3r_u32)r->t_ys;
    rec[TR_YE] = (o3r_u32)r->t_ye;
    rec[TR_RX] = (o3r_u32)r->tri.rx;
    rec[TR_FLAGS] = (r->tri.persp ? TRF_PERSP : 0) | (r->tri.bilinear ? TRF_BILINEAR : 0)
                  | ((o3r_u32)(r->tri.lfrac & 255) << 8) | ((o3r_u32)(r->tri.level & 255) << 16);
    for (i = 0; i < r->tri.nact; i++) {
        int a = r->tri.act[i];
        rec[TR_GX + a] = (o3r_u32)r->tri.gx[a];
        rec[TR_GY + a] = (o3r_u32)r->tri.gy[a];
        rec[TR_ROW + a] = (o3r_u32)r->tri.row[a];
    }
    at = o3r_take(r, TR_WORDS * 4);
    rec[TR_SPANS] = (o3r_u32)(r->data_used / 4);
    o3r_put_words(r, at, rec, TR_WORDS);
    o3r_put_words(r, o3r_take(r, (unsigned long)rows * 4), r->spans, (unsigned long)rows);
    /* the bands, in order */
    for (ty = ty0; ty <= ty1; ty++) {
        int a = 0x7FFFFFFF, b = -1, y;
        for (y = ty * OGPU3D_TILE; y < (ty + 1) * OGPU3D_TILE; y++) {
            o3r_u32 sp;
            int x0, x1;
            if (y < r->t_ys || y >= r->t_ye) continue;
            sp = r->spans[y - r->t_ys];
            x0 = (int)(sp & 0xFFFF); x1 = (int)(sp >> 16);
            if (x0 >= x1) continue;
            if (x0 / OGPU3D_TILE < a) a = x0 / OGPU3D_TILE;
            if ((x1 - 1) / OGPU3D_TILE > b) b = (x1 - 1) / OGPU3D_TILE;
        }
        if (b < a) continue;
        r->bands[r->nb].tri = at / 4;
        r->bands[r->nb].ty = ty;
        r->bands[r->nb].tx0 = a;
        r->bands[r->nb].tx1 = b;
        r->nb++;
    }
    r->entries += entries;
    if (tx0 < r->gtx0) r->gtx0 = tx0;
    if (ty0 < r->gty0) r->gty0 = ty0;
    if (tx1 > r->gtx1) r->gtx1 = tx1;
    if (ty1 > r->gty1) r->gty1 = ty1;
    r->cmd_pixels += px;
    r->stats.triangles++;
}

static void o3r_span(void *user, int y, int x0, int x1) {
    struct ogpu3d_rec *r = user;
    long i = y - r->t_ys;
    if (i < 0 || i >= r->spcap || i >= r->t_ye - r->t_ys) return;
    r->spans[i] = x0 < x1 ? (o3r_u32)x0 | ((o3r_u32)x1 << 16) : 0;
    if (y == r->t_ye - 1) o3r_commit(r);
}

static const struct ogpu3d_sink o3r_sink = { o3r_begin, o3r_tri, o3r_span };

/* ---- TRIANGLES -------------------------------------------------------------------------- */

static int o3r_vertex_size(o3r_u32 layout) {
    int n = 20;
    if (layout & OGPU_LAY_Z) n += 4;
    if (layout & OGPU_LAY_W) n += 4;
    if (layout & OGPU_LAY_SPEC) n += 4;
    if (layout & OGPU_LAY_UV1) n += 8;
    return n;
}

int ogpu3d_rec_triangles(struct ogpu3d_rec *r, struct ogpu3d *t, const ogpu_u8 *cmd, long words, int *result) {
    struct ogpu_core *c = t->core;
    const ogpu_u8 *a = cmd + 4;
    long args = words - 1;
    int was_pending, res;
    double t0, t1;
    if (!r || r->mode == O3R_MODE_CORE || args < 8) return 0;
    /* the setup reads vertices and indices on the CPU: drawn first if pending work writes them */
    {
        o3r_u32 vaddr = o3r_rd32(a), vcount = o3r_rd32(a + 4), stride = o3r_rd32(a + 8);
        o3r_u32 iaddr = o3r_rd32(a + 12), icount = o3r_rd32(a + 16), isize = o3r_rd32(a + 20);
        o3r_u32 layout = args >= 9 ? o3r_rd32(a + 32) : 0, vsize = (o3r_u32)o3r_vertex_size(layout);
        if (vcount && vcount <= 0x00FFFFFFu && stride >= vsize && stride <= 0x10000u) {
            unsigned long len = (unsigned long)stride * (vcount - 1) + vsize;
            o3r_reads(r, c->map(c->user, vaddr, (ogpu_u32)len), len);
        }
        if (iaddr && icount && icount <= 0x00FFFFFFu && (isize == 1 || isize == 2 || isize == 4))
            o3r_reads(r, c->map(c->user, iaddr, (ogpu_u32)(icount * isize)), (unsigned long)icount * isize);
    }
    was_pending = r->committed || r->cfg.pending(r->cfg.user);
    r->began = 0; r->flushed = 0; r->lost = 0; r->cmd_pixels = 0; r->cmd_bytes_ns = 0;
    t->sink = &o3r_sink;
    t->sink_user = r;
    t0 = o3r_now();
    res = ogpu3d_op(t, OGPU_OP_TRIANGLES, cmd, words);
    t1 = o3r_now();
    t->sink = 0;
    t->sink_user = 0;
    if (res == OGPU3D_DECLINED) return 0;
    *result = res;
    if (!r->began) return 1;                    /* nothing to draw */
    if (r->lost) r->stats.lost++;
    if (!r->cmd_pixels && !was_pending && !r->flushed) { o3r_forget(r); return 1; }
    if (r->mode == O3R_MODE_AUTO && !was_pending && !r->flushed) {
        double core = (t1 - t0) + (double)r->cmd_pixels * r->core_px_ns;
        double gpu = r->cfg.round_trip_ns + r->cmd_bytes_ns + (double)r->cmd_pixels * r->gpu_px_ns;
        if (core < gpu) {
            /* quicker on the core: nothing recorded goes, the core draws it now */
            long px0 = t->pixels;
            double t2, t3;
            o3r_forget(r);
            t2 = o3r_now();
            *result = ogpu3d_op(t, OGPU_OP_TRIANGLES, cmd, words);
            t3 = o3r_now();
            if (t->pixels - px0 >= 256) {
                double ns = ((t3 - t2) - (t1 - t0)) / (double)(t->pixels - px0);
                if (ns < 0.2) ns = 0.2;
                if (ns > 200) ns = 200;
                r->core_px_ns += (ns - r->core_px_ns) * 0.25;
            }
            ogpu3d_rec_cpu_wrote(r);
            r->stats.cpu++;
            return 1;
        }
    }
    if (r->cmd_pixels) r->committed = 1;
    r->submit_pixels += r->cmd_pixels;
    r->stats.pixels += (unsigned long)r->cmd_pixels;
    r->stats.gpu++;
    return 1;
}

/* ---- setting up ---------------------------------------------------------------------- */

struct ogpu3d_rec *ogpu3d_rec_create(const struct ogpu3d_rec_cfg *cfg) {
    struct ogpu3d_rec *r = calloc(1, sizeof *r);
    const char *m = getenv("OGPU_VK_3D");
    if (!r) return 0;
    r->cfg = *cfg;
    r->mode = m && !strcmp(m, "gpu") ? O3R_MODE_GPU : m && !strcmp(m, "core") ? O3R_MODE_CORE : O3R_MODE_AUTO;
    r->core_px_ns = O3R_CORE_PX_NS;
    r->gpu_px_ns = O3R_GPU_PX_NS;
    r->bcap = 4096;
    r->bands = malloc((size_t)r->bcap * sizeof *r->bands);
    r->spcap = 4096;
    r->spans = malloc((size_t)r->spcap * sizeof *r->spans);
    if (!r->bands || !r->spans) { ogpu3d_rec_destroy(r); return 0; }
    o3r_forget(r);
    o3r_tex_reset(r);
    return r;
}

void ogpu3d_rec_destroy(struct ogpu3d_rec *r) {
    if (!r) return;
    free(r->bands);
    free(r->spans);
    free(r->scratch);
    free(r);
}

void ogpu3d_rec_stats(const struct ogpu3d_rec *r, struct ogpu3d_rec_stats *s) {
    *s = r->stats;
    s->core_px_ns = r->core_px_ns;
    s->gpu_px_ns = r->gpu_px_ns;
    s->round_trip_ns = r->cfg.round_trip_ns;
}
