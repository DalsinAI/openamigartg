/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * 3D scenes for comparing a back end's 3D range with the core's (host only):
 *  - random scenes: every mode stream v1.2's 3D range has (blends, logic
 *    ops, stencil, chroma, fog, stipple, colour masks, every texture format,
 *    filter and wrap, mip levels, both TRIANGLES forms, CLUT8 targets),
 *    with the target, Z and stencil buffers, textures and vertices each in
 *    video RAM or elsewhere, and unaligned rows;
 *  - bench scenes shaped like the programs we measure (W3DTest, Cow3D,
 *    MGLTest): their sizes, triangle counts and modes, with Warp3D's memory
 *    (the target in video RAM, Z buffer, textures and vertices in Fast RAM);
 *    and the 640x480 one without perspective and fog, and without a
 *    texture, to show what each costs.
 * A scene writes its data into two regions and returns its batch (valid
 * until the next call). Built the same way twice, a scene starts from the
 * same memory, so two back ends can be compared byte for byte. */
#ifndef OGPU_SCENES3D_H
#define OGPU_SCENES3D_H

#include "../library/opengpu/ogpu_core.h"

#define OGPU_SCENE3D_REGION 0x200000L       /* bytes each region needs */

struct ogpu_scene3d_env {
    ogpu_u8 *mem[2];                        /* [0] video RAM, [1] other memory (Fast RAM) */
    unsigned long addr[2];                  /* the addresses the stream knows them by */
};

/* A random scene from seed. */
const unsigned char *ogpu_scene3d_random(struct ogpu_scene3d_env *e, unsigned long seed, long *words);

/* Bench scene `which` (0 .. OGPU_SCENE3D_BENCHES - 1), its name in *name. */
#define OGPU_SCENE3D_BENCHES 8
const unsigned char *ogpu_scene3d_bench(struct ogpu_scene3d_env *e, int which, long *words, const char **name);

/* The bench scene's picture: its target's place (region, offset), size and format. */
void ogpu_scene3d_bench_target(int which, int *region, long *offset, int *w, int *h, int *format);

#endif
