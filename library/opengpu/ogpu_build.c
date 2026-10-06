/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Building OGPU stream v1 batches (include/opengpu/build.h). Words are
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
