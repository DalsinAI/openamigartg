/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * OpenRTG's own screens and its chunky bitmaps (screens.c). */
#ifndef OPENRTG_SCREENS_H
#define OPENRTG_SCREENS_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include "modes.h"

#define ORTG_BM_MAGIC 0x4F525447UL                     /* "ORTG" */

struct Screen;

/* A chunky bitmap: the BitMap graphics sees, then OpenRTG's own fields. */
struct ortg_bitmap {
    struct BitMap bm;
    ULONG magic;
    UBYTE *mem;                 /* the pixels */
    ULONG stride;               /* bytes a row */
    UWORD width, height;
    UBYTE monitor;              /* 1-4: in that board's video RAM; 0: fast RAM */
    ULONG vram_off;             /* its offset in the board's video RAM */
    ULONG vram_block;           /* where its block starts (the head, then the pixels) */
    struct Screen *screen;      /* the screen OpenRTG made it for, if any */
    UBYTE format;               /* ORTG_CLUT8 (a pen a byte), ORTG_RGB16 (R5G6B5), ORTG_ARGB32 */
    UBYTE bpp;                  /* bytes a pixel: 1, 2 or 4 */
    UBYTE pal_index;            /* which pen table its pens use (16 and 32-bit) */
};

/* 16 and 32-bit bitmaps draw a pen as the colour the pen stands for: each
 * monitor's screens share one table of 256 colours (0x00RRGGBB), set from
 * the front screen's palette whenever it changes (OpenRTG 0.5). */
extern ULONG ortg_pen_rgb[][256];

/* 1 when any monitor shows an OpenRTG screen. */
int ortg_any_shown(void);

static inline ULONG ortg_encode(const struct ortg_bitmap *o, ULONG rgb)
{
    if (o->bpp == 2) return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    return rgb & 0xFFFFFF;
}
static inline ULONG ortg_decode(const struct ortg_bitmap *o, ULONG px)
{
    if (o->bpp == 2) {
        ULONG r = (px >> 11) & 31, g = (px >> 5) & 63, b = px & 31;
        return (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
    }
    return px & 0xFFFFFF;
}
/* The pixel a pen draws as. */
static inline ULONG ortg_pen_px(const struct ortg_bitmap *o, ULONG pen)
{
    return o->bpp == 1 ? (pen & 255) : ortg_encode(o, ortg_pen_rgb[o->pal_index][pen & 255]);
}
static inline ULONG ortg_get(const struct ortg_bitmap *o, LONG x, LONG y)
{
    const UBYTE *p = o->mem + y * o->stride;
    if (o->bpp == 1) return p[x];
    if (o->bpp == 2) return ((const UWORD *)p)[x];
    return ((const ULONG *)p)[x];
}
static inline void ortg_put(struct ortg_bitmap *o, LONG x, LONG y, ULONG v)
{
    UBYTE *p = o->mem + y * o->stride;
    if (o->bpp == 1) p[x] = (UBYTE)v;
    else if (o->bpp == 2) ((UWORD *)p)[x] = (UWORD)v;
    else ((ULONG *)p)[x] = v;
}
/* The pen nearest a pixel's colour (exact when the pen's colour is there). */
UBYTE ortg_px_pen(const struct ortg_bitmap *o, ULONG px);

int ortg_is(struct BitMap *bm);
struct ortg_bitmap *ortg_alloc(int monitor, ULONG w, ULONG h, int clear, int format);
void ortg_free(struct ortg_bitmap *o);

/* For pixels.c: the OpenRTG bitmap a BitMap is (or NULL); every visible
 * piece of a RastPort's box, in bitmap coordinates (dx, dy: the bitmap
 * position of the RastPort's 0,0), 0 when the RastPort isn't on an OpenRTG
 * bitmap; the colours a RastPort's pens stand for; a count that moves on
 * whenever an OpenRTG screen's palette changes. */
typedef void (*ortg_piece_fn)(void *ctx, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy);
struct ortg_bitmap *ortg_find(struct BitMap *bm);
int ortg_pieces(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, ortg_piece_fn fn, void *ctx);
struct ColorMap *ortg_colormap(struct RastPort *rp);
extern volatile ULONG ortg_palette_gen;

/* OpenRTG's screens on: the patches go in (once). boards[n]: monitor n's board. */
int ortg_screens_on(struct Library *gfx, struct ortg_mode_table **tables, APTR *boards);

#endif
