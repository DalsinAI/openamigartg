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
 * Stream v1.2's 3D range (0x0030-0x003F) is carried out with 3D state of
 * the back end's own when ogpu_vk.c is built with OGPU_WITH_3D, as
 * ogpu_core.c is for it, and ogpu_3d.c is linked: TRIANGLES on the GPU
 * (ogpu3d.comp, ogpu3d_rec.h) where that is quicker and the shader draws it
 * exactly as the core does, and on the core otherwise; the rest of the range
 * on the core. OGPU_VK_3D=gpu sends every TRIANGLES the shader can draw to
 * the GPU, =core none. Without OGPU_WITH_3D, those opcodes are
 * OGPU_ERR_BADOP and ogpu_vk_supports answers OGPU_NONE. */
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
    /* "map" only: video RAM's card part (the compositor's step 2b). From
     * card_at to card_end, video RAM is the card's own memory (device-local,
     * reached through the PCI bar) instead of host memory, in one GPU buffer
     * by sparse binding. The CPU writes it well but reads it slowly (7 MB/s
     * against 2.5 GB/s on daletop's RX 460), so a command that reads it is
     * drawn on the GPU whatever the cost (ogpu_vk_stats' card_gpu). Each a
     * multiple of 64 KiB. card_at 0: none, or the environment's
     * OGPU_VK_CARD_AT (bytes). card_end 0: the end of video RAM, or the
     * environment's OGPU_VK_CARD_END. What follows card_end is host memory
     * again: the ACRTG board's drivers keep OpenGPU's ring in the top 256 KiB
     * of video RAM, and the core reads every word of it. Without sparse
     * binding or the memory, there is no card part: ogpu_vk_card_at says
     * what was made. */
    ogpu_u32 card_at, card_end;
    /* With a card part: put size bytes of the file fd, from fd_off, at
     * offset `at` of host_vram, shared, and return 0 (place, a part at a
     * time). 0: mmap. */
    int (*place_part)(void *user, int fd, unsigned long fd_off, ogpu_u32 at, ogpu_u32 size);
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
#define OGPU_VK_COST_KINDS 11       /* ogpu_vk_stats' core_scale */
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
/* How video RAM reaches the GPU: "own", "import", "map", or "map+card"
 * (map with a card part). */
const char *ogpu_vk_vram_mode(struct ogpu_vk *vk);
/* Where video RAM's card part starts (bytes from its start), or 0: none;
 * and where it ends. */
ogpu_u32 ogpu_vk_card_at(const struct ogpu_vk *vk);
ogpu_u32 ogpu_vk_card_end(const struct ogpu_vk *vk);

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

/* Why a drawing command ran on the core (ogpu_vk_stats' why[]): */
#define OGPU_VK_WHY_OP      0   /* its opcode isn't on the GPU path (v1.4's COMPOSITE_PD, v1.5-v1.7's) */
#define OGPU_VK_WHY_TARGET  1   /* its target isn't in video RAM (Fast RAM: SDL's surfaces, backing store),
                                 * or not aligned to its pixels, or INDEX8, or there is none */
#define OGPU_VK_WHY_COST    2   /* routing by cost: quicker on the core */
#define OGPU_VK_WHY_REFUSED 3   /* the GPU command's own checks: a format, a flag, a source over the
                                 * rectangle, a source the arena can't take */
#define OGPU_VK_WHY_LOST    4   /* the device was lost: everything on the core */
#define OGPU_VK_WHY_ROUTE   5   /* OGPU_VK_ROUTE_CORE (AC_OPENGPU=core-like routing) */
#define OGPU_VK_WHY_COUNT   6
#define OGPU_VK_CORE_OPS    32  /* opcodes 0x0010..0x002F */
struct ogpu_vk_stats {
    unsigned long gpu;      /* drawing commands the GPU did (2D) */
    unsigned long cpu;      /* commands the C core did */
    unsigned long submits;  /* command buffers sent */
    unsigned long routed;   /* of cpu: commands the GPU path could take that cost the core drew */
    unsigned long trip_us;  /* a submit and its wait on this device, measured at the start (0: not measured) */
    int route;              /* OGPU_VK_ROUTE_ in force */
    unsigned long lists;    /* composite lists sent (each one dispatch) */
    unsigned long listed;   /* of gpu: commands drawn in those lists */
    /* The 3D range (OGPU_WITH_3D): TRIANGLES commands drawn on the GPU, on
     * the core as quicker there, on the core as the GPU can't draw them
     * exactly; the texture cache; and the costs TRIANGLES is routed by. */
    unsigned long tri_gpu, tri_cpu, tri_declined;
    unsigned long tex_uploads, tex_checks, tex_hits;
    double core_px_ns, gpu_px_ns, round_trip_us;
    int shader3d;           /* 1: TRIANGLES can go to the 3D shader (it was set up) */
    double record_ms;       /* the CPU's time on TRIANGLES the GPU drew */
    double wait_ms;         /* time spent waiting for the GPU (every submit) */
    /* Routing by cost: what each side's work has lately taken over what
     * was reckoned for it (1: as reckoned; the reckoning is scaled by it),
     * the core's by kind of work: fills, copies, templates, pixels,
     * composites, composites from video RAM, blends, bilinear, YUV, lines,
     * patterns */
    double gpu_scale, core_scale[OGPU_VK_COST_KINDS];
    /* Why each 2D drawing command of cpu ran on the core (they add up to
     * cpu): OGPU_VK_WHY_ below. And of those, by opcode: core_op[op - 0x10]
     * for the drawing range 0x0010..0x002F (FILL to the last 2D opcode). */
    unsigned long why[OGPU_VK_WHY_COUNT];
    unsigned long core_op[OGPU_VK_CORE_OPS];
    /* Video RAM's card part: commands that read it, drawn on the GPU where
     * the cost model would have drawn them on the core (card_gpu), and drawn
     * on the core because the GPU path doesn't take them, slowly (card_core). */
    unsigned long card_gpu, card_core;
};
void ogpu_vk_stats(const struct ogpu_vk *vk, struct ogpu_vk_stats *s);

#endif
