/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's insides: the library base, a context's private state and
 * a texture's. The library is a thin front end: it keeps Warp3D's state,
 * converts vertices and textures, and turns every call into OGPU stream
 * v1.2 commands (include/opengpu/stream3d.h). A batch goes to
 * opengpu.library (the "W3D_OpenGPU" driver: the host's GPU on AmigaChrome)
 * or, with no opengpu.library that draws 3D, to the same 3D core linked in
 * here ("W3D_CPU"). Both draw the same pixels. */
#ifndef W3D_INTERNAL_H
#define W3D_INTERNAL_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <utility/tagitem.h>
#include <Warp3D/Warp3D.h>

#include "../opengpu/ogpu_core.h"
#include "../opengpu/ogpu_3d.h"
#include "../../include/opengpu/build3d.h"

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#define LIBCALL                                 /* library entry points */

#define W3D_LIB_VERSION   5
#define W3D_LIB_REVISION  0

/* Drivers (W3D_GetDrivers order). */
#define ROUTE_GPU 0                             /* opengpu.library: the ring to the host when there is one */
#define ROUTE_CPU 1                             /* the 3D core on this 68k */

struct W3DBase {
    struct Library lib;
    BPTR seglist;
    struct SignalSemaphore lock;
    W3D_Driver drv[2];
    W3D_Driver *drivers[3];                     /* W3D_GetDrivers: 0-terminated */
    int gpu_3d;                                 /* opengpu.library opened and draws 3D */
    int gpu_host;                               /* ... on a back end that is not this 68k */
    int prefer_cpu;                             /* ENV:Warp3D/Driver says CPU */
    int prefer_gpu;                             /* ... OpenGPU, even on its CPU back end */
    int z32;                                    /* ENV:Warp3D/ZBuffer says 32 */
    /* Speed against looks, as Wazp3D offered them (ENV:Warp3D/Fog, Perspective,
     * Filtering, Lighting: "0" turns one off; all on by default). */
    int no_fog, no_persp, no_filter, no_light;
    int quality;                                /* ENV:Warp3D/Perspective: 0 per 8 pixels (default) */
};

/* A texture's private part (W3D_Texture.driver). Every texture is kept as
 * ARGB32 with its mip levels, which is what the 3D core and a GPU read
 * fastest; what the texels mean (alpha only, luminance ...) goes with it. */
struct w3dtex {
    ULONG *level[OGPU_TEX_LEVELS];              /* ARGB32, level 0 first */
    int levels;
    int w, h;
    int base;                                   /* OGPU_TEXBASE_ */
    ULONG generation;                           /* changes whenever the texels do */
    int min, mag, wrap_s, wrap_t;
    ULONG border;
    int env;                                    /* W3D_REPLACE .. */
    ULONG envcolour;                            /* ARGB32 */
    ULONG chroma_lo, chroma_hi;                 /* ARGB32 */
    int chroma_mode;
    ULONG *srcpal;                              /* CHUNKY: the palette given (ARGB) */
};

/* The vertex the stream carries (stream3d.h, the full 3D layout). */
struct ovtx {
    LONG x, y;                                  /* 16.16 */
    ULONG argb;
    LONG u, v;                                  /* 16.16, 0..1 over the texture */
    ULONG z;                                    /* 0..0xFFFFFFFF */
    ULONG w;                                    /* IEEE754 single */
    ULONG spec;                                 /* A: fog factor, RGB: specular */
};
#define OVTX_LAYOUT (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC)

#define BATCH_WORDS   16384                     /* 64 KB of commands */
#define PREFIX_WORDS  16                        /* room for SURFACE and TARGET in front */
#define VERT_BYTES    (256 * 1024)              /* vertices and small data for one batch */

/* A context's private part (W3D_Context.driver). */
struct w3dctx {
    W3D_Context *ctx;
    int route;                                  /* ROUTE_ */
    /* the drawing area */
    int fmt;                                    /* OGPU_FMT_ the core draws into */
    int direct;                                 /* draws in the bitmap's memory */
    ULONG *off;                                 /* else: an ARGB32 copy, written back with WritePixelArray */
    ULONG offbpr;
    APTR bmlock;                                /* LockBitMapTags while a batch runs */
    UBYTE *base;                                /* the area's first byte while locked */
    ULONG bpr;
    /* buffers */
    UBYTE *zbuf;
    int zfmt;                                   /* OGPU_FMT_Z16 or Z32 */
    ULONG zbpr;
    UBYTE *sbuf;
    /* the batch: commands, then the vertices and data they point to */
    ULONG *cmdmem;
    struct OGPUBatch b;                         /* builds from cmdmem + PREFIX_WORDS */
    UBYTE *vmem;
    ULONG vused;
    int body;                                   /* the batch has commands */
    long list_at;                               /* word of the last TRIANGLES list, or -1, to extend it */
    W3D_Texture *list_tex;
    ULONG list_vend;                            /* where its vertices end */
    /* state the stream has */
    W3D_Texture *sent_tex;
    ULONG sent_gen;
    int r3d_dirty, tex_dirty, clip_dirty;
    /* Warp3D's state */
    ULONG zfunc, afunc, bsrc, bdst, logicop, fogmode, sfunc, sref, smask, sfail, szfail, szpass, swmask;
    float aref;
    ULONG cmask, penmask;
    W3D_Color current;
    ULONG currentpen;
    ULONG hints[16];
    int fogmode_v5;                             /* W3D_SetParameter's fog mode, or -1 */
    float zfog_start, zfog_end, zfog_density;
    UBYTE *stipple;                             /* 32x32 polygon stipple (a copy) or 0 */
    UWORD line_pattern;
    int line_factor;
    float point_size, line_width;
    struct w3dtex *global_env;                  /* W3D_GLOBALTEXENV: the environment from SetTexEnv(NULL) */
    int genv;
    ULONG genvcolour;
    /* the 3D core, for ROUTE_CPU */
    struct ogpu_core core;
    struct ogpu3d d3;
    ULONG fence;
    W3D_Bitmap texbm;                           /* W3D_SetDrawRegionTexture's drawing area */
};

/* ---- shared ---------------------------------------------------------------- */

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct GfxBase *GfxBase;
extern struct Library *UtilityBase;
extern struct Library *CyberGfxBase;
extern struct Library *OpenGPUBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *AslBase;
extern struct W3DBase *W3DBase;

#define CTX(c) ((struct w3dctx *)(c)->driver)
#define TEX(t) ((struct w3dtex *)(t)->driver)

void *w3d_alloc(ULONG bytes);                   /* cleared */
void w3d_free(void *p);
void w3d_copy(void *d, const void *s, ULONG n);
void w3d_clear(void *d, ULONG n);
ULONG w3d_colour(const W3D_Color *c);           /* to ARGB32 */
LONG w3d_f2l(float f);                          /* rounded to the nearest */
ULONG w3d_fbits(float f);

void w3d_read_prefs(struct W3DBase *base);     /* again for each new context */

/* w3d_batch.c */
int  w3d_open_area(struct w3dctx *x, struct BitMap *bm, W3D_Bitmap *wbm, int yoffset);
void w3d_close_area(struct w3dctx *x);
void w3d_begin(struct w3dctx *x);               /* the state the next primitives need */
ULONG w3d_flush(struct w3dctx *x);
void w3d_maybe_flush(struct w3dctx *x);         /* after a primitive, when drawing is direct */
int  w3d_room(struct w3dctx *x, long words, ULONG vbytes);
UBYTE *w3d_data(struct w3dctx *x, ULONG bytes); /* bytes in the batch's data area (aligned) */
void w3d_select_texture(struct w3dctx *x, W3D_Texture *t);
struct ovtx *w3d_vspace(struct w3dctx *x, ULONG n);
void w3d_emit(struct w3dctx *x, struct ovtx *d, ULONG n, int prim);
void w3d_send_clip(struct w3dctx *x);
ULONG w3d_fill(struct w3dctx *x, ULONG argb);
void w3d_clear_buffer(struct w3dctx *x, int kind, ULONG value);
ULONG w3d_query(int route, ULONG query, ULONG destfmt);
ULONG w3d_texfmt_info(int route, ULONG format, ULONG destfmt);

/* w3d_draw.c */
void w3d_vertex(struct w3dctx *x, const W3D_Vertex *v, struct w3dtex *t, struct ovtx *o);

/* w3d_texture.c */
void w3d_free_texture(struct w3dctx *x, W3D_Texture *t);

#endif
