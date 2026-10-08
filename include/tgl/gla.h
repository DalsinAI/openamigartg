/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * TinyGL on OpenGPU: the context calls (GLInit, GLClose and the GLA calls)
 * with which a TinyGL program opens GL on a window, a screen or a bitmap.
 * They are ordinary functions in libtinygl.a, which runs GL on OpenGPU's
 * GL.module (Mesa: virgl on the GPU, else softpipe on the 68k) through
 * libGL.a. The names and arguments are TinyGL's, so TinyGL programs build
 * unchanged; the code is the Team's (library/tinygl/README.md). */
#ifndef TGL_GLA_H
#define TGL_GLA_H

#include <utility/tagitem.h>
#include <tgl/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLAInitializeContext's tags. When more than one target is given, the
 * screen is taken first, then the window, then the bitmap. */
enum {
    TGL_CONTEXT_SCREEN = TAG_USER + 1,  /* struct Screen *: draw on the whole screen */
    TGL_CONTEXT_WINDOW,                 /* struct Window *: draw inside its borders */
    TGL_CONTEXT_BITMAP,                 /* struct BitMap *: draw into it */
    TGL_CONTEXT_STENCIL                 /* BOOL: ask for a stencil buffer (8 bits) */
};

/* A new context: nothing is drawn until one of the GLAInitializeContext
 * calls gives it a target. NULL when memory is short. */
GLContext *GLInit(void);
/* Ends the context (destroying its target's GL first) and frees it. */
void GLClose(GLContext *context);

/* Start GL on a target. 1 on success, 0 when GL couldn't start (no
 * LIBS:OpenGPU/GL.module, or memory). The context becomes current. */
int GLAInitializeContext(GLContext *context, struct TagItem *tags);
int GLAInitializeContextWindowed(GLContext *context, void *window);
int GLAInitializeContextScreen(GLContext *context, void *screen);
int GLAInitializeContextBitMap(GLContext *context, void *bitmap);
/* The window was resized, or the program moved GL to another window. */
int GLAReinitializeContextWindowed(GLContext *context, void *window);

/* Stop GL on the target; the context can be initialised again. */
void GLADestroyContext(GLContext *context);
void GLADestroyContextWindowed(GLContext *context);
void GLADestroyContextScreen(GLContext *context);
void GLADestroyContextBitMap(GLContext *context);

/* Show the frame. A window's new inner size is picked up here as well. */
void GLASwapBuffers(GLContext *context);
/* 1: GLASwapBuffers waits for the display's vertical blank first. */
void GLASetSync(GLContext *context, int enable);
/* Attributes TinyGL programs set; this layer keeps them and acts on none yet. */
void GLASetAttr(GLContext *context, unsigned int attr, unsigned int value);
/* GL's own function by name (glXxx), or NULL. The Team's addition. */
void *GLAGetProcAddress(GLContext *context, const char *name);

#ifdef __cplusplus
}
#endif

#endif
