/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's core (G1, DESIGN.md section 5): one rasterizer for every back
 * end. The 68k runs it as the CPU back end, the Cradle's runtime runs it
 * behind ACRTG.gpu, and a PiStorm's spare ARM core runs it behind
 * PiStorm.gpu; tests/test_opengpu.c checks it against simple per-pixel
 * references and keeps a golden checksum every build must match.
 *
 * Integers only, no C library, pixels read and written byte by byte in the
 * Amiga's order, so the result is the same on a big- or little-endian CPU.
 */
#include "ogpu_core.h"

/* ---- small helpers ---------------------------------------------------------------- */

static ogpu_u32 rd32(const ogpu_u8 *p) {
    return ((ogpu_u32)p[0] << 24) | ((ogpu_u32)p[1] << 16) | ((ogpu_u32)p[2] << 8) | (ogpu_u32)p[3];
}

static int s16(ogpu_u32 v) { v &= 0xFFFFUL; return v >= 0x8000UL ? (int)v - 0x10000 : (int)v; }
static int u16(ogpu_u32 v) { return (int)(v & 0xFFFFUL); }
static int hi_s(ogpu_u32 w) { return s16(w >> 16); }
static int lo_s(ogpu_u32 w) { return s16(w); }
static int hi_u(ogpu_u32 w) { return u16(w >> 16); }
static int lo_u(ogpu_u32 w) { return u16(w); }

static int bytes_per_pixel(int format) {
    switch (format) {
    case OGPU_FMT_CLUT8: case OGPU_FMT_INDEX8: case OGPU_FMT_A8: return 1;
    case OGPU_FMT_RGB565: return 2;
    case OGPU_FMT_ARGB32: return 4;
    }
    return 0;
}

static ogpu_u32 get_px(const ogpu_u8 *p, int format) {
    switch (format) {
    case OGPU_FMT_RGB565: return ((ogpu_u32)p[0] << 8) | p[1];
    case OGPU_FMT_ARGB32: return rd32(p);
    }
    return p[0];
}

static void put_px(ogpu_u8 *p, int format, ogpu_u32 v) {
    switch (format) {
    case OGPU_FMT_RGB565: p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; return;
    case OGPU_FMT_ARGB32:
        p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
        return;
    }
    p[0] = (ogpu_u8)v;
}

/* What COMPLEMENT flips: every bit of a pen or an RGB565 pixel, and the
 * colour (not the alpha) of an ARGB32 one. */
static ogpu_u32 flip_mask(int format) {
    switch (format) {
    case OGPU_FMT_RGB565: return 0xFFFFUL;
    case OGPU_FMT_ARGB32: return 0x00FFFFFFUL;
    }
    return 0xFFUL;
}

static ogpu_u32 to_argb(ogpu_u32 v, int format) {
    if (format == OGPU_FMT_A8) return (v & 255) << 24;     /* coverage only: black */
    if (format == OGPU_FMT_RGB565) {
        ogpu_u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        return 0xFF000000UL | (r << 16) | (g << 8) | b;
    }
    return v & 0xFFFFFFFFUL;
}

static ogpu_u32 from_argb(ogpu_u32 v, int format) {
    if (format == OGPU_FMT_A8) return (v >> 24) & 255;
    if (format == OGPU_FMT_RGB565)
        return (((v >> 19) & 31) << 11) | (((v >> 10) & 63) << 5) | ((v >> 3) & 31);
    return v & 0xFFFFFFFFUL;
}

/* x * y / 255, rounded, for x and y in 0..255. */
static ogpu_u32 mul255(ogpu_u32 x, ogpu_u32 y) {
    ogpu_u32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

static ogpu_u8 *px_at(const struct ogpu_surface *s, int x, int y) {
    return s->pixels + (long)y * s->bpr + (long)x * bytes_per_pixel(s->format);
}

/* The bytes h rows of `last` bytes take at bpr apart, in *len; 0 when that
 * passes 32 bits (as it can on the 68k), which no Amiga memory holds. */
static int span_len(ogpu_u32 bpr, int h, ogpu_u32 last, ogpu_u32 *len) {
    ogpu_u32 rows = (ogpu_u32)(h - 1);
    if (rows && bpr > (0xFFFFFFFFUL - last) / rows) return 0;
    *len = bpr * rows + last;
    return 1;
}

/* Clip a rectangle on the target to the target and the clip rectangle.
 * Returns 0 when nothing is left; otherwise *ox, *oy say how far its corner
 * moved, so a source can move with it. */
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

static int in_clip(const struct ogpu_core *c, int x, int y) {
    const struct ogpu_surface *t = &c->slot[c->target];
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) return 0;
    if (c->cx1 > c->cx0 && (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1)) return 0;
    return 1;
}

/* One pixel of a 1-bit source drawn by a draw mode (TEMPLATE, PATTERN, LINE). */
static void plot_bit(ogpu_u8 *p, int format, int bit, ogpu_u32 fg, ogpu_u32 bg, int mode) {
    if (mode & OGPU_INVERSVID) bit = !bit;
    if (mode & OGPU_COMPLEMENT) {
        if (bit) put_px(p, format, get_px(p, format) ^ flip_mask(format));
    } else if (bit) {
        put_px(p, format, fg);
    } else if (mode & OGPU_JAM2) {
        put_px(p, format, bg);
    }
}

/* ---- the commands ----------------------------------------------------------------- */

static int op_surface(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), address = rd32(a + 4), bpr = rd32(a + 8), wh = rd32(a + 12), format = rd32(a + 16);
    int w = hi_u(wh), h = lo_u(wh), bpp = bytes_per_pixel((int)format);
    struct ogpu_surface *s;
    ogpu_u8 *mem;
    ogpu_u32 len;
    if (slot >= OGPU_MAX_SLOTS) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    s->pixels = 0;
    if (!bpp || format == OGPU_FMT_INDEX8 || !w || !h || w > OGPU_MAX_SIZE || h > OGPU_MAX_SIZE
        || bpr < (ogpu_u32)w * bpp || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w * bpp, &len))
        return OGPU_ERR_UNSUPPORTED;
    mem = c->map(c->user, address, len);
    if (!mem) return OGPU_ERR_NOMAP;
    s->pixels = mem; s->bpr = (long)bpr; s->w = w; s->h = h; s->format = (int)format;
    if (c->target == (int)slot) { c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; }
    return OGPU_OK;
}

static int op_fill(struct ogpu_core *c, const ogpu_u8 *a, int invert) {
    ogpu_u32 xy = rd32(a), wh = rd32(a + 4), v = rd32(a + 8);
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    const struct ogpu_surface *t = &c->slot[c->target];
    int bpp = bytes_per_pixel(t->format);
    if (invert) v &= t->format == OGPU_FMT_ARGB32 ? 0xFFFFFFFFUL : flip_mask(t->format);
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        ogpu_u8 *p = px_at(t, x, y + j);
        for (i = 0; i < w; i++, p += bpp)
            put_px(p, t->format, invert ? get_px(p, t->format) ^ v : v);
    }
    return OGPU_OK;
}

static int op_copy(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), dxy = rd32(a + 8), wh = rd32(a + 12);
    const struct ogpu_surface *t = &c->slot[c->target], *s;
    int sx = hi_s(sxy), sy = lo_s(sxy), x = hi_s(dxy), y = lo_s(dxy), w = hi_u(wh), h = lo_u(wh);
    int ox, oy, bpp, row, j, n;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    if (s->format != t->format) return OGPU_ERR_UNSUPPORTED;
    /* Keep the source inside its surface, moving the destination with it. */
    if (sx < 0) { x -= sx; w += sx; sx = 0; }
    if (sy < 0) { y -= sy; h += sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return OGPU_OK;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    sx += ox; sy += oy;
    bpp = bytes_per_pixel(t->format);
    n = w * bpp;
    for (row = 0; row < h; row++) {
        /* Overlap (one surface, or two slots on the same memory): rows bottom
         * up when the destination is later in memory, bytes backwards when
         * a row's destination starts inside its source. */
        int r = px_at(t, x, y) > px_at(s, sx, sy) ? h - 1 - row : row;
        const ogpu_u8 *sp = px_at(s, sx, sy + r);
        ogpu_u8 *dp = px_at(t, x, y + r);
        if (dp > sp && dp < sp + n) { for (j = n - 1; j >= 0; j--) dp[j] = sp[j]; }
        else { for (j = 0; j < n; j++) dp[j] = sp[j]; }
    }
    return OGPU_OK;
}

static int op_template(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), first = rd32(a + 8), xy = rd32(a + 12), wh = rd32(a + 16);
    ogpu_u32 fg = rd32(a + 20), bg = rd32(a + 24);
    int mode = (int)rd32(a + 28);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, bpp = bytes_per_pixel(t->format);
    const ogpu_u8 *src;
    if (!w || !h) return OGPU_OK;
    if (first > 0xFFFFUL || bpr > 0xFFFFUL) return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, bpr * (ogpu_u32)(h - 1) + (first + (ogpu_u32)w + 7) / 8);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        const ogpu_u8 *row = src + (long)(oy + j) * (long)bpr;
        ogpu_u8 *p = px_at(t, x, y + j);
        for (i = 0; i < w; i++, p += bpp) {
            ogpu_u32 bit = first + (ogpu_u32)(ox + i);
            plot_bit(p, t->format, (row[bit >> 3] >> (7 - (bit & 7))) & 1, fg, bg, mode);
        }
    }
    return OGPU_OK;
}

static int op_pattern(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), rows = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12);
    ogpu_u32 fg = rd32(a + 16), bg = rd32(a + 20);
    int mode = (int)rd32(a + 24);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, bpp = bytes_per_pixel(t->format);
    const ogpu_u8 *pat;
    if (!rows || rows > 256 || (rows & (rows - 1))) return OGPU_ERR_UNSUPPORTED;
    pat = c->map(c->user, address, rows * 2);
    if (!pat) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        const ogpu_u8 *pw = pat + (((ogpu_u32)(y + j) & (rows - 1)) << 1);
        ogpu_u32 bits = ((ogpu_u32)pw[0] << 8) | pw[1];
        ogpu_u8 *p = px_at(t, x, y + j);
        for (i = 0; i < w; i++, p += bpp)
            plot_bit(p, t->format, (int)((bits >> (15 - ((x + i) & 15))) & 1), fg, bg, mode);
    }
    return OGPU_OK;
}

static int op_line(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 p0 = rd32(a), p1 = rd32(a + 4), fg = rd32(a + 8);
    int mode = (int)rd32(a + 12) & ~OGPU_INVERSVID;
    const struct ogpu_surface *t = &c->slot[c->target];
    int x0 = hi_s(p0), y0 = lo_s(p0), x1 = hi_s(p1), y1 = lo_s(p1);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int stepx = x0 < x1 ? 1 : -1, stepy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        if (in_clip(c, x0, y0)) plot_bit(px_at(t, x0, y0), t->format, 1, fg, 0, mode);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += stepx; }
            if (e2 < dx) { err += dx; y0 += stepy; }
        }
    }
    return OGPU_OK;
}

static int op_pixels(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), format = rd32(a + 8), table = rd32(a + 12);
    ogpu_u32 xy = rd32(a + 16), wh = rd32(a + 20);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j;
    int sbpp = bytes_per_pixel((int)format), dbpp = bytes_per_pixel(t->format);
    const ogpu_u8 *src, *ctab = 0;
    ogpu_u32 len;
    int raw;
    if (!sbpp || !w || !h) return sbpp ? OGPU_OK : OGPU_ERR_UNSUPPORTED;
    /* A CLUT8 target takes pens only; true-colour targets take anything
     * but bare pens, which have no colours without a table. */
    if (t->format == OGPU_FMT_CLUT8) {
        if (format != OGPU_FMT_CLUT8 && !(format == OGPU_FMT_INDEX8 && !table)) return OGPU_ERR_UNSUPPORTED;
        raw = 1;
    } else {
        if (format == OGPU_FMT_CLUT8) return OGPU_ERR_UNSUPPORTED;
        /* A8 goes only to A8: coverage has no colour, and colours no coverage. */
        if ((format == OGPU_FMT_A8) != (t->format == OGPU_FMT_A8)) return OGPU_ERR_UNSUPPORTED;
        raw = (int)format == t->format;
        if (format == OGPU_FMT_INDEX8) {
            if (!table) return OGPU_ERR_UNSUPPORTED;
            ctab = c->map(c->user, table, 1024);
            if (!ctab) return OGPU_ERR_NOMAP;
        }
    }
    if (bpr < (ogpu_u32)w * sbpp || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w * sbpp, &len))
        return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, len);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        const ogpu_u8 *sp = src + (long)(oy + j) * (long)bpr + (long)ox * sbpp;
        ogpu_u8 *dp = px_at(t, x, y + j);
        if (raw) {
            for (i = 0; i < w * dbpp; i++) dp[i] = sp[i];
            continue;
        }
        for (i = 0; i < w; i++, sp += sbpp, dp += dbpp) {
            ogpu_u32 argb = ctab ? rd32(ctab + ((ogpu_u32)sp[0] << 2)) : to_argb(get_px(sp, (int)format), (int)format);
            put_px(dp, t->format, from_argb(argb, t->format));
        }
    }
    return OGPU_OK;
}

/* One channel (shift 24, 16, 8 or 0) of a and b mixed by f/256. */
static ogpu_u32 mix(ogpu_u32 a, ogpu_u32 b, ogpu_u32 f) {
    ogpu_u32 r = 0;
    int s;
    for (s = 0; s < 32; s += 8) {
        ogpu_u32 ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= ((ca * (256 - f) + cb * f) >> 8) << s;
    }
    return r;
}

static ogpu_u32 sample(const struct ogpu_surface *s, int x, int y) {
    return to_argb(get_px(px_at(s, x, y), s->format), s->format);
}

/* Put source pixel sp (ARGB) on dp with coverage ea, by the COMPOSITE
 * operator in flags: OVER, ADD or IN. */
static void blend(ogpu_u8 *dp, int format, ogpu_u32 sp, ogpu_u32 ea, ogpu_u32 flags) {
    ogpu_u32 d, r, g, b, oa;
    if (flags & OGPU_COMP_IN) {
        d = to_argb(get_px(dp, format), format);
        r = mul255((d >> 16) & 255, ea); g = mul255((d >> 8) & 255, ea); b = mul255(d & 255, ea);
        oa = mul255((d >> 24) & 255, ea);
    } else if (flags & OGPU_COMP_ADD) {
        if (!ea) return;
        d = to_argb(get_px(dp, format), format);
        r = ((d >> 16) & 255) + mul255((sp >> 16) & 255, ea);
        g = ((d >> 8) & 255) + mul255((sp >> 8) & 255, ea);
        b = (d & 255) + mul255(sp & 255, ea);
        oa = ((d >> 24) & 255) + ea;
    } else {
        if (ea == 255) {
            put_px(dp, format, from_argb(sp | 0xFF000000UL, format));
            return;
        }
        if (!ea) return;
        d = to_argb(get_px(dp, format), format);
        r = mul255((sp >> 16) & 255, ea) + mul255((d >> 16) & 255, 255 - ea);
        g = mul255((sp >> 8) & 255, ea) + mul255((d >> 8) & 255, 255 - ea);
        b = mul255(sp & 255, ea) + mul255(d & 255, 255 - ea);
        oa = ea + mul255((d >> 24) & 255, 255 - ea);
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (oa > 255) oa = 255;
    put_px(dp, format, from_argb((oa << 24) | (r << 16) | (g << 8) | b, format));
}

static int op_composite(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 slot = rd32(a), sxy = rd32(a + 4), swh = rd32(a + 8), dxy = rd32(a + 12), dwh = rd32(a + 16);
    ogpu_u32 alpha = rd32(a + 20) & 255, flags = rd32(a + 24);
    const struct ogpu_surface *t = &c->slot[c->target], *s, *m = 0;
    int sx = hi_s(sxy), sy = lo_s(sxy), sw = hi_u(swh), sh = lo_u(swh);
    int x = hi_s(dxy), y = lo_s(dxy), w = hi_u(dwh), h = lo_u(dwh), ox, oy, i, j, dbpp, mx = 0, my = 0;
    ogpu_u32 stepx, stepy;
    if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) return OGPU_ERR_NOSURFACE;
    s = &c->slot[slot];
    if (t->format == OGPU_FMT_CLUT8 || s->format == OGPU_FMT_CLUT8) return OGPU_ERR_UNSUPPORTED;
    if ((flags & OGPU_COMP_ADD) && (flags & OGPU_COMP_IN)) return OGPU_ERR_UNSUPPORTED;
    if (flags & OGPU_COMP_MASK) {
        ogpu_u32 mslot = rd32(a + 28), mxy = rd32(a + 32);
        if (mslot >= OGPU_MAX_SLOTS || !c->slot[mslot].pixels) return OGPU_ERR_NOSURFACE;
        m = &c->slot[mslot];
        if (m->format != OGPU_FMT_A8) return OGPU_ERR_UNSUPPORTED;
        mx = hi_s(mxy); my = lo_s(mxy);
        if (mx < 0 || my < 0 || mx + w > m->w || my + h > m->h) return OGPU_ERR_UNSUPPORTED;
    }
    if (!sw || !sh || !w || !h) return OGPU_OK;
    if (sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h) return OGPU_ERR_UNSUPPORTED;
    stepx = ((ogpu_u32)sw << 16) / (ogpu_u32)w;
    stepy = ((ogpu_u32)sh << 16) / (ogpu_u32)h;
    dbpp = bytes_per_pixel(t->format);
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        ogpu_u32 py = (ogpu_u32)(oy + j) * stepy + (stepy >> 1);    /* 16.16, the pixel's centre */
        ogpu_u8 *dp = px_at(t, x, y + j);
        const ogpu_u8 *mp = m ? px_at(m, mx + ox, my + oy + j) : 0;
        for (i = 0; i < w; i++, dp += dbpp) {
            ogpu_u32 px = (ogpu_u32)(ox + i) * stepx + (stepx >> 1), sp, ea;
            if (flags & OGPU_COMP_BILINEAR) {
                long fx = (long)px - 0x8000L, fy = (long)py - 0x8000L;
                int x0, y0, x1, y1;
                ogpu_u32 wx, wy;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                x0 = (int)(fx >> 16); y0 = (int)(fy >> 16);
                wx = (ogpu_u32)(fx >> 8) & 255; wy = (ogpu_u32)(fy >> 8) & 255;
                if (x0 >= sw - 1) { x0 = sw - 1; wx = 0; }
                if (y0 >= sh - 1) { y0 = sh - 1; wy = 0; }
                x1 = x0 + 1 < sw ? x0 + 1 : x0;
                y1 = y0 + 1 < sh ? y0 + 1 : y0;
                sp = mix(mix(sample(s, sx + x0, sy + y0), sample(s, sx + x1, sy + y0), wx),
                         mix(sample(s, sx + x0, sy + y1), sample(s, sx + x1, sy + y1), wx), wy);
            } else {
                int nx = (int)(px >> 16), ny = (int)(py >> 16);
                if (nx >= sw) nx = sw - 1;
                if (ny >= sh) ny = sh - 1;
                sp = sample(s, sx + nx, sy + ny);
            }
            ea = mul255((flags & OGPU_COMP_SRCALPHA) ? (sp >> 24) & 255 : 255, alpha);
            if (mp) ea = mul255(ea, mp[i]);
            blend(dp, t->format, sp, ea, flags);
        }
    }
    return OGPU_OK;
}

/* MASK (v1.1): one ARGB32 colour drawn OVER the target through an A8 mask
 * in memory, as anti-aliased glyphs are. */
static int op_mask(struct ogpu_core *c, const ogpu_u8 *a) {
    ogpu_u32 address = rd32(a), bpr = rd32(a + 4), xy = rd32(a + 8), wh = rd32(a + 12), colour = rd32(a + 16);
    const struct ogpu_surface *t = &c->slot[c->target];
    int x = hi_s(xy), y = lo_s(xy), w = hi_u(wh), h = lo_u(wh), ox, oy, i, j, dbpp = bytes_per_pixel(t->format);
    ogpu_u32 ca = (colour >> 24) & 255, len;
    const ogpu_u8 *src;
    if (t->format == OGPU_FMT_CLUT8) return OGPU_ERR_UNSUPPORTED;
    if (!w || !h) return OGPU_OK;
    if (bpr < (ogpu_u32)w || bpr > 0x01000000UL || !span_len(bpr, h, (ogpu_u32)w, &len)) return OGPU_ERR_UNSUPPORTED;
    src = c->map(c->user, address, len);
    if (!src) return OGPU_ERR_NOMAP;
    if (!clip_rect(c, &x, &y, &w, &h, &ox, &oy)) return OGPU_OK;
    for (j = 0; j < h; j++) {
        const ogpu_u8 *sp = src + (long)(oy + j) * (long)bpr + ox;
        ogpu_u8 *dp = px_at(t, x, y + j);
        for (i = 0; i < w; i++, dp += dbpp)
            blend(dp, t->format, colour, mul255(sp[i], ca), 0);
    }
    return OGPU_OK;
}

/* ---- the run ---------------------------------------------------------------------- */

void ogpu_core_init(struct ogpu_core *c) {
    int i;
    for (i = 0; i < OGPU_MAX_SLOTS; i++) c->slot[i].pixels = 0;
    c->target = -1;
    c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0;
    c->last_error = OGPU_OK;
    c->error_word = 0;
}

/* Words each opcode needs after its header (0: not a drawing command). */
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
    case OGPU_OP_COMPOSITE: return 7;       /* 9 with OGPU_COMP_MASK, checked in the run */
    case OGPU_OP_FENCE: return 1;
    }
    return -1;
}

long ogpu_core_run(struct ogpu_core *c, const ogpu_u8 *stream, long words) {
    long at = 0, done = 0;
    c->last_error = OGPU_OK;
    while (at < words) {
        ogpu_u32 hdr = rd32(stream + at * 4);
        int op = (int)OGPU_HDR_OP(hdr), len = (int)OGPU_HDR_WORDS(hdr), need = args_for(op), r = OGPU_OK;
        const ogpu_u8 *a = stream + at * 4 + 4;
        if (len < 1 || at + len > words) {
            if (c->last_error == OGPU_OK) { c->last_error = OGPU_ERR_BADLEN; c->error_word = at; }
            break;
        }
        if (need >= 0 && (len - 1 < need
                          || (op == OGPU_OP_COMPOSITE && (rd32(a + 24) & OGPU_COMP_MASK) && len - 1 < 9))) {
            /* Too short for its own arguments: what follows can't be trusted either. */
            if (c->last_error == OGPU_OK) { c->last_error = OGPU_ERR_BADLEN; c->error_word = at; }
            break;
        }
        if (need < 0)
            r = op >= OGPU_OP_EXT_FIRST && op <= OGPU_OP_EXT_LAST && c->ext ? c->ext(c->user, op, stream + at * 4, len)
                                                                           : OGPU_ERR_BADOP;
        else if (op >= OGPU_OP_FILL && op < OGPU_OP_FENCE && (c->target < 0 || !c->slot[c->target].pixels))
            r = OGPU_ERR_NOSURFACE;
        else switch (op) {
        case OGPU_OP_SURFACE: r = op_surface(c, a); break;
        case OGPU_OP_TARGET: {
            ogpu_u32 slot = rd32(a);
            if (slot >= OGPU_MAX_SLOTS || !c->slot[slot].pixels) { c->target = -1; r = OGPU_ERR_NOSURFACE; }
            else { c->target = (int)slot; c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; }
            break;
        }
        case OGPU_OP_CLIP: {
            ogpu_u32 xy = rd32(a), wh = rd32(a + 4);
            if (!hi_u(wh) || !lo_u(wh)) { c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0; break; }   /* the whole target */
            c->cx0 = hi_s(xy); c->cy0 = lo_s(xy);
            c->cx1 = c->cx0 + hi_u(wh);
            c->cy1 = c->cy0 + lo_u(wh);
            break;
        }
        case OGPU_OP_FILL: r = op_fill(c, a, 0); break;
        case OGPU_OP_INVERT: r = op_fill(c, a, 1); break;
        case OGPU_OP_COPY: r = op_copy(c, a); break;
        case OGPU_OP_TEMPLATE: r = op_template(c, a); break;
        case OGPU_OP_PATTERN: r = op_pattern(c, a); break;
        case OGPU_OP_LINE: r = op_line(c, a); break;
        case OGPU_OP_PIXELS: r = op_pixels(c, a); break;
        case OGPU_OP_MASK: r = op_mask(c, a); break;
        case OGPU_OP_COMPOSITE: r = op_composite(c, a); break;
        case OGPU_OP_FENCE: if (c->fence) c->fence(c->user, rd32(a)); break;
        }
        if (r != OGPU_OK && c->last_error == OGPU_OK) { c->last_error = r; c->error_word = at; }
        if (op != OGPU_OP_NOP) done++;
        at += len;
    }
    return done;
}

int ogpu_core_supports(int op, int format) {
    if (format != OGPU_FMT_CLUT8 && format != OGPU_FMT_RGB565 && format != OGPU_FMT_ARGB32
        && format != OGPU_FMT_A8) return OGPU_NONE;
    switch (op) {
    case OGPU_OP_NOP: case OGPU_OP_SURFACE: case OGPU_OP_TARGET: case OGPU_OP_CLIP: case OGPU_OP_FENCE:
    case OGPU_OP_FILL: case OGPU_OP_INVERT: case OGPU_OP_COPY: case OGPU_OP_TEMPLATE:
    case OGPU_OP_PATTERN: case OGPU_OP_LINE:
        return OGPU_FULL;
    case OGPU_OP_PIXELS:    /* pens only on CLUT8, coverage only on A8 */
        return format == OGPU_FMT_CLUT8 || format == OGPU_FMT_A8 ? OGPU_PARTIAL : OGPU_FULL;
    case OGPU_OP_COMPOSITE: case OGPU_OP_MASK:
        return format == OGPU_FMT_CLUT8 ? OGPU_NONE : OGPU_FULL;
    }
    return OGPU_NONE;
}
