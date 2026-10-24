/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * openrtg.library's public interface (version 0.1, phase 2's first part). */
#ifndef OPENRTG_OPENRTG_H
#define OPENRTG_OPENRTG_H

#define OPENRTG_NAME    "openrtg.library"
#define OPENRTG_VERSION 0

/* One mode of a monitor (ORTG_GetMode). */
struct OpenRTGMode {
    unsigned long  mode_id;
    unsigned short width, height;
    unsigned char  depth;          /* 8, 16 or 32 */
    unsigned char  format;         /* 0: 8-bit CLUT, 1: 16-bit, 2: 32-bit ARGB */
    unsigned char  standard;       /* 1: in the Standard list */
    char           name[32];       /* "OpenRTG.1: 1920x1080 32-bit" */
    unsigned char  native;         /* 0.17: 1 for a Native mode (the struct's last byte, padding before) */
};

/* The Native modes (0.17, the native screen mode): on an ACRTG board of protocol v5 or later, the
 * host's window can ask for the size of the Amiga's screen. While it does, each RTG monitor lists three
 * more modes, last: "OpenRTG.n: Native 8-bit", "16-bit" and "32-bit", at that size. Their ModeIDs never
 * change (the place 0x22 of the mode table): 0x6n221000, 0x6n221100 and 0x6n221200. A screen opened on
 * one gets the size of that moment and keeps it until it is opened again. BestModeIDA never picks one:
 * a program gets a Native mode only by naming it. */
#define ORTG_NATIVE_PLACE   0x22
#define ORTG_IS_NATIVE(id)  ((((id) >> 16) & 0xFFUL) == ORTG_NATIVE_PLACE && ((id) & 0x1000UL))

/* What the host asks of a monitor's board, for ORTG_NativeSize (0.17). */
struct OpenRTGNativeSize {
    unsigned short width, height;              /* the size the host's window asks for; 0 x 0 when none is */
    unsigned long  gen;                        /* up by one each time that size changes (WANT_GEN) */
    unsigned long  ask;                        /* up by one each time a page asks the Amiga to follow now (ASK) */
    unsigned short limit_width, limit_height;  /* the board's largest picture (LIMIT) */
    unsigned long  mode_id;                    /* the monitor's Native 32-bit ModeID */
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
    long                 dest_width, dest_height;   /* written at this size (0: the same); 0.16: through OpenGPU
                                                       for ARGB, 0RGB, and RAW on 16-bit */
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
    unsigned char  flags;           /* 0.20: ORTG_BMI_CARD; 0 before */
};
/* 0.20: in video RAM's card part (acrtg-v7), the PC graphics card's own
 * memory: its GPU draws there at full speed, the CPU reads it at a crawl.
 * cybergraphics.library's LockBitMapTags gives no address for it. 0.22:
 * not set when the host shadows the card part (acrtg-v8), where the CPU
 * reads at host speed. */
#define ORTG_BMI_CARD     0x01

/* 0.20: AllocBitMap's tags (BMATags_*, OS 3.2's extended form): ti_Data TRUE
 * asks for a displayable friend that only drawing commands will read (no
 * program locks it, the display never shows it): layers' window surfaces.
 * On a board with a card part it goes there, then below it, then in fast
 * RAM. Other graphics libraries ignore it. */
#define ORTG_BMA_CARD     0x804F5201UL

/* ORTG_WrittenSpans (0.23; decision D7(b), acrtg-v9): what the CPU itself
 * wrote into an OpenRTG bitmap in video RAM by address since the last
 * call, a row's piece at a time; never what OpenGPU or the board drew.
 * - The first call for a bitmap asks the board to watch it, and answers 0.
 *   A board watches one bitmap: a call for another moves the watch, but
 *   not off a bitmap a monitor shows to one it doesn't. A board that can't
 *   watch (before acrtg-v9, or a runtime not asked to) answers 0 always.
 * - It answers how many spans it put in buf, max at most. When it answers
 *   max, call again for the rest: nothing is lost between calls.
 * - buf NULL: the watch is set as above, and the answer is the address of
 *   a long that is not 0 while there is something to take, or 0 when the
 *   board doesn't watch the bitmap. Reading it costs nothing; the board
 *   sets it at the CPU's first write and clears it once everything is
 *   taken from the board. After a call that answered max there may be
 *   more whatever the long says.
 * layers.library uses it on a composited screen's bitmap: what a program
 * writes into the screen by address goes into the window there. */
struct OpenRTGSpan {
    unsigned short y;               /* the row */
    unsigned short x0, x1;          /* its pixels x0 to x1, both written */
};

/* A frame the host's GPU rendered through ACVirgl, for ORTG_PresentVirgl
 * (0.15). The host copies the resource's damage box straight into the
 * window's visible rectangles (ACV_PRESENT); nothing passes through the
 * 68k's memory twice. The 68k still decides where, and what is visible. */
struct OpenRTGVirglFrame {
    unsigned long  context;         /* ACVirgl context of the resource */
    unsigned long  handle;          /* the resource (its colour buffer) */
    unsigned short width, height;   /* of the resource */
    short          damage_x, damage_y;  /* the part that changed, in the resource; */
    unsigned short damage_w, damage_h;  /* 0 by 0: all of it */
    unsigned long  flags;           /* ORTG_VIRGL_FLIP_Y: row 0 is the bottom (GL) */
};
#define ORTG_VIRGL_FLIP_Y       1

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
#define ORTG_STAT_VIRGL         20  /* ORTG_PresentVirgl: GL frames the host wrote straight in (0.15) */
/* 0.21: OpenGPU's batches, not pieces: those left running (where the GPU
 * count is), those waited for, the bytes of sources copied to be left
 * running; the fourth count stays 0 */
#define ORTG_STAT_WAIT          21
#define ORTG_STAT_COUNT         22
#define ORTG_STAT_NAMES { "fill", "invert", "pattern", "template", "copy", "blit", "scroll", "setrast", "line", \
    "pixel", "chunky", "chunky read", "pixels", "pixels read", "pixels fill", "pixels invert", "alpha", \
    "blit planar", "blit convert", "blit masked", "virgl", "batches" }

#endif
