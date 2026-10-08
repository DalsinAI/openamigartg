/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU stream v1.2: 3D. The ops SDL 2's renderer and Warp3D share,
 * carried out by one rasteriser (library/opengpu/ogpu_3d.c on the CPU; a GPU
 * back end draws the same ops on the host). Agreed on 8 October 2026 with
 * SDL's half (TRIANGLES' 9-word form). stream.h keeps the opcode list; this
 * file defines the layouts of 0x0030-0x0035, the 3D range.
 *
 * Words are big-endian, as in stream.h. "Words" below counts the header.
 *
 * TRIANGLES has two forms:
 *   9 words, SDL's: vertices are x, y (16.16), ARGB32, u, v (16.16, 0..1 over
 *     the texture); colour times texture; blend mode in flags; no state block.
 *   10 words, 3D: word 10 is the vertex layout and primitive; the vertices
 *     carry the extra fields the layout names, and RENDER3D, TEXENV, TEXTURE
 *     and DEPTH say how they are drawn (Warp3D, later GL).
 * Rules for both: pixel centres at +0.5, the top-left fill rule, both
 * windings drawn (culling is the caller's), coordinates within +-16383.
 * 3D pictures are compared with a tolerance (+-2 a channel, edge pixels
 * excepted): a GPU never matches an integer core bit for bit. */
#ifndef OPENGPU_STREAM3D_H
#define OPENGPU_STREAM3D_H

#include "stream.h"

#define OGPU_STREAM_MINOR_3D    2

/* Opcodes (0x0030-0x0035, the 3D range). Arguments after the header:            words */
#define OGPU_OP_TRIANGLES       0x0030  /* vertex address, vertex count, stride (bytes),
                                           index address (0 = none), index count, index
                                           size (1, 2, 4), texture, flags [, layout]   9/10 */
#define OGPU_OP_RENDER3D        0x0031  /* the state block below                        22 */
#define OGPU_OP_TEXENV          0x0032  /* unit, mode, environment colour (ARGB32)       4 */
#define OGPU_OP_TEXTURE         0x0033  /* unit, address, bytes per row, wh, format,
                                           table, generation, filter, wrap, border
                                           colour, levels, then one address for each
                                           level after the first                  12+n-1 */
#define OGPU_OP_DEPTH           0x0034  /* kind, address, bytes per row, wh, format;
                                           address 0 = none                             6 */
#define OGPU_OP_CLEAR3D         0x0035  /* kind, xy, wh, value (inside the clip); with
                                           OGPU_KIND_READBACK, nothing is cleared and
                                           the rectangle is valid in memory at the next
                                           fence (a no-op on the CPU)                   5 */
#define OGPU_OP_3D_FIRST        0x0030
#define OGPU_OP_3D_LAST         0x0035

/* TRIANGLES' texture word. */
#define OGPU_TRI_NOTEX          0xFFFF  /* untextured */
#define OGPU_TRI_UNIT0          0xFFFE  /* TEXTURE unit 0 (3D form); 0..15 is a SURFACE slot */

/* TRIANGLES' flags (both forms; the 3D form uses only BILINEAR and CLAMP for slot textures). */
#define OGPU_TRI_BLEND_MASK     7
#define OGPU_TRI_BLEND_NONE     0       /* copy */
#define OGPU_TRI_BLEND_BLEND    1       /* OVER, not premultiplied */
#define OGPU_TRI_BLEND_ADD      2       /* dst + src * srcA, saturating */
#define OGPU_TRI_BLEND_MOD      3       /* dst * src */
#define OGPU_TRI_BLEND_MUL      4       /* dst * src + dst * (1 - srcA) */
#define OGPU_TRI_BILINEAR       8
#define OGPU_TRI_CLAMP          16      /* clamp, not repeat */

/* TRIANGLES' layout word (3D form): the primitive and the extra vertex
 * fields, which follow x, y, ARGB32, u, v in this order. */
#define OGPU_LAY_LIST           0
#define OGPU_LAY_STRIP          1
#define OGPU_LAY_FAN            2
#define OGPU_LAY_PRIM_MASK      3
#define OGPU_LAY_Z              (1 << 4)    /* + z: u32, 0..0xFFFFFFFF for 0..1 */
#define OGPU_LAY_W              (1 << 5)    /* + w: IEEE754 single (1.0 at the front plane) */
#define OGPU_LAY_SPEC           (1 << 6)    /* + ARGB32: specular RGB; A = vertex fog factor (255 = none) */
#define OGPU_LAY_UV1            (1 << 7)    /* + u1, v1 (16.16), the second unit (reserved) */

/* RENDER3D: 21 argument words.
 *   1 enables (below)              11 fog end         (float)
 *   2 Z compare (1-8)              12 fog density     (float)
 *   3 alpha func | ref << 16       13 stencil func | ref << 8 | mask << 16
 *   4 blend src | dst << 16        14 stencil fail | zfail << 8 | zpass << 16 | write mask << 24
 *   5 blend constant (ARGB32)      15 chroma mode (1-3)
 *   6 colour mask (RGBA bits 0-3)  16 chroma lower (ARGB32)
 *     | pen mask << 8              17 chroma upper (ARGB32)
 *   7 logic op (1-16)              18 current colour (ARGB32), used when not GOURAUD
 *   8 fog mode                     19 CLUT8 targets: RGB555-to-pen table (32768 bytes)
 *   9 fog colour (ARGB32)          20 polygon stipple (32x32 bits, 128 bytes) or 0
 *  10 fog start (float)            21 CLUT8 targets: the palette (256 ARGB32)
 * Compare functions, blend factors, logic ops, stencil functions and ops and
 * chroma modes are numbered as Warp3D numbers them (W3D_Z_*, W3D_A_*, W3D_ZERO..
 * W3D_ONE_MINUS_CONSTANT_ALPHA, W3D_LO_*, W3D_ST_*, W3D_CHROMATEST_*), so no
 * caller translates them. Fog distances are in w-space: 1.0 at the front
 * plane, 0.0 at the back. */
#define OGPU_R3D_PERSPECTIVE    (1 << 0)
#define OGPU_R3D_GOURAUD        (1 << 1)
#define OGPU_R3D_ZTEST          (1 << 2)
#define OGPU_R3D_ZWRITE         (1 << 3)
#define OGPU_R3D_BLEND          (1 << 4)
#define OGPU_R3D_FOG            (1 << 5)
#define OGPU_R3D_ALPHATEST      (1 << 6)
#define OGPU_R3D_STENCIL        (1 << 7)
#define OGPU_R3D_LOGICOP        (1 << 8)
#define OGPU_R3D_CHROMA         (1 << 9)
#define OGPU_R3D_SPECULAR       (1 << 10)
#define OGPU_R3D_DITHER         (1 << 11)   /* may be ignored */
#define OGPU_R3D_STIPPLE        (1 << 12)
#define OGPU_R3D_TEXTURE        (1 << 13)
#define OGPU_R3D_ARGS           21

#define OGPU_FOG_LINEAR         1       /* f = (w - end) / (start - end) */
#define OGPU_FOG_EXP            2       /* f = e^-(density * (1 - w)) */
#define OGPU_FOG_EXP2           3       /* f = e^-(density * (1 - w))^2 */
#define OGPU_FOG_VERTEX         4       /* f from each vertex's SPEC alpha */

/* TEXENV modes (Warp3D's numbers). */
#define OGPU_ENV_REPLACE        1
#define OGPU_ENV_DECAL          2
#define OGPU_ENV_MODULATE       3
#define OGPU_ENV_BLEND          4
#define OGPU_ENV_ADD            5       /* colour + texture */
#define OGPU_ENV_SUB            6       /* colour - texture */
#define OGPU_ENV_OFF            7       /* the unit does nothing */

/* TEXTURE's format word: a pixel format (stream.h: ARGB32, RGB565, INDEX8
 * with its table, A8) in bits 0-7 and what the texels mean in bits 8-15,
 * which decides the environment's sums (as OpenGL's base formats do). */
#define OGPU_TEXBASE_RGBA       0
#define OGPU_TEXBASE_RGB        1       /* alpha ignored (255) */
#define OGPU_TEXBASE_ALPHA      2       /* only A */
#define OGPU_TEXBASE_LUMINANCE  3       /* R is L; alpha ignored */
#define OGPU_TEXBASE_LUM_ALPHA  4       /* R is L, A is A */
#define OGPU_TEXBASE_INTENSITY  5       /* R is I, for colour and alpha */
#define OGPU_TEX_FORMAT(fmt, base)  ((ogpu_u32)(fmt) | ((ogpu_u32)(base) << 8))

/* TEXTURE's filter word: min | mag << 8, Warp3D's numbers, named as
 * OpenGL's (texels, then levels): NEAREST 1, LINEAR 2, NEAREST_MIP_NEAREST 3,
 * NEAREST_MIP_LINEAR 4, LINEAR_MIP_NEAREST 5, LINEAR_MIP_LINEAR 6; mag is
 * NEAREST or LINEAR. The CPU picks the level once a triangle. Wrap word:
 * s | t << 8. */
#define OGPU_WRAP_REPEAT        1
#define OGPU_WRAP_CLAMP         2
#define OGPU_WRAP_BORDER        3       /* outside is the border colour */
#define OGPU_TEX_LEVELS         12      /* up to 2048 x 2048 */
#define OGPU_TEX_UNITS          2

/* DEPTH and CLEAR3D kinds, and the buffers' formats (stream.h leaves 16-18 to them). */
#define OGPU_KIND_DEPTH         0
#define OGPU_KIND_STENCIL       1
#define OGPU_KIND_COLOUR        2       /* with READBACK only: the target */
#define OGPU_KIND_READBACK      0x100   /* CLEAR3D: read back instead of clearing */
#define OGPU_FMT_Z16            16      /* 2 bytes, high first: depth 0..65535 */
#define OGPU_FMT_Z32            17      /* 4 bytes, high first: depth 0..0xFFFFFFFF */
#define OGPU_FMT_S8             18      /* 1 byte: stencil */

#endif
