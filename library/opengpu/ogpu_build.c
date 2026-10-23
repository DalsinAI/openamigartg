/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Building OGPU stream v1 batches (and v1.6's paths and gradients) (include/opengpu/build.h). Words are
 * written byte by byte, high byte first, so a batch built on any CPU is the
 * same bytes. */
#include "../../include/opengpu/build.h"

void ogpu_batch_init(struct OGPUBatch *b, void *buf, long cap_words) {
    b->buf = buf; b->cap = cap_words; b->words = 0; b->overflow = 0;
}

static void put(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

/* One command: its opcode and the words after the header. */
static void cmd(struct OGPUBatch *b, int op, int n, const unsigned long *args) {
    unsigned char *p;
    int i;
    if (b->overflow || b->words + 1 + n > b->cap) { b->overflow = 1; return; }
    p = b->buf + b->words * 4;
    put(p, OGPU_HDR(op, n + 1));
    for (i = 0; i < n; i++) put(p + 4 + i * 4, args[i]);
    b->words += 1 + n;
}

void ogpu_surface(struct OGPUBatch *b, int slot, unsigned long address, unsigned long bpr, int w, int h, int format) {
    unsigned long a[5];
    a[0] = (unsigned long)slot; a[1] = address; a[2] = bpr; a[3] = OGPU_XY(w, h); a[4] = (unsigned long)format;
    cmd(b, OGPU_OP_SURFACE, 5, a);
}

void ogpu_target(struct OGPUBatch *b, int slot) {
    unsigned long a[1];
    a[0] = (unsigned long)slot;
    cmd(b, OGPU_OP_TARGET, 1, a);
}

void ogpu_clip(struct OGPUBatch *b, int x, int y, int w, int h) {
    unsigned long a[2];
    a[0] = OGPU_XY(x, y); a[1] = OGPU_XY(w, h);
    cmd(b, OGPU_OP_CLIP, 2, a);
}

void ogpu_fill(struct OGPUBatch *b, int x, int y, int w, int h, unsigned long colour) {
    unsigned long a[3];
    a[0] = OGPU_XY(x, y); a[1] = OGPU_XY(w, h); a[2] = colour;
    cmd(b, OGPU_OP_FILL, 3, a);
}

void ogpu_invert(struct OGPUBatch *b, int x, int y, int w, int h, unsigned long mask) {
    unsigned long a[3];
    a[0] = OGPU_XY(x, y); a[1] = OGPU_XY(w, h); a[2] = mask;
    cmd(b, OGPU_OP_INVERT, 3, a);
}

void ogpu_copy(struct OGPUBatch *b, int src_slot, int sx, int sy, int x, int y, int w, int h) {
    unsigned long a[4];
    a[0] = (unsigned long)src_slot; a[1] = OGPU_XY(sx, sy); a[2] = OGPU_XY(x, y); a[3] = OGPU_XY(w, h);
    cmd(b, OGPU_OP_COPY, 4, a);
}

void ogpu_template(struct OGPUBatch *b, unsigned long address, unsigned long bpr, unsigned long first_bit,
                   int x, int y, int w, int h, unsigned long fg, unsigned long bg, int mode) {
    unsigned long a[8];
    a[0] = address; a[1] = bpr; a[2] = first_bit; a[3] = OGPU_XY(x, y); a[4] = OGPU_XY(w, h);
    a[5] = fg; a[6] = bg; a[7] = (unsigned long)mode;
    cmd(b, OGPU_OP_TEMPLATE, 8, a);
}

void ogpu_pattern(struct OGPUBatch *b, unsigned long address, unsigned long rows,
                  int x, int y, int w, int h, unsigned long fg, unsigned long bg, int mode) {
    unsigned long a[7];
    a[0] = address; a[1] = rows; a[2] = OGPU_XY(x, y); a[3] = OGPU_XY(w, h);
    a[4] = fg; a[5] = bg; a[6] = (unsigned long)mode;
    cmd(b, OGPU_OP_PATTERN, 7, a);
}

void ogpu_line(struct OGPUBatch *b, int x0, int y0, int x1, int y1, unsigned long fg, int mode) {
    unsigned long a[4];
    a[0] = OGPU_XY(x0, y0); a[1] = OGPU_XY(x1, y1); a[2] = fg; a[3] = (unsigned long)mode;
    cmd(b, OGPU_OP_LINE, 4, a);
}

void ogpu_pixels(struct OGPUBatch *b, unsigned long address, unsigned long bpr, int format, unsigned long table,
                 int x, int y, int w, int h) {
    unsigned long a[6];
    a[0] = address; a[1] = bpr; a[2] = (unsigned long)format; a[3] = table; a[4] = OGPU_XY(x, y); a[5] = OGPU_XY(w, h);
    cmd(b, OGPU_OP_PIXELS, 6, a);
}

void ogpu_composite(struct OGPUBatch *b, int src_slot, int sx, int sy, int sw, int sh,
                    int x, int y, int w, int h, int alpha, unsigned long flags) {
    unsigned long a[7];
    a[0] = (unsigned long)src_slot; a[1] = OGPU_XY(sx, sy); a[2] = OGPU_XY(sw, sh);
    a[3] = OGPU_XY(x, y); a[4] = OGPU_XY(w, h); a[5] = (unsigned long)alpha & 255; a[6] = flags;
    cmd(b, OGPU_OP_COMPOSITE, 7, a);
}

void ogpu_composite_masked(struct OGPUBatch *b, int src_slot, int sx, int sy, int sw, int sh,
                           int x, int y, int w, int h, int alpha, unsigned long flags,
                           int mask_slot, int mx, int my) {
    unsigned long a[9];
    a[0] = (unsigned long)src_slot; a[1] = OGPU_XY(sx, sy); a[2] = OGPU_XY(sw, sh);
    a[3] = OGPU_XY(x, y); a[4] = OGPU_XY(w, h); a[5] = (unsigned long)alpha & 255; a[6] = flags | OGPU_COMP_MASK;
    a[7] = (unsigned long)mask_slot; a[8] = OGPU_XY(mx, my);
    cmd(b, OGPU_OP_COMPOSITE, 9, a);
}

void ogpu_mask(struct OGPUBatch *b, unsigned long address, unsigned long bpr, int x, int y, int w, int h,
               unsigned long colour) {
    unsigned long a[5];
    a[0] = address; a[1] = bpr; a[2] = OGPU_XY(x, y); a[3] = OGPU_XY(w, h); a[4] = colour;
    cmd(b, OGPU_OP_MASK, 5, a);
}

void ogpu_fence(struct OGPUBatch *b, unsigned long id) {
    unsigned long a[1];
    a[0] = id;
    cmd(b, OGPU_OP_FENCE, 1, a);
}

void ogpu_virgl(struct OGPUBatch *b, unsigned long address, unsigned long bytes) {
    unsigned long a[2];
    a[0] = address;
    a[1] = bytes;
    cmd(b, OGPU_OP_VIRGL, 2, a);
}

/* ---- v1.2 ---- */

void ogpu_fill_blend(struct OGPUBatch *b, int x, int y, int w, int h, unsigned long argb, int mode) {
    unsigned long a[4];
    a[0] = OGPU_XY(x, y); a[1] = OGPU_XY(w, h); a[2] = argb; a[3] = (unsigned long)mode;
    cmd(b, OGPU_OP_FILL_BLEND, 4, a);
}

void ogpu_lines_blend(struct OGPUBatch *b, unsigned long points, unsigned long count, unsigned long argb, unsigned long flags) {
    unsigned long a[4];
    a[0] = points; a[1] = count; a[2] = argb; a[3] = flags;
    cmd(b, OGPU_OP_LINES_BLEND, 4, a);
}

void ogpu_points_blend(struct OGPUBatch *b, unsigned long points, unsigned long count, unsigned long argb, int mode) {
    unsigned long a[4];
    a[0] = points; a[1] = count; a[2] = argb; a[3] = (unsigned long)mode;
    cmd(b, OGPU_OP_POINTS_BLEND, 4, a);
}

void ogpu_composite_affine(struct OGPUBatch *b, int src_slot, int sx, int sy, int sw, int sh,
                           const long m[6], unsigned long argb, unsigned long flags) {
    unsigned long a[11];
    int i;
    a[0] = (unsigned long)src_slot; a[1] = OGPU_XY(sx, sy); a[2] = OGPU_XY(sw, sh);
    for (i = 0; i < 6; i++) a[3 + i] = (unsigned long)m[i] & 0xFFFFFFFFUL;
    a[9] = argb; a[10] = flags;
    cmd(b, OGPU_OP_COMPOSITE_AFFINE, 11, a);
}

void ogpu_composite_pd(struct OGPUBatch *b, unsigned long op_flags, unsigned long src, int sx, int sy,
                       unsigned long mask, int mx, int my, int x, int y, int w, int h) {
    unsigned long a[7];
    a[0] = op_flags; a[1] = src; a[2] = OGPU_XY(sx, sy); a[3] = mask; a[4] = OGPU_XY(mx, my);
    a[5] = OGPU_XY(x, y); a[6] = OGPU_XY(w, h);
    cmd(b, OGPU_OP_COMPOSITE_PD, 7, a);
}

void ogpu_yuv(struct OGPUBatch *b, unsigned long y_address, unsigned long y_bpr, unsigned long u_address, unsigned long u_bpr,
              unsigned long v_address, unsigned long v_bpr, int w, int h, int x, int y, unsigned long format) {
    unsigned long a[9];
    a[0] = y_address; a[1] = y_bpr; a[2] = u_address; a[3] = u_bpr; a[4] = v_address; a[5] = v_bpr;
    a[6] = OGPU_XY(w, h); a[7] = OGPU_XY(x, y); a[8] = format;
    cmd(b, OGPU_OP_YUV, 9, a);
}

void ogpu_yuv_scaled(struct OGPUBatch *b, unsigned long y_address, unsigned long y_bpr, unsigned long u_address,
                     unsigned long u_bpr, unsigned long v_address, unsigned long v_bpr, int w, int h,
                     unsigned long format, int sx, int sy, int sw, int sh, int x, int y, int dw, int dh) {
    unsigned long a[12];
    a[0] = y_address; a[1] = y_bpr; a[2] = u_address; a[3] = u_bpr; a[4] = v_address; a[5] = v_bpr;
    a[6] = OGPU_XY(w, h); a[7] = format; a[8] = OGPU_XY(sx, sy); a[9] = OGPU_XY(sw, sh);
    a[10] = OGPU_XY(x, y); a[11] = OGPU_XY(dw, dh);
    cmd(b, OGPU_OP_YUV_SCALED, 12, a);
}

/* v1.6: paths, gradients and blur. Each command and record is plain words,
 * so cmd() writes them all; a path or a gradient record is written with
 * cmd()'s words and no header. */
static void words(struct OGPUBatch *b, int n, const unsigned long *w) {
    int i;
    if (b->overflow || b->words + n > b->cap) { b->overflow = 1; return; }
    for (i = 0; i < n; i++) put(b->buf + (b->words + i) * 4, w[i]);
    b->words += n;
}

static void path_args(unsigned long *a, unsigned long flags, unsigned long paint, int px, int py, int mask_slot, int mx, int my,
                      unsigned long path, unsigned long path_words, const long m[6], unsigned long work, unsigned long work_bytes) {
    int i;
    a[0] = flags; a[1] = paint; a[2] = OGPU_XY(px, py); a[3] = (unsigned long)mask_slot; a[4] = OGPU_XY(mx, my);
    a[5] = path; a[6] = path_words;
    for (i = 0; i < 6; i++) a[7 + i] = (unsigned long)m[i] & 0xFFFFFFFFUL;
    a[13] = work; a[14] = work_bytes;
}

void ogpu_path_fill(struct OGPUBatch *b, unsigned long flags, unsigned long paint, int px, int py,
                    int mask_slot, int mx, int my, unsigned long path, unsigned long path_words, const long m[6],
                    unsigned long work, unsigned long work_bytes) {
    unsigned long a[15];
    path_args(a, flags, paint, px, py, mask_slot, mx, my, path, path_words, m, work, work_bytes);
    cmd(b, OGPU_OP_PATH_FILL, 15, a);
}

void ogpu_path_stroke(struct OGPUBatch *b, unsigned long flags, unsigned long paint, int px, int py,
                      int mask_slot, int mx, int my, unsigned long path, unsigned long path_words, const long m[6],
                      unsigned long work, unsigned long work_bytes, long width, unsigned long style, long miter,
                      unsigned long dash, unsigned long dash_count, long dash_offset) {
    unsigned long a[21];
    path_args(a, flags, paint, px, py, mask_slot, mx, my, path, path_words, m, work, work_bytes);
    a[15] = (unsigned long)width & 0xFFFFFFFFUL; a[16] = style; a[17] = (unsigned long)miter & 0xFFFFFFFFUL;
    a[18] = dash; a[19] = dash_count; a[20] = (unsigned long)dash_offset & 0xFFFFFFFFUL;
    cmd(b, OGPU_OP_PATH_STROKE, 21, a);
}

void ogpu_blur(struct OGPUBatch *b, int x, int y, int w, int h, int rx, int ry, unsigned long work,
               unsigned long work_bytes) {
    unsigned long a[5];
    a[0] = OGPU_XY(x, y); a[1] = OGPU_XY(w, h); a[2] = OGPU_XY(rx, ry); a[3] = work; a[4] = work_bytes;
    cmd(b, OGPU_OP_BLUR, 5, a);
}

void ogpu_palette(struct OGPUBatch *b, int slot, unsigned long table) {
    unsigned long a[2];
    a[0] = (unsigned long)slot; a[1] = table;
    cmd(b, OGPU_OP_PALETTE, 2, a);
}

static void path_el(struct OGPUBatch *p, int verb, int n, const long *xy) {
    unsigned long w[7];
    int i;
    w[0] = (unsigned long)verb;
    for (i = 0; i < 2 * n; i++) w[1 + i] = (unsigned long)xy[i] & 0xFFFFFFFFUL;
    words(p, 1 + 2 * n, w);
}
void ogpu_path_move(struct OGPUBatch *p, long x, long y) { long v[2]; v[0] = x; v[1] = y; path_el(p, OGPU_PATH_MOVE, 1, v); }
void ogpu_path_line(struct OGPUBatch *p, long x, long y) { long v[2]; v[0] = x; v[1] = y; path_el(p, OGPU_PATH_LINE, 1, v); }
void ogpu_path_quad(struct OGPUBatch *p, long x1, long y1, long x, long y) {
    long v[4];
    v[0] = x1; v[1] = y1; v[2] = x; v[3] = y;
    path_el(p, OGPU_PATH_QUAD, 2, v);
}
void ogpu_path_cubic(struct OGPUBatch *p, long x1, long y1, long x2, long y2, long x, long y) {
    long v[6];
    v[0] = x1; v[1] = y1; v[2] = x2; v[3] = y2; v[4] = x; v[5] = y;
    path_el(p, OGPU_PATH_CUBIC, 3, v);
}
void ogpu_path_close(struct OGPUBatch *p) { path_el(p, OGPU_PATH_CLOSE, 0, 0); }

void ogpu_gradient(struct OGPUBatch *g, int kind, int spread, int n, const long m[6], const long geometry[6]) {
    unsigned long w[14];
    int i;
    w[0] = (unsigned long)kind | ((unsigned long)spread << 8); w[1] = (unsigned long)n;
    for (i = 0; i < 6; i++) { w[2 + i] = (unsigned long)m[i] & 0xFFFFFFFFUL; w[8 + i] = (unsigned long)geometry[i] & 0xFFFFFFFFUL; }
    words(g, 14, w);
}

void ogpu_gradient_stop(struct OGPUBatch *g, long offset, unsigned long argb) {
    unsigned long w[2];
    w[0] = (unsigned long)offset & 0xFFFFFFFFUL; w[1] = argb;
    words(g, 2, w);
}

void ogpu_image_paint(struct OGPUBatch *g, int slot, unsigned long flags, int sx, int sy, int sw, int sh,
                      const long m[6], int alpha) {
    unsigned long w[OGPU_PATTERN_WORDS];
    int i;
    w[0] = (unsigned long)slot; w[1] = flags; w[2] = OGPU_XY(sx, sy); w[3] = OGPU_XY(sw, sh);
    for (i = 0; i < 6; i++) w[4 + i] = (unsigned long)m[i] & 0xFFFFFFFFUL;
    w[10] = (unsigned long)alpha & 255;
    words(g, OGPU_PATTERN_WORDS, w);
}
