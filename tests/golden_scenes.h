/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * OpenGPU's golden scenes (golden_scenes.c). */
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

#endif
