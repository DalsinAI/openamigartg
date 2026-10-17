/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's 3D golden scene (golden/opengpu-3d.txt): a textured, fogged,
 * Z-buffered floor and walls in perspective under a see-through Gouraud
 * triangle, in ARGB32 and then RGB565. Built once for the 3D core's test
 * (test_3d.c), the Vulkan back end's (test_opengpu_vk.c) and the Cradle's
 * runtime GPU test, as golden_scenes.c is for the 2D scenes; the scene's
 * memory is the same arena (struct ogpu_scene_env). Host only: it places
 * vertices with the C library's floating point. */
#include <math.h>
#include <string.h>
#include "golden_scenes.h"
#include "../include/opengpu/build3d.h"

#define GW OGPU_GOLDEN_3D_W
#define GH OGPU_GOLDEN_3D_H
#define GT OGPU_GOLDEN_3D_TARGET
#define GZ 0x30000L     /* the Z buffer, Z16 */
#define TEX 0x40000L    /* a 64x64 ARGB32 texture */
#define VB 0x50000L     /* vertices */
#define A(off) (e->addr + (unsigned long)(off))

/* A 3D vertex record: x, y, ARGB, u, v, z, w, spec (the full layout). */
#define LAY_FULL (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC)
#define VSIZE 32
struct V { double x, y, u, v, z, w; unsigned long c, s; };

static ogpu_u8 *arena;
static unsigned char sbuf[1 << 14];
static struct OGPUBatch B;
static unsigned long R[OGPU_R3D_ARGS];

static void wr32(long a, unsigned long v) {
    ogpu_u8 *p = arena + a;
    p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v;
}
static unsigned long fbits(float f) { unsigned int u; memcpy(&u, &f, 4); return (unsigned long)u; }
static long fx16(double v) { return (long)floor(v * 65536.0 + 0.5); }
static void put_vertex(long at, const struct V *v) {
    wr32(at, (unsigned long)fx16(v->x) & 0xFFFFFFFFUL); wr32(at + 4, (unsigned long)fx16(v->y) & 0xFFFFFFFFUL);
    wr32(at + 8, v->c); wr32(at + 12, (unsigned long)fx16(v->u) & 0xFFFFFFFFUL); wr32(at + 16, (unsigned long)fx16(v->v) & 0xFFFFFFFFUL);
    wr32(at + 20, v->z >= 1.0 ? 0xFFFFFFFFUL : (unsigned long)(v->z * 4294967296.0));
    wr32(at + 24, fbits((float)v->w)); wr32(at + 28, v->s);
}

/* RENDER3D's arguments as a new 3D state has them. */
static void r3d_defaults(void) {
    memset(R, 0, sizeof R);
    R[0] = OGPU_R3D_GOURAUD;
    R[1] = 2;                               /* Z LESS */
    R[2] = 8;                               /* alpha ALWAYS */
    R[3] = 2 | (1UL << 16);                 /* ONE, ZERO */
    R[5] = 0xFF0F;                          /* every channel, every pen bit */
    R[6] = 4;                               /* COPY */
    R[12] = 2 | (0xFFUL << 16);             /* stencil ALWAYS, mask 255 */
    R[13] = 1 | (1UL << 8) | (1UL << 16) | (0xFFUL << 24);
    R[14] = 1;                              /* chroma NONE */
    R[17] = 0xFFFFFFFFUL;                   /* current colour */
}

static void make_texture(long at, int w, int h) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            wr32(at + (long)(y * w + x) * 4, 0xFF000000UL | ((unsigned long)(x * 255 / (w - 1)) << 16)
                 | ((unsigned long)(y * 255 / (h - 1)) << 8) | (((x / 4 + y / 4) & 1) ? 0xFF : 0));
}

const unsigned char *ogpu_golden_3d_batch(struct ogpu_scene_env *e, int fmt, long *words) {
    int k, x;
    int f = fmt ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32, bpp = fmt ? 2 : 4;
    long n = 0;
    arena = e->arena;
    make_texture(TEX, 64, 64);
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_surface(&B, 0, A(GT), (unsigned long)GW * bpp, GW, GH, f);
    ogpu_target(&B, 0);
    ogpu_fill(&B, 0, 0, GW, GH, fmt ? 0x18C3 : 0xFF202830UL);
    ogpu_depth(&B, OGPU_KIND_DEPTH, A(GZ), GW * 2, GW, GH, OGPU_FMT_Z16);
    ogpu_clear3d(&B, OGPU_KIND_DEPTH, 0, 0, GW, GH, 0xFFFFFFFFUL);
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | OGPU_R3D_TEXTURE | OGPU_R3D_PERSPECTIVE | OGPU_R3D_FOG;
    R[7] = OGPU_FOG_LINEAR; R[8] = 0xFF202830UL; R[9] = fbits(0.9f); R[10] = fbits(0.15f);
    ogpu_render3d(&B, R);
    ogpu_texenv(&B, 0, OGPU_ENV_MODULATE, 0);
    ogpu_texture(&B, 0, A(TEX), 64 * 4, 64, 64, OGPU_FMT_ARGB32, 0, 1, 2, 2, 1, 1, 0, 1, 0);
    /* a floor into the distance and a row of slanted walls */
    for (k = 0; k < 8; k++) {
        double z0 = 1 + k * 1.5, z1 = z0 + 1.5;
        double xl = 80 - 120 / z0, xr = 80 + 120 / z0, yl = 60 + 60 / z0;
        double xl1 = 80 - 120 / z1, xr1 = 80 + 120 / z1, yl1 = 60 + 60 / z1;
        struct V v[4];
        memset(v, 0, sizeof v);
        v[0].x = xl; v[0].y = yl; v[0].w = 1 / z0; v[0].u = 0; v[0].v = k;
        v[1].x = xr; v[1].y = yl; v[1].w = 1 / z0; v[1].u = 3; v[1].v = k;
        v[2].x = xl1; v[2].y = yl1; v[2].w = 1 / z1; v[2].u = 0; v[2].v = k + 1;
        v[3].x = xr1; v[3].y = yl1; v[3].w = 1 / z1; v[3].u = 3; v[3].v = k + 1;
        for (x = 0; x < 4; x++) {
            v[x].z = 1 - v[x].w; v[x].c = x & 1 ? 0xFFFFE0C0UL : 0xFFC0E0FFUL; v[x].s = 0xFF000000UL;
            put_vertex(VB + (n + x) * VSIZE, &v[x]);
        }
        ogpu_triangles3d(&B, A(VB + n * VSIZE), 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
        n += 4;
    }
    for (k = 0; k < 6; k++) {
        double z0 = 1.5 + k * 1.2, z1 = z0 + 0.9, xo = (k & 1) ? 1 : -1;
        struct V v[4];
        memset(v, 0, sizeof v);
        v[0].x = 80 + xo * 70 / z0; v[0].y = 60 - 50 / z0; v[0].w = 1 / z0; v[0].u = 0; v[0].v = 0;
        v[1].x = 80 + xo * 20 / z1; v[1].y = 60 - 50 / z1; v[1].w = 1 / z1; v[1].u = 1; v[1].v = 0;
        v[2].x = 80 + xo * 70 / z0; v[2].y = 60 + 60 / z0; v[2].w = 1 / z0; v[2].u = 0; v[2].v = 1;
        v[3].x = 80 + xo * 20 / z1; v[3].y = 60 + 60 / z1; v[3].w = 1 / z1; v[3].u = 1; v[3].v = 1;
        for (x = 0; x < 4; x++) {
            v[x].z = 1 - v[x].w; v[x].c = 0xFFFFFFFFUL; v[x].s = 0xFF000000UL;
            put_vertex(VB + (n + x) * VSIZE, &v[x]);
        }
        ogpu_triangles3d(&B, A(VB + n * VSIZE), 4, VSIZE, 0, 0, 0, OGPU_TRI_UNIT0, 0, LAY_FULL | OGPU_LAY_STRIP);
        n += 4;
    }
    /* a see-through Gouraud triangle on top */
    r3d_defaults();
    R[0] = OGPU_R3D_GOURAUD | OGPU_R3D_BLEND | OGPU_R3D_ZTEST;
    R[3] = 7 | (8UL << 16);
    ogpu_render3d(&B, R);
    {
        struct V v[3];
        memset(v, 0, sizeof v);
        v[0].x = 20; v[0].y = 10; v[0].c = 0xC0FF4040UL;
        v[1].x = 140; v[1].y = 30; v[1].c = 0x6040FF40UL;
        v[2].x = 70; v[2].y = 110; v[2].c = 0xA04040FFUL;
        for (x = 0; x < 3; x++) { v[x].z = 0.3; v[x].w = 1; put_vertex(VB + (n + x) * VSIZE, &v[x]); }
        ogpu_triangles3d(&B, A(VB + n * VSIZE), 3, VSIZE, 0, 0, 0, OGPU_TRI_NOTEX, 0, LAY_FULL);
    }
    if (B.overflow) e->failures++;
    *words = B.words;
    return sbuf;
}

unsigned long ogpu_golden_3d_one(struct ogpu_scene_env *e, int fmt, unsigned long h) {
    long words, k, bytes = (long)GW * GH * (fmt ? 2 : 4);
    const unsigned char *s = ogpu_golden_3d_batch(e, fmt, &words);
    if (e->run(e->user, s, words) != OGPU_OK) e->failures++;
    for (k = 0; k < bytes; k++) h = ((h ^ e->arena[GT + k]) * 16777619UL) & 0xFFFFFFFFUL;
    return h;
}

unsigned long ogpu_golden_3d(struct ogpu_scene_env *e) {
    return ogpu_golden_3d_one(e, 1, ogpu_golden_3d_one(e, 0, 2166136261UL));
}
