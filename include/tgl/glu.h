/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * TinyGL on OpenGPU: the GLU calls TinyGL programs use most, in
 * libtinygl.a. They are the Team's own, on GL 1.x calls; the rest of GLU
 * (tessellation, quadrics, NURBS) isn't here. */
#ifndef TGL_GLU_H
#define TGL_GLU_H

#include <tgl/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ, GLdouble centerX, GLdouble centerY,
               GLdouble centerZ, GLdouble upX, GLdouble upY, GLdouble upZ);
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
/* Loads the picture as level 0 and has GL make the smaller levels
 * (GL_GENERATE_MIPMAP). 0, or GL's error. */
GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const void *data);
const GLubyte *gluErrorString(GLenum error);

#ifdef __cplusplus
}
#endif

#endif
