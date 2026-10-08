/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core: Mesa's GL state tracker and softpipe behind a small C API.
 * On OS 3.2 programs reach it through OpenGPU's GL module (libGL.a and
 * GL.module), and the host tests call it directly. It knows nothing of the
 * Amiga: finished frames go to a present hook, which on OS 3.2 sends them
 * through OpenGPU (one PIXELS command) or OpenRTG (WritePixelArray). */
#ifndef GLA_CORE_H
#define GLA_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

enum gla_profile { GLA_COMPAT = 0, GLA_CORE = 1, GLA_ES2 = 2 };

struct gla_config {
    int profile;            /* GLA_ */
    int major, minor;       /* 0, 0 = the highest the profile has */
    int doublebuf;          /* draw into a back buffer, shown by gla_swap */
    int depth, stencil;     /* bits wanted: 0, 16 or 24; 0 or 8 */
    int alpha;              /* keep destination alpha */
};

/* Byte orders of a frame's pixels in memory. Big-endian builds (the 68k)
 * draw GLA_ARGB, which is OpenGPU's ARGB32 and WritePixelArray's
 * RECTFMT_ARGB; little-endian hosts draw GLA_BGRA. */
enum gla_format { GLA_BGRA = 0, GLA_ARGB = 1 };

/* A finished frame: rows of 32-bit pixels, row 0 at the top. Alpha is
 * undefined unless the config asked for it. x0..x1, y0..y1 is the part
 * that changed. */
struct gla_frame {
    const void *pixels;
    int         format;     /* GLA_ */
    unsigned    stride;     /* bytes a row */
    unsigned    w, h;
    int         x0, y0, x1, y1;
};

struct gla_present {
    void (*present)(void *user, const struct gla_frame *frame);
    void  *user;
};

struct gla_display;
struct gla_context;
struct gla_buffer;

/* Mesa on the host's graphics chip through virgl (stream v1.2's
 * OGPU_OP_VIRGL): how request blocks reach the host, and memory it reaches. */
struct gla_virgl_transport {
    void *(*alloc)(void *user, unsigned long bytes);    /* zeroed; 0 when there is none */
    void (*free)(void *user, void *p);
    unsigned long (*addr)(void *user, const void *p);   /* the address the host knows p by */
    int (*run)(void *user, void *block, unsigned long bytes);   /* 0, or an error: the block wasn't run */
    void *user;
};

/* softpipe, on this CPU. */
struct gla_display *gla_display_create(void);
/* virgl through t, or NULL when the host doesn't answer (then softpipe). */
struct gla_display *gla_display_create_virgl(const struct gla_virgl_transport *t);
/* "softpipe" or "virgl". */
const char *gla_display_driver(struct gla_display *d);
void gla_display_destroy(struct gla_display *d);
/* The versions this display gives: major * 10 + minor, 0 when none. */
int gla_display_version(struct gla_display *d, int profile);

struct gla_context *gla_context_create(struct gla_display *d, const struct gla_config *cfg,
                                       struct gla_context *share);
void gla_context_destroy(struct gla_context *c);

struct gla_buffer *gla_buffer_create(struct gla_display *d, const struct gla_config *cfg,
                                     unsigned w, unsigned h, const struct gla_present *p);
void gla_buffer_resize(struct gla_buffer *b, unsigned w, unsigned h);
void gla_buffer_destroy(struct gla_buffer *b);

/* c 0 releases the current context. */
int  gla_make_current(struct gla_context *c, struct gla_buffer *b);
/* Finish drawing and present the back (or front) buffer. */
void gla_swap(struct gla_context *c, struct gla_buffer *b);
void *gla_get_proc_address(const char *name);

#ifdef __cplusplus
}
#endif
#endif
