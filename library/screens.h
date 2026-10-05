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
    UBYTE *mem;                 /* the pixels, one byte each (8-bit CLUT) */
    ULONG stride;               /* bytes a row */
    UWORD width, height;
    UBYTE monitor;              /* 1-4: in that board's video RAM; 0: fast RAM */
    ULONG vram_off;             /* its offset in the board's video RAM */
    ULONG vram_block;           /* where its block starts (the head, then the pixels) */
    struct Screen *screen;      /* the screen OpenRTG made it for, if any */
};

int ortg_is(struct BitMap *bm);
struct ortg_bitmap *ortg_alloc(int monitor, ULONG w, ULONG h, int clear);
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
