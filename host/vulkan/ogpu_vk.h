/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's Vulkan back end (G2, DESIGN.md section 5): an OGPU stream v1
 * carried out on the host's GPU, pixel for pixel as library/opengpu's C core
 * carries it out. The Cradle's runtime runs it behind ACRTG.gpu: the board's
 * video RAM is this back end's Vulkan buffer, so the runtime, the C core and
 * the GPU all draw in the same memory.
 *
 * Every command is decoded and clipped here, by the C core's own rules; the
 * GPU gets one compute dispatch per drawing command (host/vulkan/ogpu.comp).
 * A command the GPU path doesn't take (a target outside video RAM or not
 * aligned to its pixels, a composite that reads what it writes, anything
 * that would be an error) runs on the C core after the GPU has finished, so
 * results and errors are always the core's. */
#ifndef OGPU_VK_H
#define OGPU_VK_H

#include "../../library/opengpu/ogpu_core.h"

struct ogpu_vk;

struct ogpu_vk_config {
    ogpu_u32 vram_base;     /* the Amiga address of video RAM's first byte */
    ogpu_u32 vram_size;     /* bytes, a multiple of 4 */
    ogpu_u32 arena_size;    /* bytes for data uploaded from outside video RAM; 0: 4 MiB */
    /* Memory outside video RAM (bitmaps in Fast RAM, templates, tables):
     * as ogpu_core's map; 0 when the address can't be reached. */
    ogpu_u8 *(*map)(void *user, ogpu_u32 address, ogpu_u32 length);
    void (*fence)(void *user, ogpu_u32 id);
    void *user;
    /* A device whose name contains this, or 0: the environment's
     * OGPU_VK_DEVICE, else a real GPU before a CPU one (lavapipe). */
    const char *device;
    /* Video RAM the caller already has (the runtime's board memory, which
     * its 68k reaches directly), or 0 for the back end's own. The GPU then
     * draws in place: the memory is imported (VK_EXT_external_memory_host,
     * RADV and lavapipe), or the GPU's memory is exported and mapped over it
     * (VK_KHR_external_memory_fd, V3DV on the Pi 5), keeping what was there.
     * It must be page aligned and vram_size a multiple of the page size.
     * OGPU_VK_HOSTMEM=import or map tries only that way. */
    ogpu_u8 *host_vram;
    /* "map" only, or 0: put the file fd (vram_size bytes from 0) over
     * host_vram, shared, and return 0, as the runtime's JIT does for its own
     * memory (jit_alias) so it knows the pages changed; fd -1 puts plain
     * memory back (the back end then copies the contents in). 0: mmap. */
    int (*place)(void *user, int fd, ogpu_u32 size);
};

/* 0 on failure, with the reason in err. */
struct ogpu_vk *ogpu_vk_create(const struct ogpu_vk_config *cfg, char *err, int errlen);
void ogpu_vk_destroy(struct ogpu_vk *vk);

/* Video RAM as the host sees it: vram_size bytes, coherent. Anything the
 * host writes here is seen by the next run; after a run returns, the host
 * sees everything the GPU drew. */
ogpu_u8 *ogpu_vk_vram(struct ogpu_vk *vk);
const char *ogpu_vk_device(struct ogpu_vk *vk);
/* How video RAM reaches the GPU: "own", "import" or "map". */
const char *ogpu_vk_vram_mode(struct ogpu_vk *vk);

/* As ogpu_core_run: carries out `words` words of stream and returns the
 * commands done; the GPU's work is finished when it returns. Slots, the
 * target and the clip carry over from run to run, as the core's do. */
long ogpu_vk_run(struct ogpu_vk *vk, const ogpu_u8 *stream, long words);
/* Forget the slots, the target and the clip, as a board reset does (the
 * GPU's work is finished first; video RAM is left as it is). */
void ogpu_vk_reset(struct ogpu_vk *vk);
int ogpu_vk_last_error(const struct ogpu_vk *vk);      /* OGPU_OK, or the first error */
long ogpu_vk_error_word(const struct ogpu_vk *vk);

struct ogpu_vk_stats {
    unsigned long gpu;      /* drawing commands the GPU did */
    unsigned long cpu;      /* commands the C core did */
    unsigned long submits;  /* command buffers sent */
};
void ogpu_vk_stats(const struct ogpu_vk *vk, struct ogpu_vk_stats *s);

#endif
