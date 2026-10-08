/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLA's calls, answering "no GL", for SDL's tests linked with libSDL2.a
 * where no libGL.a is built (library/build.sh builds SDL without the GL
 * module). SDL's test framework calls SDL_GL_ functions, which bring in
 * libSDL2.a's SDL2_gl.o and its references to libGL.a. A program meant to
 * draw GL links -lGL instead (`sdl2-config --libs` gives it). */
#include <stddef.h>

void *gla_display_create(void) { return NULL; }
void *gla_display_create_virgl(const void *t) { (void)t; return NULL; }
const char *gla_display_driver(void *d) { (void)d; return "none"; }
void gla_display_destroy(void *d) { (void)d; }
int gla_display_version(void *d, int profile) { (void)d; (void)profile; return 0; }
void *gla_context_create(void *d, const void *cfg, void *share) { (void)d; (void)cfg; (void)share; return NULL; }
void gla_context_destroy(void *c) { (void)c; }
void *gla_buffer_create(void *d, const void *cfg, unsigned w, unsigned h, const void *p)
{
    (void)d; (void)cfg; (void)w; (void)h; (void)p;
    return NULL;
}
void gla_buffer_resize(void *b, unsigned w, unsigned h) { (void)b; (void)w; (void)h; }
void gla_buffer_destroy(void *b) { (void)b; }
int gla_make_current(void *c, void *b) { (void)c; (void)b; return 0; }
void gla_swap(void *c, void *b) { (void)c; (void)b; }
void *gla_get_proc_address(const char *name) { (void)name; return NULL; }
void gla_os3_present_init(void *p, void *t) { (void)p; (void)t; }
int gla_os3_virgl_transport(void *t) { (void)t; return 0; }
