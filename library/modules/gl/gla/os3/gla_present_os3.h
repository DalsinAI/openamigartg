/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Where gl.library shows a context's frames on OS 3.2 (gla_present_os3.c). */
#ifndef GLA_PRESENT_OS3_H
#define GLA_PRESENT_OS3_H

struct RastPort;
struct BitMap;
struct gla_present;

struct gla_os3_target {
    struct RastPort *rp;        /* drawn through WritePixelArray (layers respected) */
    struct BitMap   *bitmap;    /* set only for a screen GL owns: drawn by OpenGPU directly */
    int left, top;              /* the GL area's corner in rp (or bitmap) */
};

/* Point p at t; t must outlive the GL buffer that uses p. */
void gla_os3_present_init(struct gla_present *p, struct gla_os3_target *t);

#endif
