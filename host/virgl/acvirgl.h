/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side: the request blocks of OGPU_OP_VIRGL
 * (include/opengpu/virgl.h) carried out with virglrenderer, so Mesa's virgl
 * driver on the Amiga draws GL and GLES on the host's graphics chip. The
 * Cradle's runtime calls it from its ring (amigachrome's acrtg.c, as its
 * `virgl` hook), in ring order.
 *
 * virglrenderer (MIT) is opened with dlopen, so a runtime without it still
 * starts: acvirgl_create says why not, and OGPU_OP_VIRGL stays unknown. It
 * makes its own GL context (EGL, surfaceless) on the thread that calls
 * acvirgl_run; every call must come from that thread. Desktop GL first,
 * else GLES (a Pi's V3D); AC_VIRGL_GLES=1 or 0 picks one, and
 * AC_VIRGL_DRIVER names Mesa's driver (zink: GL over the host's Vulkan). */
#ifndef ACVIRGL_H
#define ACVIRGL_H

#include <stdint.h>

struct acvirgl;

/* lib: the libvirglrenderer.so.1 to open (a path), or 0 for the
 * environment's ACVIRGL_LIB. Returns 0 with the reason in err. */
struct acvirgl *acvirgl_create(const char *lib, char *err, int errlen);
void acvirgl_destroy(struct acvirgl *v);

/* One request block of `bytes` at Amiga address addr, reached through map.
 * Returns 0, or an OGPU_ERR_ when the block itself can't be read; each
 * command's own result is in its result word. */
int acvirgl_run(struct acvirgl *v, uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user,
                uint32_t addr, uint32_t bytes);

/* What it has done, for the runtime's log. */
struct acvirgl_stats {
    uint64_t blocks, submits, submit_words, puts, put_bytes, gets, get_bytes, errors;
    uint64_t frame_get_bytes;                       /* GETs of display targets: frames read back into Amiga memory */
    uint64_t presents, present_rects, present_pixels;   /* ACV_PRESENT: frames written straight into a bitmap */
};
void acvirgl_stats(const struct acvirgl *v, struct acvirgl_stats *s);

/* "GL" or "GLES": the kind of context virglrenderer is drawing with. */
const char *acvirgl_api(const struct acvirgl *v);
/* The GPU it draws on, the host's GL_RENDERER: "V3D 7.1", "zink Vulkan 1.3(V3D 7.1 ...)", "unknown". */
const char *acvirgl_renderer(const struct acvirgl *v);

#endif
