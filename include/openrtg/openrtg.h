/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * openrtg.library's public interface (version 0.1, phase 2's first part). */
#ifndef OPENRTG_OPENRTG_H
#define OPENRTG_OPENRTG_H

#define OPENRTG_NAME    "openrtg.library"
#define OPENRTG_VERSION 0

/* One mode of a monitor (the same layout as library/modes.h's ortg_mode). */
struct OpenRTGMode {
    unsigned long  mode_id;
    unsigned short width, height;
    unsigned char  depth;          /* 8, 16 or 32 */
    unsigned char  format;         /* 0: 8-bit CLUT, 1: 16-bit, 2: 32-bit ARGB */
    unsigned char  standard;       /* 1: in the Standard list */
    char           name[32];       /* "OpenRTG.1: 1920x1080 32-bit" */
};

/* A rectangle of pixels in memory, for ORTG_WritePixels and ORTG_ReadPixels
 * (0.4). The formats are CyberGraphX's RECTFMT_ numbers, so its library can
 * pass them on as they are; ORTG_PIX_INDEX is bytes looked up in ctable. */
struct OpenRTGPixels {
    void                *data;              /* row 0 of the memory */
    long                 x, y;              /* the rectangle's corner in it */
    long                 modulo;            /* bytes a row */
    unsigned long        format;            /* ORTG_PIX_ */
    const unsigned long *ctable;            /* ORTG_PIX_INDEX: 256 colours, 0x00RRGGBB */
    long                 width, height;     /* the rectangle */
    long                 dest_width, dest_height;   /* written at this size (0: the same) */
};

#define ORTG_PIX_RGB      0UL     /* 3 bytes: R, G, B */
#define ORTG_PIX_RGBA     1UL
#define ORTG_PIX_ARGB     2UL
#define ORTG_PIX_PEN      3UL     /* 1 byte: the pen itself (CyberGraphX's LUT8) */
#define ORTG_PIX_GREY     4UL
#define ORTG_PIX_RAW      5UL     /* the bitmap's own pixels (8-bit: pens) */
#define ORTG_PIX_BGR      109UL
#define ORTG_PIX_BGRA     111UL
#define ORTG_PIX_ABGR     113UL
#define ORTG_PIX_0RGB     114UL
#define ORTG_PIX_BGR0     115UL
#define ORTG_PIX_RGB0     116UL
#define ORTG_PIX_0BGR     117UL
#define ORTG_PIX_INDEX    0x100UL /* 1 byte, looked up in ctable */

/* What an OpenRTG bitmap is, for ORTG_BitMapInfo (0.4). */
struct OpenRTGBitMapInfo {
    void          *memory;          /* pixel 0,0 */
    unsigned long  bytes_per_row;
    unsigned short width, height;
    unsigned char  depth;           /* 8 */
    unsigned char  format;          /* as OpenRTGMode's */
    unsigned char  monitor;         /* 1-4: in that board's video RAM; 0: fast RAM */
    unsigned char  pad;
};

/* ORTG_DrawStats (0.11): how OpenRTG drew on its bitmaps, by kind of
 * drawing, since it started or was last reset. Each kind has four counts:
 * pieces OpenGPU drew, pieces the CPU drew, and the pixels of each. A piece
 * is one visible rectangle of one call (a call through a window partly
 * covered draws several). OpenGPU draws a piece when its command does
 * exactly what OpenRTG's CPU code does; everything else stays on the CPU. */
/* ORTG_DrawStats' flags. */
#define ORTG_DRAW_RESET         1   /* the counts back to 0 */
#define ORTG_DRAW_CPU_ONLY      2   /* from now on, draw everything with the CPU code (to compare) */
#define ORTG_DRAW_OPENGPU       4   /* ... and through OpenGPU again, where it is exact (the default) */
#define ORTG_STAT_FILL          0   /* RectFill, Text's background, underlines */
#define ORTG_STAT_INVERT        1   /* COMPLEMENT fills */
#define ORTG_STAT_PATTERN       2   /* area patterns */
#define ORTG_STAT_TEMPLATE      3   /* BltTemplate, Text's glyphs, BltPattern's masks */
#define ORTG_STAT_COPY          4   /* plain blits, ClipBlit, ScrollRaster's move */
#define ORTG_STAT_BLIT          5   /* chunky blits with a minterm other than a copy, or a plane mask */
#define ORTG_STAT_SCROLL        6   /* ScrollRaster's uncovered area */
#define ORTG_STAT_SETRAST       7   /* SetRast */
#define ORTG_STAT_LINE          8   /* Draw, PolyDraw */
#define ORTG_STAT_PIXEL         9   /* WritePixel, ReadPixel */
#define ORTG_STAT_CHUNKY        10  /* WriteChunkyPixels, WritePixelArray8, WritePixelLine8 */
#define ORTG_STAT_CHUNKY_READ   11  /* ReadPixelArray8, ReadPixelLine8 */
#define ORTG_STAT_PIXELS        12  /* ORTG_WritePixels: CyberGraphX's and Picasso96API's pixel arrays */
#define ORTG_STAT_PIXELS_READ   13  /* ORTG_ReadPixels */
#define ORTG_STAT_PIXELS_FILL   14  /* ORTG_FillPixels */
#define ORTG_STAT_PIXELS_INVERT 15  /* ORTG_InvertPixels */
#define ORTG_STAT_ALPHA         16  /* ORTG_WritePixelsAlpha */
#define ORTG_STAT_BLIT_PLANAR   17  /* blits from or to a planar bitmap */
#define ORTG_STAT_BLIT_CONVERT  18  /* copies between OpenRTG bitmaps of two formats */
#define ORTG_STAT_BLIT_MASKED   19  /* blits through a mask (BltMaskBitMapRastPort) */
#define ORTG_STAT_COUNT         20
#define ORTG_STAT_NAMES { "fill", "invert", "pattern", "template", "copy", "blit", "scroll", "setrast", "line", \
    "pixel", "chunky", "chunky read", "pixels", "pixels read", "pixels fill", "pixels invert", "alpha", \
    "blit planar", "blit convert", "blit masked" }

#endif
