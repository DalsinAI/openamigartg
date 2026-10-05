/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Pixel arrays (pixels.c): what ORTG_WritePixels and the rest do. */
#ifndef ORTG_PIXELS_H
#define ORTG_PIXELS_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include "../include/openrtg/openrtg.h"

LONG ortg_write_pixels(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px);
LONG ortg_read_pixels(struct RastPort *rp, LONG x, LONG y, struct OpenRTGPixels *px);
LONG ortg_fill_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, ULONG argb);
LONG ortg_invert_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h);
BOOL ortg_bitmap_info(struct BitMap *bm, struct OpenRTGBitMapInfo *info);

#endif
