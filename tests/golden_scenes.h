/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * OpenGPU's golden scenes: the 2D ones (golden_scenes.c) and the 3D one
 * (golden_scene_3d.c, host only). */
#ifndef OGPU_GOLDEN_SCENES_H
#define OGPU_GOLDEN_SCENES_H

#include "../library/opengpu/ogpu_core.h"

#define OGPU_SCENE_ARENA 0xC0000L       /* bytes the scenes use */

struct ogpu_scene_env {
    ogpu_u8 *arena;                     /* OGPU_SCENE_ARENA bytes */
    unsigned long addr;                 /* the address a back end knows arena[0] by */
    /* Carry out a batch, done when this returns (or waited for); its first error. */
    long (*run)(void *user, const unsigned char *stream, long words);
    void *user;
    int failures;                       /* batches that didn't run cleanly */
};

unsigned long ogpu_fnv(const ogpu_u8 *p, long n, unsigned long h);
unsigned long ogpu_golden_g1(struct ogpu_scene_env *e);
unsigned long ogpu_golden_v11(struct ogpu_scene_env *e);
unsigned long ogpu_golden_v12(struct ogpu_scene_env *e);

/* The 3D scene (golden_scene_3d.c, golden/opengpu-3d.txt): stream v1.2's 3D
 * range into a 160x120 target at arena + OGPU_GOLDEN_3D_TARGET, ARGB32
 * (fmt 0) then RGB565 (fmt 1). ogpu_golden_3d_batch writes the texture and
 * vertices into the arena and returns the batch for one format (its words in
 * *words; it stays valid until the next call); ogpu_golden_3d_one runs it
 * and adds the picture to the FNV-1a hash h; ogpu_golden_3d does both
 * formats from FNV-1a's start, the golden file's number. */
#define OGPU_GOLDEN_3D_W 160
#define OGPU_GOLDEN_3D_H 120
#define OGPU_GOLDEN_3D_TARGET 0x10000L
const unsigned char *ogpu_golden_3d_batch(struct ogpu_scene_env *e, int fmt, long *words);
unsigned long ogpu_golden_3d_one(struct ogpu_scene_env *e, int fmt, unsigned long h);
unsigned long ogpu_golden_3d(struct ogpu_scene_env *e);

#endif
