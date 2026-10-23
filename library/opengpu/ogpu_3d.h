/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
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
/* A slot texture's filter (min, mag) for SDL's PIXELART (v1.7). Warp3D's
 * are 1-6 and TEXTURE keeps its filter word's low byte, so this is beyond
 * any it can give: a v1.2 stream's filter 7 draws as it always did. */
#define OGPU3D_PIXELART 0x100

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

/* ---- what TRIANGLES works out before it draws -------------------------------------
 * A back end that draws elsewhere (host/vulkan, on the host's GPU) takes
 * these through a sink (below): the settings of each TRIANGLES command and
 * each triangle exactly as this file would draw them, so it only has to
 * colour the pixels. */

/* The values a triangle carries, in this order (gx, gy and row's indices). */
enum { OGPU3D_R, OGPU3D_G, OGPU3D_B, OGPU3D_A, OGPU3D_SR, OGPU3D_SG, OGPU3D_SB, OGPU3D_F, OGPU3D_Z, OGPU3D_FI,
       OGPU3D_U, OGPU3D_V, OGPU3D_Q, OGPU3D_VALUES };

/* One mip level as a triangle samples it. */
struct o3_level {
    const ogpu_u8 *base;
    unsigned int bpr;
    int w, h, wmask, hmask;             /* masks for power-of-two sizes, else 0 */
    int bpp, format, wrap_s, wrap_t;
    const ogpu_u8 *table;
    int border[4];
};

/* What one TRIANGLES command draws with: RENDER3D's state for the 3D form,
 * SDL's flags turned into the same terms for the 9-word form. */
struct o3_pipe {
    const struct ogpu_surface *dst;     /* the target */
    unsigned int en;                    /* OGPU_R3D_ */
    const struct ogpu3d_tex *tx;        /* 0 = untextured */
    int env, base;
    int envc[4];
    int bsrc, bdst;
    int fmt;                            /* the target's */
    int has_z, has_w, has_spec;
    int cur[4], fog[4], kc[4];          /* current, fog and blend colours as channels */
    int mask_all;                       /* the colour mask lets everything through */
    unsigned int wmask;                 /* else: the raw bits it lets through */
};

/* One triangle's values: each a plane, exact modulo 2^32. A value k the
 * triangle carries (act) is row[k] + gy[k] * (y - ystart) + gx[k] * (x - rx)
 * at pixel (x, y); the others are 0. Texture coordinates are in texels of
 * level `level`, 24.8; under perspective (persp) U, V and Q are divided
 * every 8 pixels from the start of each span. */
struct o3_tri {
    int gx[OGPU3D_VALUES], gy[OGPU3D_VALUES], row[OGPU3D_VALUES];  /* steps across and down; value at column rx of the current row */
    int rx;
    int nact, act[OGPU3D_VALUES];       /* the values this triangle carries */
    int persp, bilinear, lfrac;         /* lfrac: blend with the next level, 0..255 */
    int level;                          /* L0's level of the texture (L1 is the next) */
    struct o3_level L0, L1;
    /* v1.7, SDL's PIXELART: the texels a pixel spans each way (16.16, 256-65536)
     * and 2^24 over them (stream.h's OGPU_AFF_PIXELART); and U and V ([0], [1])
     * exactly, as 16.16 texels and a remainder over pden (the triangle's area):
     * at column rx of the row drawn, and their steps across and down */
    int pixelart;
    unsigned int pbu, pru, pbv, prv;
    int paq[2], pxq[2], pyq[2];
    unsigned int par[2], pxr[2], pyr[2], pden;
};

/* TRIANGLES hands its work to a sink instead of drawing (struct ogpu3d's
 * sink, 0: it draws). Spans come for each row from ystart to yend - 1, top
 * to bottom, x0 <= x < x1 (x0 >= x1: nothing on that row), already inside
 * the target, the clip and the depth and stencil buffers. */
struct ogpu3d_sink {
    /* A TRIANGLES command's settings, before its first triangle, and the
     * drawing area (x0 <= x < x1, y0 <= y < y1). Nonzero leaves the command
     * to this file: TRIANGLES then returns OGPU3D_DECLINED having drawn
     * nothing, and the caller runs it again without the sink. */
    int (*begin)(void *user, const struct ogpu3d *t, const struct o3_pipe *pp, int x0, int y0, int x1, int y1);
    void (*tri)(void *user, const struct o3_tri *tr, int ystart, int yend);
    void (*span)(void *user, int y, int x0, int x1);
};
#define OGPU3D_DECLINED (-100)          /* never an OGPU_ERR_ code */

/* Only the host's back ends take a sink: the 68k's core is built without it,
 * so its code is as it was. */
#if !defined(__mc68000__) && !defined(OGPU3D_NO_SINK)
#define OGPU3D_SINK 1
#endif

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
#ifdef OGPU3D_SINK
    const struct ogpu3d_sink *sink;     /* 0: TRIANGLES draws here (ogpu3d_init sets 0) */
    void *sink_user;
#endif
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
