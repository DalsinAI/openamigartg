/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
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
 * results and errors are always the core's.
 *
 * Stream v1.2's 3D range (0x0030-0x003F) runs on that core too, with 3D
 * state of its own, when ogpu_vk.c is built with OGPU_WITH_3D, as
 * ogpu_core.c is for it, and ogpu_3d.c is linked. Without it, those
 * opcodes are OGPU_ERR_BADOP and ogpu_vk_supports answers OGPU_NONE. */
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
     * draws in place: the GPU's memory is exported and mapped over it
     * (VK_KHR_external_memory_fd: RADV, V3DV on the Pi 5), keeping what was
     * there, or else the memory is imported (VK_EXT_external_memory_host:
     * RADV, lavapipe). Mapped first: on daletop's RX 460 a GPU round trip
     * takes about 90 us mapped against 340 us imported.
     * It must be page aligned and vram_size a multiple of the page size.
     * OGPU_VK_HOSTMEM=import or map tries only that way. */
    ogpu_u8 *host_vram;
    /* "map" only, or 0: put the file fd (vram_size bytes from 0) over
     * host_vram, shared, and return 0, as the runtime's JIT does for its own
     * memory (jit_alias) so it knows the pages changed; fd -1 puts plain
     * memory back (the back end then copies the contents in). 0: mmap. */
    int (*place)(void *user, int fd, ogpu_u32 size);
    /* As ogpu_core's ext (OGPU_OP_VIRGL and its range), with user; called
     * once the GPU has finished everything before the command. 0: none. */
    int (*ext)(void *user, int op, const ogpu_u8 *cmd, long words);
    /* Where a command the GPU path takes is drawn: OGPU_VK_ROUTE_COST, on
     * the GPU only where that is faster, its wait included (the back end
     * measures this device's round trip when it starts; the costs a pixel
     * are those measured on daletop's RX 460 and its host); OGPU_VK_ROUTE_GPU,
     * wherever the GPU path takes it; OGPU_VK_ROUTE_CORE, always on the core
     * here. 0 (OGPU_VK_ROUTE_COST) takes the environment's OGPU_VK_ROUTE
     * (cost, gpu or core) when it is set. Results are the same every way. */
    int route;
};
#define OGPU_VK_ROUTE_COST 0
#define OGPU_VK_ROUTE_GPU  1
#define OGPU_VK_ROUTE_CORE 2

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
/* Forget the slots, the target, the clip and the 3D state, as a board
 * reset does (the GPU's work is finished first; video RAM is left as it is). */
void ogpu_vk_reset(struct ogpu_vk *vk);
/* OGPU_FULL, OGPU_PARTIAL or OGPU_NONE for an opcode into a format: what a
 * run carries out, on the GPU or on the core here. As ogpu_core_supports,
 * but OGPU_NONE for the 3D range when built without OGPU_WITH_3D, and for
 * OGPU_OP_EXT_FIRST..LAST (OGPU_OP_VIRGL) OGPU_FULL when the config has an
 * ext, which carries them out (whether it has the API behind one, such as
 * virglrenderer, is the caller's to say), else OGPU_NONE. */
int ogpu_vk_supports(const struct ogpu_vk *vk, int op, int format);
int ogpu_vk_last_error(const struct ogpu_vk *vk);      /* OGPU_OK, or the first error */
long ogpu_vk_error_word(const struct ogpu_vk *vk);
/* 1 once a submit has failed (the device lost; that run's error is
 * OGPU_ERR_DEVICE): from then on the core here draws everything. */
int ogpu_vk_lost(const struct ogpu_vk *vk);

struct ogpu_vk_stats {
    unsigned long gpu;      /* drawing commands the GPU did */
    unsigned long cpu;      /* commands the C core did */
    unsigned long submits;  /* command buffers sent */
    unsigned long routed;   /* of cpu: commands the GPU path could take that cost the core drew */
    unsigned long trip_us;  /* a submit and its wait on this device, measured at the start (0: not measured) */
    int route;              /* OGPU_VK_ROUTE_ in force */
};
void ogpu_vk_stats(const struct ogpu_vk *vk, struct ogpu_vk_stats *s);

#endif
