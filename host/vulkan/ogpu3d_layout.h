/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The 3D range on the GPU: what the recorder (ogpu3d_rec.c) writes and the
 * shader (ogpu3d.comp, ogpu3d_px.h) reads. Plain #defines, so C and GLSL
 * share this file.
 *
 * Four storage buffers, seen by the shader as 32-bit words; a byte at offset
 * a is bits 8 * (a & 3) of word a >> 2 (the host is little-endian), so
 * pixels keep the Amiga's byte order, as in ogpu.comp:
 *   0  video RAM (the 2D shader's buffer)
 *   1  staging: rows of targets, Z and stencil buffers outside video RAM,
 *      copied in before a submit and back after it
 *   2  the texture cache: each texture's levels, repacked, and its table
 *   3  the submit's data: draw states, triangles, spans and tile lists
 *
 * One dispatch draws a group of TRIANGLES commands into one target: 8x8
 * pixel tiles, one invocation a pixel. Each tile has the list of triangles
 * that touch it, in stream order, and each invocation takes them in turn, so
 * every pixel sees the triangles in the order the core draws them. */
#ifndef OGPU3D_LAYOUT_H
#define OGPU3D_LAYOUT_H

#define OGPU3D_TILE         8           /* tiles are 8 x 8 pixels; a workgroup is one tile */
#define OGPU3D_NONE         0xFFFFFFFFu /* no table, stipple, palette */

/* buffers */
#define B3_VRAM             0u
#define B3_STAGE            1u
#define B3_TEX              2u
#define B3_DATA             3u

/* push constants (words) */
#define P3_TBUF             0           /* the target: its buffer, byte offset, bytes a row, format */
#define P3_TOFF             1
#define P3_TBPR             2
#define P3_TFMT             3
#define P3_ZBUF             4           /* the Z buffer (format 0: none) */
#define P3_ZOFF             5
#define P3_ZBPR             6
#define P3_ZFMT             7
#define P3_SBUF             8           /* the stencil buffer */
#define P3_SOFF             9
#define P3_SBPR             10
#define P3_GX0              11          /* the first tile's top-left pixel */
#define P3_GY0              12
#define P3_GTW              13          /* tiles across */
#define P3_BINS             14          /* word offset of the tile lists in the data */
#define P3_COUNT            16

/* A tile's list: two words at P3_BINS + 2 * tile (first entry's word offset,
 * entries); each entry is a triangle's word offset. */

/* A triangle (word offsets in the data) */
#define TR_STATE            0           /* its draw state's word offset */
#define TR_YS               1           /* rows ystart .. yend - 1 */
#define TR_YE               2
#define TR_RX               3           /* the column its planes start at */
#define TR_SPANS            4           /* word offset: one word a row, x0 | x1 << 16 (x0 <= x < x1) */
#define TR_FLAGS            5           /* TRF_ bits, lfrac << 8, level << 16 */
#define TR_GX               6           /* 13 steps across */
#define TR_GY               19          /* 13 steps down */
#define TR_ROW              32          /* 13 values at (rx, ystart) */
#define TR_WORDS            45
#define TRF_PERSP           1u
#define TRF_BILINEAR        2u

/* The values a triangle carries (ogpu_3d.h's OGPU3D_ order) */
#define AT3_R               0u
#define AT3_G               1u
#define AT3_B               2u
#define AT3_A               3u
#define AT3_SR              4u
#define AT3_SG              5u
#define AT3_SB              6u
#define AT3_F               7u
#define AT3_Z               8u
#define AT3_FI              9u
#define AT3_U               10u
#define AT3_V               11u
#define AT3_Q               12u
#define AT3_N               13

/* A draw state: one a TRIANGLES command (word offsets from its start) */
#define ST_MODE             0           /* M_ bits, worked out as ogpu_3d.c's o3_span does */
#define ST_ENV              1
#define ST_BASE             2
#define ST_ENVC             3           /* environment colour, ARGB32 */
#define ST_BSRC             4
#define ST_BDST             5
#define ST_CUR              6           /* current colour, ARGB32 */
#define ST_FOGC             7           /* fog colour, ARGB32 */
#define ST_KC               8           /* blend constant, ARGB32 */
#define ST_WMASK            9           /* the colour mask as the target's raw bits */
#define ST_ZFUNC            10
#define ST_AFUNC            11
#define ST_AREF             12
#define ST_LOGICOP          13
#define ST_CHLO             14
#define ST_CHHI             15
#define ST_SFUNC            16
#define ST_SREF             17
#define ST_SMASK            18
#define ST_SFAIL            19
#define ST_SZFAIL           20
#define ST_SZPASS           21
#define ST_SWMASK           22
#define ST_FOGTAB           23          /* byte offsets in the data: the fog table (1024 bytes) */
#define ST_STIPPLE          24          /* the stipple (128 bytes) */
#define ST_PEN              25          /* CLUT8: the RGB555-to-pen table (32768 bytes), or OGPU3D_NONE */
#define ST_PAL              26          /* CLUT8: the palette (256 ARGB32), or OGPU3D_NONE */
#define ST_TEXW             27          /* the texture */
#define ST_TEXH             28
#define ST_TEXFMT           29
#define ST_TEXBPP           30
#define ST_WRAPS            31
#define ST_WRAPT            32
#define ST_BORDER           33          /* ARGB32 */
#define ST_TABLE            34          /* byte offset in the texture cache, or OGPU3D_NONE */
#define ST_BLEND            35          /* 1 SRC_ALPHA, 1 - SRC_ALPHA; 2 ONE, ONE; 3 any other */
#define ST_LEVEL            36          /* 12 byte offsets in the texture cache */
#define ST_PITCH            48          /* 12 row lengths there */
#define ST_WORDS            64

#define M_GOURAUD           1u
#define M_TEX               2u
#define M_SPEC              4u
#define M_FOGV              8u          /* fog from each vertex */
#define M_FOGT              16u         /* fog from the table */
#define M_ATEST             32u
#define M_CHROMA            64u
#define M_CHROMA_IN         128u        /* keeps the range (INCLUSIVE), else drops it */
#define M_LOGIC             256u
#define M_BLEND             512u
#define M_ZWRITE            1024u
#define M_ZTEST             2048u       /* a Z buffer to test */
#define M_STENCIL           4096u       /* a stencil buffer to test */
#define M_STIPPLE           8192u
#define M_MASKALL           16384u

#endif
