/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's 3D core: the CPU rasteriser behind stream v1.2's 3D ops
 * (include/opengpu/stream3d.h). It draws SDL 2's geometry and Warp3D's
 * triangles alike: every primitive, texture format, environment, blend,
 * fog, Z, stencil and alpha mode Warp3D has, the Team's replacement for a
 * software Warp3D.
 *
 * Like ogpu_core.c: plain C, integers only, no C library, pixels read and
 * written byte by byte in the Amiga's order, so the 68k, the Cradle's
 * runtime and a PiStorm's ARM core draw the same pixels. It draws into the
 * core's surfaces (struct ogpu_core's slots and target) and maps memory
 * through the core's map hook.
 *
 * Wiring: the core hands opcodes 0x0030-0x0035 to ogpu_3d_run. The 3D state
 * lives for one batch beside the core (struct ogpu_core's d3, set up with
 * ogpu3d_init), as the core's slots do: each batch carries the state it
 * draws with. Until the core carries v1.2, ogpu3d_run splits a stream
 * itself: 3D commands here, the rest through ogpu_core_run. */
#ifndef OGPU_3D_H
#define OGPU_3D_H

#include "ogpu_core.h"
#include "../../include/opengpu/stream3d.h"

#define OGPU3D_FOGTAB 1024              /* fog factors for w = 0 .. 1 */

struct ogpu3d_tex {
    const ogpu_u8 *level[OGPU_TEX_LEVELS];  /* mapped; level[0] == 0: no texture */
    const ogpu_u8 *table;                   /* INDEX8's 256 ARGB32, mapped */
    long bpr[OGPU_TEX_LEVELS];
    int w, h, levels, format, base, bpp;
    int min, mag, wrap_s, wrap_t;
    ogpu_u32 border;
};

struct ogpu3d_buf {
    ogpu_u8 *mem;                       /* mapped; 0 = none */
    long bpr;
    int w, h, format;
};

struct ogpu3d {
    struct ogpu_core *core;
    struct ogpu3d_tex tex[OGPU_TEX_UNITS];
    int env[OGPU_TEX_UNITS];
    ogpu_u32 env_colour[OGPU_TEX_UNITS];
    struct ogpu3d_buf depth, stencil;
    /* RENDER3D, decoded */
    ogpu_u32 enables;
    int zfunc, afunc, aref, bsrc, bdst, logicop, fogmode, chroma;
    int sfunc, sref, smask, sfail, szfail, szpass, swmask;
    ogpu_u32 bconst, cmask, fogcolour, chroma_lo, chroma_hi, current;
    const ogpu_u8 *pen_table, *stipple, *palette;
    ogpu_u8 fogtab[OGPU3D_FOGTAB];
    unsigned int fogkey[4];             /* what the table was built for */
    long triangles, pixels;             /* counters, for the caller's statistics */
    int slow;                           /* 1: never the fast spans (tests compare the two) */
};

void ogpu3d_init(struct ogpu3d *t, struct ogpu_core *c);

/* 1 for an opcode this file carries out. */
int ogpu3d_is_op(int op);

/* Carry out one 3D command (header first, `words` long; the run has checked
 * the length fits the stream). Returns OGPU_OK or an error. */
int ogpu3d_op(struct ogpu3d *t, int op, const ogpu_u8 *cmd, long words);

/* A whole stream with 3D commands in it: they come here, the others go to
 * ogpu_core_run. Returns commands done; c->last_error as ogpu_core_run. */
long ogpu3d_run(struct ogpu3d *t, const ogpu_u8 *stream, long words);

/* The core's hook, once struct ogpu_core has `struct ogpu3d *d3` (0 = no 3D:
 * the ops answer OGPU_ERR_BADOP). */
#ifdef OGPU_CORE_HAS_D3
int ogpu_3d_run(struct ogpu_core *c, int op, const ogpu_u8 *cmd, long words);
#endif

/* OGPU_FULL, OGPU_PARTIAL or OGPU_NONE for a 3D opcode into a format. */
int ogpu3d_supports(int op, int format);

#endif
