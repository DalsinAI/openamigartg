/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Building stream v1.2's 3D commands (include/opengpu/build3d.h),
 * byte by byte, high byte first, as ogpu_build.c does. */
#include "../../include/opengpu/build3d.h"

static void put(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void cmd(struct OGPUBatch *b, int op, int n, const unsigned long *args) {
    unsigned char *p;
    int i;
    if (b->overflow || b->words + 1 + n > b->cap) { b->overflow = 1; return; }
    p = b->buf + b->words * 4;
    put(p, OGPU_HDR(op, n + 1));
    for (i = 0; i < n; i++) put(p + 4 + i * 4, args[i]);
    b->words += 1 + n;
}

static void tris(struct OGPUBatch *b, int n, unsigned long vaddr, unsigned long vcount, unsigned long stride,
                 unsigned long iaddr, unsigned long icount, int isize, int texture, unsigned long flags,
                 unsigned long layout) {
    unsigned long a[9];
    a[0] = vaddr; a[1] = vcount; a[2] = stride; a[3] = iaddr; a[4] = icount; a[5] = (unsigned long)isize;
    a[6] = (unsigned long)texture & 0xFFFFUL; a[7] = flags; a[8] = layout;
    cmd(b, OGPU_OP_TRIANGLES, n, a);
}

void ogpu_triangles(struct OGPUBatch *b, unsigned long vaddr, unsigned long vcount, unsigned long stride,
                    unsigned long iaddr, unsigned long icount, int isize, int texture, unsigned long flags) {
    tris(b, 8, vaddr, vcount, stride, iaddr, icount, isize, texture, flags, 0);
}

void ogpu_triangles3d(struct OGPUBatch *b, unsigned long vaddr, unsigned long vcount, unsigned long stride,
                      unsigned long iaddr, unsigned long icount, int isize, int texture, unsigned long flags,
                      unsigned long layout) {
    tris(b, 9, vaddr, vcount, stride, iaddr, icount, isize, texture, flags, layout);
}

void ogpu_render3d(struct OGPUBatch *b, const unsigned long *args) {
    cmd(b, OGPU_OP_RENDER3D, OGPU_R3D_ARGS, args);
}

void ogpu_texenv(struct OGPUBatch *b, int unit, int mode, unsigned long colour) {
    unsigned long a[3];
    a[0] = (unsigned long)unit; a[1] = (unsigned long)mode; a[2] = colour;
    cmd(b, OGPU_OP_TEXENV, 3, a);
}

void ogpu_texture(struct OGPUBatch *b, int unit, unsigned long address, unsigned long bpr, int w, int h,
                  unsigned long format, unsigned long table, unsigned long generation, int min, int mag,
                  int wrap_s, int wrap_t, unsigned long border, int levels, const unsigned long *level_addrs) {
    unsigned long a[11 + OGPU_TEX_LEVELS];
    int i;
    if (levels < 1) levels = 1;
    if (levels > OGPU_TEX_LEVELS) levels = OGPU_TEX_LEVELS;
    a[0] = (unsigned long)unit; a[1] = address; a[2] = bpr; a[3] = OGPU_XY(w, h); a[4] = format; a[5] = table;
    a[6] = generation; a[7] = (unsigned long)min | ((unsigned long)mag << 8);
    a[8] = (unsigned long)wrap_s | ((unsigned long)wrap_t << 8); a[9] = border; a[10] = (unsigned long)levels;
    for (i = 1; i < levels; i++) a[10 + i] = level_addrs ? level_addrs[i - 1] : 0;
    cmd(b, OGPU_OP_TEXTURE, 10 + levels, a);
}

void ogpu_depth(struct OGPUBatch *b, int kind, unsigned long address, unsigned long bpr, int w, int h, int format) {
    unsigned long a[5];
    a[0] = (unsigned long)kind; a[1] = address; a[2] = bpr; a[3] = OGPU_XY(w, h); a[4] = (unsigned long)format;
    cmd(b, OGPU_OP_DEPTH, 5, a);
}

void ogpu_clear3d(struct OGPUBatch *b, int kind, int x, int y, int w, int h, unsigned long value) {
    unsigned long a[4];
    a[0] = (unsigned long)kind; a[1] = OGPU_XY(x, y); a[2] = OGPU_XY(w, h); a[3] = value;
    cmd(b, OGPU_OP_CLEAR3D, 4, a);
}

void ogpu_readback(struct OGPUBatch *b, int kind, int x, int y, int w, int h) {
    unsigned long a[4];
    a[0] = (unsigned long)kind | OGPU_KIND_READBACK; a[1] = OGPU_XY(x, y); a[2] = OGPU_XY(w, h); a[3] = 0;
    cmd(b, OGPU_OP_CLEAR3D, 4, a);
}
