/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU stream v1.1: the bytes every OpenGPU back end receives (DESIGN.md
 * section 5). v1.1 adds A8 coverage masks for anti-aliased text and clip
 * paths (OpenGfx): the A8 format, MASK, and COMPOSITE's MASK, ADD and IN
 * flags. A v1.0 back end answers OGPU_NONE to OGPU_Query(OGPU_OP_MASK, ...),
 * so a program asks before it uses any of them. The 68k's own CPU back end, ACRTG.gpu on the Cradle and
 * PiStorm.gpu on a spare ARM core all read exactly this, so they draw the
 * same pixels.
 *
 * A stream is 32-bit words, big-endian (the Amiga's order) wherever it is
 * read. Each command starts with a header word: the opcode in the top 16
 * bits and the command's length in words, header included, in the bottom 16.
 * A back end that meets an opcode it doesn't know skips it by its length
 * and reports OGPU_ERR_BADOP, so newer streams fail one command, not all.
 *
 * Points and sizes are packed in one word: x (or width) in the top 16 bits,
 * y (or height) in the bottom 16, signed for points.
 *
 * Addresses are the Amiga's own. Each back end maps them to its memory: the
 * CPU back end uses them as they are, the Cradle through the guest's RAM
 * and video RAM, the PiStorm through Emu68's view of fast RAM.
 *
 * Pixels in memory are in the Amiga's byte order: RGB565 high byte first,
 * ARGB32 as the bytes A, R, G, B. Colours in commands are raw pixel values
 * in the target's format (a pen for CLUT8). */
#ifndef OPENGPU_STREAM_H
#define OPENGPU_STREAM_H

#define OGPU_STREAM_VERSION 1
#define OGPU_STREAM_MINOR   1

#define OGPU_HDR(op, words)     (((unsigned long)(op) << 16) | ((unsigned long)(words) & 0xFFFFUL))
#define OGPU_HDR_OP(w)          ((unsigned)((w) >> 16))
#define OGPU_HDR_WORDS(w)       ((unsigned)((w) & 0xFFFFUL))
#define OGPU_XY(x, y)           ((((unsigned long)(x) & 0xFFFFUL) << 16) | ((unsigned long)(y) & 0xFFFFUL))

/* Pixel formats (surfaces and PIXELS sources). */
#define OGPU_FMT_CLUT8          1   /* 1 byte: a pen */
#define OGPU_FMT_RGB565         2   /* 2 bytes, high byte first */
#define OGPU_FMT_ARGB32         3   /* 4 bytes: A, R, G, B */
#define OGPU_FMT_INDEX8         4   /* PIXELS sources only: 1 byte, looked up in a 256-entry ARGB table */
#define OGPU_FMT_A8             5   /* v1.1: 1 byte: coverage (alpha) 0-255, no colour */
#define OGPU_FMT_COUNT          6

/* Draw modes, as graphics.library's (JAM1, JAM2, COMPLEMENT, INVERSVID). */
#define OGPU_JAM1               0
#define OGPU_JAM2               1
#define OGPU_COMPLEMENT         2
#define OGPU_INVERSVID          4

/* Opcodes. Words after the header, in order:                                      words */
#define OGPU_OP_NOP             0x0000  /* (any padding)                                 1+ */
#define OGPU_OP_SURFACE         0x0001  /* slot, address, bytes per row, wh, format      6  */
#define OGPU_OP_TARGET          0x0002  /* slot: where the next commands draw            2  */
#define OGPU_OP_CLIP            0x0003  /* xy, wh: drawing stays inside; wh 0 = whole     3  */
#define OGPU_OP_FILL            0x0010  /* xy, wh, colour                                4  */
#define OGPU_OP_INVERT          0x0011  /* xy, wh, mask (pixel ^= mask)                  4  */
#define OGPU_OP_COPY            0x0012  /* source slot, source xy, xy, wh (overlap safe) 5  */
#define OGPU_OP_TEMPLATE        0x0013  /* address, bytes per row, first bit, xy, wh,
                                           fg, bg, mode                                  9  */
#define OGPU_OP_PATTERN         0x0014  /* address, rows (1, 2, 4 .. 256), xy, wh,
                                           fg, bg, mode                                  8  */
#define OGPU_OP_LINE            0x0015  /* xy0, xy1, fg, mode (both ends drawn)          5  */
#define OGPU_OP_PIXELS          0x0016  /* address, bytes per row, format, table, xy, wh 7  */
#define OGPU_OP_MASK            0x0017  /* v1.1: address, bytes per row, xy, wh, colour  6
                                           (an A8 mask in memory; colour is ARGB32)      */
#define OGPU_OP_COMPOSITE       0x0020  /* source slot, source xy, source wh, xy, wh,
                                           alpha (0-255), flags                          8  */
                                        /* with OGPU_COMP_MASK, then: mask slot, mask xy 10 */
#define OGPU_OP_FENCE           0x00F0  /* id: reported when everything before is done   2  */

/* OGPU_OP_COMPOSITE flags. */
#define OGPU_COMP_SRCALPHA      1   /* use the source's own alpha (ARGB32 sources) */
#define OGPU_COMP_BILINEAR      2   /* filter when scaling; nearest otherwise */
/* v1.1. Coverage (each pixel's source alpha times alpha, and times the mask)
 * is applied with OVER unless ADD or IN is set. */
#define OGPU_COMP_MASK          4   /* an A8 surface's coverage, read 1:1 against the destination rect */
#define OGPU_COMP_ADD           8   /* dst + src * coverage, each channel saturating at 255 */
#define OGPU_COMP_IN            16  /* dst * coverage: clips what is already there */

#define OGPU_MAX_SLOTS          16
#define OGPU_MAX_SIZE           16384   /* widths and heights */

/* Back end results. */
#define OGPU_OK                 0
#define OGPU_ERR_BADOP          -1  /* an opcode this back end doesn't know (skipped) */
#define OGPU_ERR_BADLEN         -2  /* a length that doesn't fit the command or the stream (stops) */
#define OGPU_ERR_NOSURFACE      -3  /* a slot that wasn't described, or no target */
#define OGPU_ERR_NOMAP          -4  /* an address the back end can't reach */
#define OGPU_ERR_UNSUPPORTED    -5  /* a format pair the command can't do */

/* What a back end can do, per operation and target format (OGPU_Query). */
#define OGPU_NONE               0
#define OGPU_PARTIAL            1
#define OGPU_FULL               2

#endif
