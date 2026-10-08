/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Building stream v1.2's 3D commands (stream3d.h) into an OGPU
 * batch (build.h). Link library code, like build.h: no library calls. */
#ifndef OPENGPU_BUILD3D_H
#define OPENGPU_BUILD3D_H

#include "build.h"
#include "stream3d.h"

/* SDL's form (9 words). */
void ogpu_triangles(struct OGPUBatch *b, unsigned long vaddr, unsigned long vcount, unsigned long stride,
                    unsigned long iaddr, unsigned long icount, int isize, int texture, unsigned long flags);
/* The 3D form (10 words): layout is OGPU_LAY_ primitive and fields. */
void ogpu_triangles3d(struct OGPUBatch *b, unsigned long vaddr, unsigned long vcount, unsigned long stride,
                      unsigned long iaddr, unsigned long icount, int isize, int texture, unsigned long flags,
                      unsigned long layout);
/* RENDER3D: args[0..OGPU_R3D_ARGS-1] as stream3d.h lists them. */
void ogpu_render3d(struct OGPUBatch *b, const unsigned long *args);
void ogpu_texenv(struct OGPUBatch *b, int unit, int mode, unsigned long colour);
/* TEXTURE: level_addrs holds levels-1 addresses (levels after the first), or 0 for one level;
 * address 0 unbinds the unit. */
void ogpu_texture(struct OGPUBatch *b, int unit, unsigned long address, unsigned long bpr, int w, int h,
                  unsigned long format, unsigned long table, unsigned long generation, int min, int mag,
                  int wrap_s, int wrap_t, unsigned long border, int levels, const unsigned long *level_addrs);
void ogpu_depth(struct OGPUBatch *b, int kind, unsigned long address, unsigned long bpr, int w, int h, int format);
void ogpu_clear3d(struct OGPUBatch *b, int kind, int x, int y, int w, int h, unsigned long value);
void ogpu_readback(struct OGPUBatch *b, int kind, int x, int y, int w, int h);

#endif
