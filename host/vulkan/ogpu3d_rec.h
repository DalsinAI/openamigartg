/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The 3D range on the GPU, the host's half: TRIANGLES set up by ogpu_3d.c
 * itself (its sink) and written out for ogpu3d.comp (ogpu3d_layout.h), with
 * staging for targets, Z and stencil buffers outside video RAM, a texture
 * cache, and the choice of GPU or core for each command. No Vulkan here: the
 * back end (ogpu_vk.c) gives it the buffers' memory and a way to record a
 * dispatch and to send everything; tests/test_ogpu3d_emul.c runs it with the
 * shader on the CPU.
 *
 * What a run looks like:
 *  - State commands (RENDER3D, TEXENV, TEXTURE, DEPTH) go to the core's 3D
 *    state as before; they change no pixels, so nothing waits.
 *  - TRIANGLES runs ogpu_3d.c's setup with the sink: the sink takes the
 *    draw state, each triangle's planes and its spans, and writes them out;
 *    the shader colours the pixels. Consecutive commands into the same
 *    target are one dispatch (a "group"); everything until the run ends (or
 *    a FENCE, or a command only the core does) is one submit.
 *  - Video RAM is drawn in place. A target, Z or stencil buffer elsewhere
 *    (Fast RAM: Warp3D's Z buffer, an SDL surface) has the rows a command
 *    draws in copied into staging, and back once the GPU has finished, so
 *    memory is right when the run returns, as on the core.
 *  - A texture is copied into the cache once and used again while its
 *    texels are the same: an entry is checked by a 64-bit hash of its
 *    texels (and table) the first time it is used in a run, since the 68k
 *    may have changed them since. (Warp3D's generation would let us skip
 *    the hash, but every texture starts at generation 1, so a texture freed
 *    and another made at the same address would look the same.)
 *  - The first command of a submit goes to the GPU only when that is
 *    quicker than the core, the round trip included; once something waits
 *    to be sent, the rest of the submit goes to the GPU too.
 * Anything the shader would not draw exactly as the core does (a texture
 * format it lacks, a target overlapping its Z buffer, a texture too big for
 * the cache) is declined and drawn by the core, after the GPU has finished. */
#ifndef OGPU3D_REC_H
#define OGPU3D_REC_H

#include "../../library/opengpu/ogpu_3d.h"

struct ogpu3d_rec;

struct ogpu3d_rec_cfg {
    /* The buffers (ogpu3d_layout.h's B3_), as the host sees them. The host
     * reads video RAM and staging and only writes the texture cache and
     * the data, which may be the GPU's own memory. */
    ogpu_u8 *vram;  unsigned long vram_size;
    ogpu_u8 *stage; unsigned long stage_size;
    ogpu_u8 *tex;   unsigned long tex_size;
    ogpu_u8 *data;  unsigned long data_size;
    /* Record one dispatch of ogpu3d.comp: P3_COUNT words of push
     * constants, gx x gy workgroups, after the work recorded before it. */
    void (*dispatch)(void *user, const unsigned int *pc, unsigned gx, unsigned gy);
    /* Send everything recorded and wait for it: calls ogpu3d_rec_close
     * before it sends and ogpu3d_rec_done once the GPU has finished. */
    void (*flush)(void *user);
    /* 1 when GPU work is recorded and not yet sent. */
    int (*pending)(void *user);
    void *user;
    double round_trip_ns;   /* a submit and its wait, with nothing in it */
};

/* 0 when out of memory. OGPU_VK_3D=gpu takes every command the shader can
 * draw to the GPU, =core none; otherwise each goes where it is quicker. */
struct ogpu3d_rec *ogpu3d_rec_create(const struct ogpu3d_rec_cfg *cfg);
void ogpu3d_rec_destroy(struct ogpu3d_rec *r);

/* A TRIANGLES command (header first, `words` long) of the 3D state t:
 * recorded for the GPU, or drawn on the core when that is quicker and
 * nothing waits to be sent. 1 when done, its result in *result; 0 to leave
 * it to the caller, which sends what is recorded and has the core draw it. */
int ogpu3d_rec_triangles(struct ogpu3d_rec *r, struct ogpu3d *t, const ogpu_u8 *cmd, long words, int *result);

/* The back end's flush: before it sends (records the open group's
 * dispatch) and after the GPU has finished (staged rows go back to memory;
 * wait_ns is how long the wait took). */
void ogpu3d_rec_close(struct ogpu3d_rec *r);
void ogpu3d_rec_done(struct ogpu3d_rec *r, double wait_ns);

/* 1 while staged surfaces wait to go back to memory: the caller sends what
 * is recorded before any other drawing command, which might read them. */
int ogpu3d_rec_staged(const struct ogpu3d_rec *r);
/* GPU work the caller recorded (2D) writes these bytes of video RAM. */
void ogpu3d_rec_wrote(struct ogpu3d_rec *r, const ogpu_u8 *p, unsigned long len);
/* The CPU may have written memory (a new run: the 68k; the core drew):
 * cached textures are checked again before their next use. */
void ogpu3d_rec_cpu_wrote(struct ogpu3d_rec *r);

struct ogpu3d_rec_stats {
    unsigned long gpu;          /* TRIANGLES commands drawn on the GPU */
    unsigned long cpu;          /* ... on the core, as quicker */
    unsigned long declined;     /* ... on the core, as the shader can't */
    unsigned long triangles, pixels, groups;
    unsigned long tex_uploads, tex_checks, tex_hits;
    unsigned long staged_rows;
    unsigned long lost;         /* commands missing triangles: host memory ran out */
    double core_px_ns, gpu_px_ns, round_trip_ns;   /* the costs it routes by */
};
void ogpu3d_rec_stats(const struct ogpu3d_rec *r, struct ogpu3d_rec_stats *s);

#endif
