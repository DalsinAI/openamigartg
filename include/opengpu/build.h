/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Building an OGPU stream v1 (stream.h) in memory. Link library code: the
 * calls write words into the caller's buffer and never call opengpu.library,
 * so building a batch costs no library calls; OGPU_Submit sends it.
 * A batch that runs out of room sets `overflow` and stops growing. */
#ifndef OPENGPU_BUILD_H
#define OPENGPU_BUILD_H

#include "stream.h"

struct OGPUBatch {
    unsigned char *buf;     /* the stream */
    long cap;               /* words it can hold */
    long words;             /* words written */
    int overflow;           /* a command didn't fit, and was left out */
};

void ogpu_batch_init(struct OGPUBatch *b, void *buf, long cap_words);
void ogpu_surface(struct OGPUBatch *b, int slot, unsigned long address, unsigned long bpr, int w, int h, int format);
void ogpu_target(struct OGPUBatch *b, int slot);
void ogpu_clip(struct OGPUBatch *b, int x, int y, int w, int h);
void ogpu_fill(struct OGPUBatch *b, int x, int y, int w, int h, unsigned long colour);
void ogpu_invert(struct OGPUBatch *b, int x, int y, int w, int h, unsigned long mask);
void ogpu_copy(struct OGPUBatch *b, int src_slot, int sx, int sy, int x, int y, int w, int h);
void ogpu_template(struct OGPUBatch *b, unsigned long address, unsigned long bpr, unsigned long first_bit,
                   int x, int y, int w, int h, unsigned long fg, unsigned long bg, int mode);
void ogpu_pattern(struct OGPUBatch *b, unsigned long address, unsigned long rows,
                  int x, int y, int w, int h, unsigned long fg, unsigned long bg, int mode);
void ogpu_line(struct OGPUBatch *b, int x0, int y0, int x1, int y1, unsigned long fg, int mode);
void ogpu_pixels(struct OGPUBatch *b, unsigned long address, unsigned long bpr, int format, unsigned long table,
                 int x, int y, int w, int h);
void ogpu_composite(struct OGPUBatch *b, int src_slot, int sx, int sy, int sw, int sh,
                    int x, int y, int w, int h, int alpha, unsigned long flags);
void ogpu_fence(struct OGPUBatch *b, unsigned long id);

#endif
