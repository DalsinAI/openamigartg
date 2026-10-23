/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU stream v1.7: the bytes every OpenGPU back end receives (DESIGN.md
 * section 5). v1.1 added A8 coverage masks for anti-aliased text and clip
 * paths (OpenGfx): the A8 format, MASK, and COMPOSITE's MASK, ADD and IN
 * flags. v1.2 (8 October 2026) adds what SDL 2's renderer and Warp3D need:
 * blend modes (as SDL's), FILL_BLEND, LINES_BLEND, POINTS_BLEND,
 * COMPOSITE_AFFINE (scale, rotate, flip, colour modulation), YUV, the
 * formats of real cards' screens, and the 3D range 0x0030-0x003F, whose
 * layouts are in stream3d.h (TRIANGLES serves SDL_RenderGeometry and
 * Warp3D alike). An older back end answers OGPU_NONE to OGPU_Query for
 * anything newer than it, so a program asks before it uses any of them.
 * v1.3 (12 October 2026) adds XRGB32, ARGB32's colour without its alpha: a
 * 32-bit OpenRTG screen's pixels, whose top byte is always 0, so ARGB
 * pixels reach one through PIXELS on the GPU, not the 68k.
 * v1.4 (17 October 2026) adds COMPOSITE_PD: Porter-Duff compositing of
 * premultiplied colour, with pixman's operators and rounding, so
 * pixman.library hands its composites to OpenGPU. The 68k's own CPU back end, ACRTG.gpu on the Cradle and
 * PiStorm.gpu on a spare ARM core all read exactly this, so they draw the
 * same pixels.
 * v1.5 (22 October 2026) adds YUV_SCALED: a part of a YUV picture converted
 * straight into a destination rect of any size (nearest), so a video frame
 * reaches the target in one pass.
 * v1.6 (23 October 2026) adds vector drawing, so that opengpu.library is the
 * one place for a browser's pixels (no cairo, no pixman): PATH_FILL (an
 * anti-aliased path, non-zero or even-odd), PATH_STROKE (widths, caps,
 * joins, dashes), gradients (linear, radial, conic) as a path's paint, BLUR
 * (shadows), and the W3C blend modes (multiply to luminosity) as operators of
 * COMPOSITE_PD and of the paths. A back end that answers PATH_FILL in
 * OGPU_Query takes all of v1.6.
 * v1.7 (23 October 2026) adds what SDL 3.4's renderer asks for beyond SDL 2:
 * its premultiplied blend modes (BLEND_PREMUL, ADD_PREMUL) wherever v1.2's
 * blend modes are taken; the PIXELART filter (SDL_SCALEMODE_PIXELART) for
 * COMPOSITE, COMPOSITE_AFFINE and TRIANGLES; TRIANGLES' clamp or repeat on
 * each axis (SDL_SetRenderTextureAddressMode); palette textures (INDEX8
 * surfaces with a PALETTE, read as sources by COMPOSITE, COMPOSITE_AFFINE
 * and TRIANGLES); the byte orders RGBA32 and ABGR32; and for YUV and
 * YUV_SCALED the P010 layout and the BT.709 full range and BT.2020 colour
 * spaces. A back end that answers PALETTE in OGPU_Query takes all of v1.7;
 * one that doesn't refuses each of these as UNSUPPORTED, or, for an opcode
 * it doesn't know, BADOP.
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
#define OGPU_STREAM_MINOR   7

#define OGPU_HDR(op, words)     (((unsigned long)(op) << 16) | ((unsigned long)(words) & 0xFFFFUL))
#define OGPU_HDR_OP(w)          ((unsigned)((w) >> 16))
#define OGPU_HDR_WORDS(w)       ((unsigned)((w) & 0xFFFFUL))
#define OGPU_XY(x, y)           ((((unsigned long)(x) & 0xFFFFUL) << 16) | ((unsigned long)(y) & 0xFFFFUL))

/* Pixel formats (surfaces and PIXELS sources). */
#define OGPU_FMT_CLUT8          1   /* 1 byte: a pen */
#define OGPU_FMT_RGB565         2   /* 2 bytes, high byte first */
#define OGPU_FMT_ARGB32         3   /* 4 bytes: A, R, G, B */
#define OGPU_FMT_INDEX8         4   /* 1 byte, looked up in a 256-entry ARGB32 table: PIXELS' table, or
                                       from v1.7 a surface's PALETTE (drawn into, it takes indexes
                                       as CLUT8 takes pens: PALETTE below) */
#define OGPU_FMT_A8             5   /* v1.1: 1 byte: coverage (alpha) 0-255, no colour */
/* v1.2: the screens of real cards and of a PiStorm's Picasso96, as targets
 * and sources; ACRTG needs only RGB565 and ARGB32. */
#define OGPU_FMT_RGB565PC       6   /* 2 bytes, low byte first */
#define OGPU_FMT_RGB555         7   /* 2 bytes, high byte first: 0RRRRRGG GGGBBBBB */
#define OGPU_FMT_RGB555PC       8   /* 2 bytes, low byte first */
#define OGPU_FMT_BGRA32         9   /* 4 bytes: B, G, R, A */
/* v1.3: ARGB32 without the alpha. Every write sets X to 0 and every read
 * takes the pixel as opaque, so a PIXELS of ARGB32 (or INDEX8) into an
 * XRGB32 target copies the colour and forces the top byte to 0 (an XRGB32
 * source's X is cleared too). COPY, which moves raw pixels, keeps them. */
#define OGPU_FMT_XRGB32         10  /* 4 bytes: X (0), R, G, B */
/* v1.7: the byte orders SDL 3 programs often pick (SDL_PIXELFORMAT_RGBA32,
 * SDL_PIXELFORMAT_ABGR8888 on the Amiga), as sources and targets wherever
 * ARGB32 is, so a streaming texture is drawn as it is, not converted on the
 * 68k first. */
#define OGPU_FMT_RGBA32         11  /* 4 bytes: R, G, B, A */
#define OGPU_FMT_ABGR32         12  /* 4 bytes: A, B, G, R */
#define OGPU_FMT_COUNT          13
/* 16-18 (Z16, Z32, S8: depth and stencil) are stream3d.h's. */

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
#define OGPU_OP_FILL_BLEND      0x0018  /* v1.2: xy, wh, colour (ARGB32), blend           5  */
#define OGPU_OP_LINES_BLEND     0x0019  /* v1.2: address of points (one OGPU_XY word
                                           each), count, colour (ARGB32), flags          5  */
#define OGPU_OP_POINTS_BLEND    0x001A  /* v1.2: address of points, count, colour, blend  5  */
#define OGPU_OP_COMPOSITE       0x0020  /* source slot, source xy, source wh, xy, wh,
                                           alpha (0-255), flags                          8  */
                                        /* with OGPU_COMP_MASK, then: mask slot, mask xy 10 */
#define OGPU_OP_COMPOSITE_AFFINE 0x0021 /* v1.2: source slot, source xy, source wh,
                                           m00, m01, m02, m10, m11, m12, colour, flags   12 */
#define OGPU_OP_YUV             0x0022  /* v1.2: Y address, Y bytes per row, U (or UV)
                                           address, its bytes per row, V address, its
                                           bytes per row, wh, xy, format                 10 */
#define OGPU_OP_COMPOSITE_PD    0x0023  /* v1.4: operator and flags, source (slot or
                                           colour), source xy, mask (slot or alpha),
                                           mask xy, xy, wh                               8  */
#define OGPU_OP_YUV_SCALED      0x0024  /* v1.5: YUV's six plane words, the picture's
                                           wh, format, then the part's source xy and
                                           wh, and the destination xy and wh (nearest)  13 */
#define OGPU_OP_PATH_FILL       0x0025  /* v1.6: flags (operator, winding, paint), paint,
                                           paint xy, mask slot, mask xy, path address,
                                           path words, m00, m01, m02, m10, m11, m12,
                                           work address, work bytes                     16 */
#define OGPU_OP_PATH_STROKE     0x0026  /* v1.6: PATH_FILL's words, then width, style
                                           (cap, join), miter limit, dash address,
                                           dash count, dash offset                      22 */
#define OGPU_OP_BLUR            0x0027  /* v1.6: xy, wh, radii, work address, work bytes 6  */
#define OGPU_OP_PALETTE         0x0028  /* v1.7: slot, table address (256 ARGB32 words):
                                           an INDEX8 slot's colours; 0 forgets them      3  */
/* 0x0030-0x003F: 3D (TRIANGLES, RENDER3D, TEXENV, TEXTURE, DEPTH, CLEAR3D):
 * include/opengpu/stream3d.h. */
#define OGPU_OP_FENCE           0x00F0  /* id: reported when everything before is done   2  */
/* 0x0080-0x008F: GPU APIs carried through OpenGPU, run by a back end's
 * extension hook (ogpu_core.ext) in stream order; a back end without one
 * reports OGPU_ERR_BADOP, and OGPU_Query answers OGPU_NONE. */
#define OGPU_OP_VIRGL           0x0080  /* request address, request bytes: a block of
                                           ACVirgl commands for the host's virglrenderer 3  */
#define OGPU_OP_EXT_FIRST       0x0080
#define OGPU_OP_EXT_LAST        0x008F

/* OGPU_OP_COMPOSITE flags. */
#define OGPU_COMP_SRCALPHA      1   /* use the source's own alpha (ARGB32 sources) */
#define OGPU_COMP_BILINEAR      2   /* filter when scaling; nearest otherwise */
/* v1.1. Coverage (each pixel's source alpha times alpha, and times the mask)
 * is applied with OVER unless ADD or IN is set. */
#define OGPU_COMP_MASK          4   /* an A8 surface's coverage, read 1:1 against the destination rect */
#define OGPU_COMP_ADD           8   /* dst + src * coverage, each channel saturating at 255 */
#define OGPU_COMP_IN            16  /* dst * coverage: clips what is already there */
/* v1.7: SDL 3's pixel-art scaling (SDL_SCALEMODE_PIXELART), instead of
 * nearest or BILINEAR: see OGPU_AFF_PIXELART. */
#define OGPU_COMP_PIXELART      32
/* v1.2: one of the blend modes below, in bits 8-10, instead of OVER, ADD or IN. */
#define OGPU_COMP_BLENDMODE     128
#define OGPU_COMP_MODE(m)       ((unsigned long)(m) << 8)
#define OGPU_COMP_MODE_OF(f)    ((int)(((f) >> 8) & 7))

/* v1.2: blend modes, as SDL's (colours not premultiplied). FILL_BLEND,
 * POINTS_BLEND and COMPOSITE_AFFINE take one in their flags' bits 0-2, as
 * TRIANGLES does (stream3d.h's OGPU_TRI_BLEND_ are the same numbers); LINES_BLEND
 * too, beside its own flags; COMPOSITE with OGPU_COMP_BLENDMODE in bits 8-10.
 * The source's alpha is its own (with OGPU_COMP_SRCALPHA, where a source has
 * one) times the colour's or the alpha word's, times any mask. */
#define OGPU_BLEND_NONE         0   /* dst = src */
#define OGPU_BLEND_BLEND        1   /* dstRGB = srcRGB * srcA + dstRGB * (1 - srcA); dstA = srcA + dstA * (1 - srcA) */
#define OGPU_BLEND_ADD          2   /* dstRGB = dstRGB + srcRGB * srcA, saturating; dstA kept */
#define OGPU_BLEND_MOD          3   /* dstRGB = srcRGB * dstRGB; dstA kept */
#define OGPU_BLEND_MUL          4   /* dstRGB = srcRGB * dstRGB + dstRGB * (1 - srcA), saturating; dstA kept */
/* v1.7: SDL 3's premultiplied modes. The source's colour is taken as
 * premultiplied: it is times the colour (and the mask) as in the modes
 * above, but not times the alpha, which only scales what of the destination
 * shows through (as SDL 3's software renderer and its GPU renderers do: an
 * alpha modulation alone doesn't fade a premultiplied picture). */
#define OGPU_BLEND_BLEND_PREMUL 5   /* dstRGBA = srcRGBA + dstRGBA * (1 - srcA), saturating */
#define OGPU_BLEND_ADD_PREMUL   6   /* dstRGB = srcRGB + dstRGB, saturating; dstA kept */
#define OGPU_BLEND_LAST         6   /* the highest a stream may carry (7 is UNSUPPORTED) */
#define OGPU_BLEND_MASK         7

/* v1.4: COMPOSITE_PD. Colours are premultiplied (ARGB32's R, G and B are
 * already times A). The operator is in bits 0-7 of the first word, with
 * pixman's numbers. For each target pixel d in xy, wh (inside the clip):
 *   s = the source pixel at (source xy + offset), or the solid colour; a
 *       pixel outside the source is 0 unless SRC_REPEAT tiles it. XRGB32
 *       and the 16-bit formats read with alpha 255; A8 as (a, 0, 0, 0).
 *   with a mask: m = the mask pixel's alpha (an A8 or ARGB32 slot, 0 outside
 *       unless MASK_REPEAT), or the solid alpha; then each channel of s is
 *       s * m / 255, rounded.
 *   d = s * Fa + d * Fb, each product x * y / 255 rounded, the sum saturating
 *       at 255, with Fa and Fb by the operator (as = s's alpha, ad = d's):
 *       CLEAR 0, 0; SRC 1, 0; DST 0, 1; OVER 1, 1 - as; OVER_REVERSE 1 - ad, 1;
 *       IN ad, 0; IN_REVERSE 0, as; OUT 1 - ad, 0; OUT_REVERSE 0, 1 - as;
 *       ATOP ad, 1 - as; ATOP_REVERSE 1 - ad, as; XOR 1 - ad, 1 - as; ADD 1, 1.
 *   The pixel is then stored in the target's format (5 and 6-bit channels
 *   truncated, XRGB32's X as 0), as pixman stores it. */
#define OGPU_PD_CLEAR           0
#define OGPU_PD_SRC             1
#define OGPU_PD_DST             2
#define OGPU_PD_OVER            3
#define OGPU_PD_OVER_REVERSE    4
#define OGPU_PD_IN              5
#define OGPU_PD_IN_REVERSE      6
#define OGPU_PD_OUT             7
#define OGPU_PD_OUT_REVERSE     8
#define OGPU_PD_ATOP            9
#define OGPU_PD_ATOP_REVERSE    10
#define OGPU_PD_XOR             11
#define OGPU_PD_ADD             12
#define OGPU_PD_OPS             13      /* operators 0 .. OGPU_PD_OPS - 1, and from v1.6 the blend modes below */
#define OGPU_PD_SOLID           0x0100  /* the source word is a premultiplied ARGB32 colour, not a slot */
#define OGPU_PD_MASK            0x0200  /* a mask: the mask word is a slot (A8 or ARGB32) */
#define OGPU_PD_MASK_SOLID      0x0400  /* a mask: the mask word's low byte is a constant alpha */
#define OGPU_PD_SRC_REPEAT      0x0800  /* the source tiles (pixman's REPEAT_NORMAL) */
#define OGPU_PD_MASK_REPEAT     0x1000  /* the mask tiles */

/* v1.6: the W3C blend modes (Compositing and Blending Level 1), with pixman's
 * numbers, as operators of COMPOSITE_PD, PATH_FILL and PATH_STROKE. Each is
 * source-over with the colour mixed by the mode B:
 *   co = cs (1 - ad) + cd (1 - as) + as ad B(Cd, Cs),  ao = as + ad - as ad
 * on premultiplied channels (Cs = cs / as and Cd = cd / ad are the
 * unpremultiplied colours), in integers: B's products exact, co rounded once
 * (to within 1 of the real formula; soft-light's square root and the HSL
 * modes' divisions within 2). The HSL modes use the spec's luminosity
 * 0.3 R + 0.59 G + 0.11 B and its ClipColor. */
#define OGPU_BLEND_MULTIPLY     0x30
#define OGPU_BLEND_SCREEN       0x31
#define OGPU_BLEND_OVERLAY      0x32
#define OGPU_BLEND_DARKEN       0x33
#define OGPU_BLEND_LIGHTEN      0x34
#define OGPU_BLEND_COLOR_DODGE  0x35
#define OGPU_BLEND_COLOR_BURN   0x36
#define OGPU_BLEND_HARD_LIGHT   0x37
#define OGPU_BLEND_SOFT_LIGHT   0x38
#define OGPU_BLEND_DIFFERENCE   0x39
#define OGPU_BLEND_EXCLUSION    0x3A
#define OGPU_BLEND_HUE          0x3B
#define OGPU_BLEND_SATURATION   0x3C
#define OGPU_BLEND_COLOR        0x3D
#define OGPU_BLEND_LUMINOSITY   0x3E

/* v1.6: PATH_FILL and PATH_STROKE. Coordinates are device pixels: the
 * target's, with no picture drawn larger and scaled down.
 *   The path is words in memory (path address, path words): each element a
 *   verb, then its points, each an x and a y word in 24.8 (signed):
 *   MOVE 1 point, LINE 1, QUAD 2 (control, end), CUBIC 3, CLOSE none; a
 *   path that doesn't start with MOVE starts at (0, 0).
 *   A point (x, y) is drawn at x' = m00 x + m01 y + m02, y' = m10 x + m11 y
 *   + m12 (the matrix 16.16, as COMPOSITE_AFFINE's), y down.
 *   PATH_FILL closes each subpath and fills it, non-zero unless EVENODD.
 *   Coverage is the exact area of each pixel inside the path (curves cut
 *   into lines within 1/16 pixel), 0-255.
 *   PATH_STROKE fills the outline of the path's stroke, non-zero: width (16.16,
 *   in the path's units, so the matrix scales it), the cap and join in the
 *   style word, the miter limit (16.16, as canvas's miterLimit: a miter
 *   longer than limit times half the width is bevelled), and dashes: dash
 *   count 16.16 lengths at dash address (on, off, on, ...; an odd count is
 *   used twice, as canvas does; 0 or a zero total: solid), started dash
 *   offset into the pattern at each subpath. A dashed subpath's pieces each
 *   have caps, but a closed one on at its start and its end has one piece
 *   there, joined where it starts; an undashed closed one has joins all round.
 *   The paint, for each pixel's centre: a premultiplied ARGB32 colour (the
 *   paint word), a slot (PATH_SLOT: the slot's pixel (x - px, y - py) for
 *   paint xy px, py; outside it 0, or tiled with PATH_REPEAT), or a gradient
 *   (PATH_GRADIENT: the paint word is its address, below), or a picture
 *   under a matrix (PATH_PATTERN: the paint word is a pattern's address,
 *   below: drawImage and patterns with a transform). With PATH_MASK,
 *   the coverage is times the A8 or ARGB32 mask slot's alpha at (x - mx, y -
 *   my) (0 outside): a clip path drawn into an A8 surface first. To clip by
 *   two paths, draw the second into the mask with IN.
 *   Then, as COMPOSITE_PD with that coverage as its mask: s = paint times
 *   coverage, d = operator(s, d), stored in the target's format (any but
 *   CLUT8; an A8 target keeps only alpha, so a clip mask is drawn as one).
 *   The operators that change a pixel s = 0 doesn't (CLEAR, SRC, IN,
 *   IN_REVERSE, OUT, ATOP_REVERSE) act on the whole drawable rectangle, as
 *   canvas's do; the rest only where the path is.
 *   work address and work bytes: memory the back end may use while it draws
 *   (aligned on 4 bytes, not a surface): at least 32 bytes per pixel of the
 *   target's width, more draws in fewer passes (32 x width x height: one).
 *   The CPU back end resolves the winding rule per quarter of a pixel's
 *   height: where two pieces of a path overlap inside a pixel (a
 *   self-crossing, a stroke's inner join that can't meet, dashes whose caps
 *   overlap), that pixel may be off by up to a quarter of it. */
#define OGPU_PATH_MOVE          0
#define OGPU_PATH_LINE          1
#define OGPU_PATH_QUAD          2
#define OGPU_PATH_CUBIC         3
#define OGPU_PATH_CLOSE         4
#define OGPU_PATH_EVENODD       0x0100  /* the even-odd rule (else non-zero) */
#define OGPU_PATH_SLOT          0x0200  /* the paint word is a slot */
#define OGPU_PATH_GRADIENT      0x0400  /* the paint word is a gradient's address */
#define OGPU_PATH_REPEAT        0x0800  /* the slot tiles */
#define OGPU_PATH_MASK          0x1000  /* the mask slot's alpha multiplies the coverage */
#define OGPU_PATH_PATTERN       0x2000  /* the paint word is a pattern's address */
/* PATH_STROKE's style word: a cap in bits 0-3, a join in bits 4-7. */
#define OGPU_CAP_BUTT           0
#define OGPU_CAP_ROUND          1
#define OGPU_CAP_SQUARE         2
#define OGPU_JOIN_MITER         0x00
#define OGPU_JOIN_ROUND         0x10
#define OGPU_JOIN_BEVEL         0x20

/* v1.6: a gradient in memory, words: kind and spread (kind in bits 0-7,
 * spread in 8-15), the stop count n (1 or more), the matrix m00, m01, m02,
 * m10, m11, m12 (16.16, the gradient's space to the target, as a path's),
 * six geometry words (16.16, in the gradient's space), then n stops, each
 * an offset (16.16, 0 to 0x10000, not decreasing) and a premultiplied
 * ARGB32 colour: 14 + 2n words.
 *   LINEAR x0, y0, x1, y1: t is how far the point is along (x0, y0)-(x1, y1).
 *   RADIAL x0, y0, r0, x1, y1, r1: two circles, as canvas's and pixman's: t is
 *     the largest with the point on the circle between them at t, its radius
 *     r0 + t (r1 - r0) not negative; no such t (or two equal circles): 0.
 *   CONIC cx, cy, angle (turns, 0x10000 a whole one): t is the angle from
 *     the start angle, clockwise (y down) as canvas's createConicGradient, in
 *     turns; CSS's conic-gradient starts at the top: its angle less a quarter.
 * The colour at t, spread (PAD, REPEAT, REFLECT) into 0..1: the stops'
 * colours mixed linearly in premultiplied form, as CSS and canvas say (a stop
 * before the first is the first colour, after the last the last; two stops at
 * one offset are a sharp change). */
#define OGPU_GRAD_LINEAR        1
#define OGPU_GRAD_RADIAL        2
#define OGPU_GRAD_CONIC         3
#define OGPU_SPREAD_PAD         0
#define OGPU_SPREAD_REPEAT      1
#define OGPU_SPREAD_REFLECT     2
#define OGPU_GRAD_WORDS(n)      (14 + 2 * (n))

/* v1.6: a pattern in memory, OGPU_PATTERN_WORDS words: a slot, flags (an
 * edge in bits 0-1, BILINEAR), its rectangle's xy and wh (wh 0: the whole
 * slot), the matrix m00, m01, m02, m10, m11, m12 (16.16, the rectangle's
 * pixels to the target, as a path's), and an alpha (0-255, times every
 * pixel). A pixel's centre is taken back through the matrix into the
 * rectangle: nearest, or bilinear between the four round it; a point
 * outside it is 0 (NONE), the nearest edge pixel (PAD), the picture tiled
 * (REPEAT) or mirrored (REFLECT). The pixels are premultiplied (XRGB32 and
 * the 16-bit formats opaque, A8 as alpha). So an image drawn with a
 * transform and any operator is a PATH_FILL of its rectangle's path, under
 * the same matrix, painted with its pattern; a tiled background is the area
 * to fill, with REPEAT. */
#define OGPU_PATTERN_NONE       0
#define OGPU_PATTERN_PAD        1
#define OGPU_PATTERN_REPEAT     2
#define OGPU_PATTERN_REFLECT    3
#define OGPU_PATTERN_BILINEAR   0x10
#define OGPU_PATTERN_WORDS      11

/* WebCore's other compositing operators, as its cairo port draws them:
 * PlusLighter is OGPU_PD_ADD, PlusDarker OGPU_BLEND_DARKEN, Difference
 * OGPU_BLEND_DIFFERENCE. A clip stack is A8 masks: each clip path drawn
 * into the mask with OGPU_PD_IN (PATH_FILL), or a mask composited into
 * another with OGPU_PD_IN (COMPOSITE_PD), then PATH_MASK or COMPOSITE_PD's
 * mask when drawing. */
#define OGPU_PD_PLUS_LIGHTER    OGPU_PD_ADD
#define OGPU_PD_PLUS_DARKER     OGPU_BLEND_DARKEN

/* v1.6: BLUR. The target's xy, wh (inside the clip) blurred in place, as a
 * Gaussian of standard deviation radius / 2 (canvas's shadowBlur and CSS's
 * box-shadow blur radius): three box blurs each way, of the sizes SVG's
 * feGaussianBlur gives. The radii, x in the top 16 bits and y in the
 * bottom, are 12.4 pixels. Pixels outside the rectangle count as 0, so a
 * shadow's rectangle has room round it. A8 or a 32-bit format (each byte
 * alike: premultiplied ARGB32); work bytes at least 2 x 4 x the larger of w
 * and h. */

/* v1.7: PALETTE. SURFACE takes INDEX8 from v1.7. Drawn into, an INDEX8 slot
 * takes indexes as a CLUT8 slot takes pens, and OGPU_Query answers for it
 * as for CLUT8: FILL, INVERT, TEMPLATE, PATTERN and LINE write indexes,
 * COPY moves them from another INDEX8 slot, and PIXELS takes pens (CLUT8)
 * or indexes (INDEX8 with no table); a command that writes colours or
 * blends (COMPOSITE, MASK, the _BLEND commands, COMPOSITE_PD, the paths,
 * YUV, TRIANGLES, PIXELS of colours) says UNSUPPORTED. Its pixels are indexes into
 * the slot's palette, 256 ARGB32 words at the table address (as PIXELS'
 * table; colours not premultiplied, unless the blend mode says they are),
 * read whenever the slot is drawn from, so a program that changes the table
 * between batches (palette cycling) changes the picture with no upload.
 * A SURFACE forgets the slot's palette; an INDEX8 slot read with none says
 * UNSUPPORTED. COMPOSITE, COMPOSITE_AFFINE and TRIANGLES (as a texture slot)
 * read it as the ARGB32 picture the table makes, filters included (bilinear
 * mixes the colours, not the indexes); COPY moves its indexes to another
 * INDEX8 slot. */

/* v1.2: LINES_BLEND's flags, above its blend mode. */
#define OGPU_LINES_STRIP        8   /* each point joins the next (else pairs: 0-1, 2-3, ...) */
#define OGPU_LINES_LAST         16  /* draw each line's last point (else it is left out, so
                                       a strip's joints are blended once) */

/* v1.2: COMPOSITE_AFFINE. The matrix, 16.16 signed, takes a point of the
 * source (from its corner at source xy) to the target:
 *   x = m00 u + m01 v + m02,  y = m10 u + m11 v + m12.
 * Each target pixel whose centre maps inside the source rectangle is drawn,
 * from the source pixel there (or the four round it, with BILINEAR), times
 * the colour (ARGB32; its alpha is the global alpha; 0xFFFFFFFF changes
 * nothing). The matrix's own scale is at most 127 either way. Flags: a blend
 * mode in bits 0-2, OGPU_COMP_SRCALPHA (8 here: the source's own alpha),
 * OGPU_COMP_BILINEAR (16 here). */
#define OGPU_AFF_SRCALPHA       8
#define OGPU_AFF_BILINEAR       16
/* v1.7: PIXELART, SDL 3's pixel-art scaling: each pixel's centre, at source
 * position u (texels, centres at +0.5), is drawn from texels n and n + 1
 * mixed by smoothstep(1 - b, 1, fract(t)), where t = u - b / 2, n = floor(t)
 * and b is how many texels one target pixel spans that way (at most 1):
 * sharp edges with one anti-aliased pixel at each texel's border when a
 * picture is enlarged, like bilinear when it is shrunk. The same each way
 * (v). BILINEAR and PIXELART together are UNSUPPORTED. */
#define OGPU_AFF_PIXELART       32

/* v1.2: YUV's format word: the layout in bits 0-7, the colour space in bits
 * 8-15. Planar layouts give the Y, U and V planes' addresses (YV12 is I420
 * with its planes named in the file's order: give U's and V's addresses as
 * they are); NV12 and NV21 give Y and the interleaved UV (V's address is 0);
 * packed layouts give their one plane as Y. Drawn 1:1 into the target
 * (RGB565 or ARGB32 and the v1.2 formats) at xy, inside the clip; wh is the
 * picture's size, even. */
#define OGPU_YUV_I420           1
#define OGPU_YUV_YV12           2
#define OGPU_YUV_NV12           3
#define OGPU_YUV_NV21           4
#define OGPU_YUV_YUY2           5
#define OGPU_YUV_UYVY           6
#define OGPU_YUV_YVYU           7
/* v1.7: P010, NV12 with 16-bit samples (big-endian words, the value in the
 * top 10 bits, as SDL_PIXELFORMAT_P010 on the Amiga): Y and the interleaved
 * UV planes, each row's bytes per row at least twice NV12's. */
#define OGPU_YUV_P010           8
#define OGPU_YUV_BT601          0   /* video range, 16-235 */
#define OGPU_YUV_BT709          1   /* video range */
#define OGPU_YUV_JPEG           2   /* full range (BT.601's colours) */
/* v1.7: SDL 3's other YUV colour spaces. */
#define OGPU_YUV_BT709_FULL     3   /* full range */
#define OGPU_YUV_BT2020         4   /* video range */
#define OGPU_YUV_BT2020_FULL    5   /* full range */
#define OGPU_YUV_FORMAT(layout, space) ((unsigned long)(layout) | ((unsigned long)(space) << 8))

#define OGPU_MAX_SLOTS          16
#define OGPU_MAX_SIZE           16384   /* widths and heights */

/* Back end results. */
#define OGPU_OK                 0
#define OGPU_ERR_BADOP          -1  /* an opcode this back end doesn't know (skipped) */
#define OGPU_ERR_BADLEN         -2  /* a length that doesn't fit the command or the stream (stops) */
#define OGPU_ERR_NOSURFACE      -3  /* a slot that wasn't described, or no target */
#define OGPU_ERR_NOMAP          -4  /* an address the back end can't reach */
#define OGPU_ERR_UNSUPPORTED    -5  /* a format pair the command can't do */
#define OGPU_ERR_DEVICE         -6  /* the GPU failed: the batch may be partly drawn (later ones run on the CPU) */
#define OGPU_ERR_EXPIRED        -7  /* OGPU_Wait: the fence is done, but too old for its result to be kept */

/* What a back end can do, per operation and target format (OGPU_Query). */
#define OGPU_NONE               0
#define OGPU_PARTIAL            1
#define OGPU_FULL               2

#endif
