/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What a command needs of a driver beyond its opcode (opengpu.library
 * 0.19's driver_takes; plain C, so tests/test_sdl3.c checks it on the host).
 * An older driver takes a v1.2 command's opcode but not v1.7's variants of
 * it (SDL 3's blend modes, PIXELART, clamping one way, P010 and the new YUV
 * spaces), nor COMPOSITE_PD with v1.6's blend modes: those need a driver
 * that also answers PALETTE (v1.7) or PATH_FILL (v1.6). SURFACE's new
 * formats are the library's drv_fmt check. */
#ifndef OGPU_VARIANT_H
#define OGPU_VARIANT_H

#include "../../include/opengpu/stream.h"
#include "../../include/opengpu/stream3d.h"

/* Word k after the header of the command at word `at` (len words, header
 * included), or 0 past its end. */
static unsigned long ogpu_arg_word(const unsigned char *s, unsigned long at, unsigned len, int k)
{
    const unsigned char *p = s + (at + 1 + (unsigned long)k) * 4;
    if ((unsigned long)k + 1 >= len) return 0;
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* The opcode a driver must also take for the command's variant, or 0. */
static unsigned ogpu_variant_needs(const unsigned char *s, unsigned long at, unsigned op, unsigned len)
{
    unsigned long f = 0, sp;
    switch (op) {
    case OGPU_OP_FILL_BLEND: case OGPU_OP_POINTS_BLEND: case OGPU_OP_LINES_BLEND:
        f = ogpu_arg_word(s, at, len, 3);
        return (f & OGPU_BLEND_MASK) >= OGPU_BLEND_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE:
        f = ogpu_arg_word(s, at, len, 6);
        return (f & OGPU_COMP_PIXELART) || ((f & OGPU_COMP_BLENDMODE) && OGPU_COMP_MODE_OF(f) >= OGPU_BLEND_BLEND_PREMUL)
               ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE_AFFINE:
        f = ogpu_arg_word(s, at, len, 10);
        return (f & OGPU_AFF_PIXELART) || (f & OGPU_BLEND_MASK) >= OGPU_BLEND_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_YUV: case OGPU_OP_YUV_SCALED:
        f = ogpu_arg_word(s, at, len, op == OGPU_OP_YUV ? 8 : 7);
        sp = (f >> 8) & 255;
        return (f & 255) == OGPU_YUV_P010 || sp > OGPU_YUV_JPEG ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_TRIANGLES:
        if (len != 9) return 0;                 /* the 3D form: no v1.7 variants */
        f = ogpu_arg_word(s, at, len, 7);
        return (f & (OGPU_TRI_PIXELART | OGPU_TRI_CLAMP_U | OGPU_TRI_CLAMP_V))
               || (f & OGPU_TRI_BLEND_MASK) >= OGPU_TRI_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE_PD:
        return (ogpu_arg_word(s, at, len, 0) & 255) >= OGPU_BLEND_MULTIPLY ? OGPU_OP_PATH_FILL : 0;
    }
    return 0;
}

#endif
