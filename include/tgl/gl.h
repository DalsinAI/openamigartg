/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * TinyGL on OpenGPU: the header a TinyGL program includes (directly, or
 * through <proto/tinygl.h>). Link -ltinygl -lGL (pkg-config tinygl).
 *
 * GL's calls (glBegin, glVertex3f and the rest) are Mesa's own, from
 * <GL/gl.h> and libGL.a: they draw into the current context, which
 * GLAInitializeContext* and GLASwapBuffers make current. The program keeps
 * its context in __tglContext, as on other systems with TinyGL, and the
 * glA... forms below use it. */
#ifndef TGL_GL_H
#define TGL_GL_H

#include <tgl/types.h>
#include <tgl/gla.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The program's context: the program defines it (GLContext *__tglContext;)
 * and sets it from GLInit(). */
extern GLContext *__tglContext;

#ifdef __cplusplus
}
#endif

#define glAInitializeContext(tags)          GLAInitializeContext(__tglContext, (tags))
#define glAInitializeContextWindowed(w)     GLAInitializeContextWindowed(__tglContext, (w))
#define glAInitializeContextScreen(s)       GLAInitializeContextScreen(__tglContext, (s))
#define glAInitializeContextBitMap(b)       GLAInitializeContextBitMap(__tglContext, (b))
#define glAReinitializeContextWindowed(w)   GLAReinitializeContextWindowed(__tglContext, (w))
#define glADestroyContext()                 GLADestroyContext(__tglContext)
#define glADestroyContextWindowed()         GLADestroyContextWindowed(__tglContext)
#define glADestroyContextScreen()           GLADestroyContextScreen(__tglContext)
#define glADestroyContextBitMap()           GLADestroyContextBitMap(__tglContext)
#define glASwapBuffers()                    GLASwapBuffers(__tglContext)
#define glASetSync(e)                       GLASetSync(__tglContext, (e))
#define glASetAttr(a, v)                    GLASetAttr(__tglContext, (a), (v))
#define glAGetProcAddress(n)                GLAGetProcAddress(__tglContext, (n))

#endif
