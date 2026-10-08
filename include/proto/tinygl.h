/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * TinyGL on OpenGPU: the header a TinyGL program includes. tinygl.library
 * is there so that a program's OpenLibrary("tinygl.library", ...) works; the
 * calls themselves are in libtinygl.a (link -ltinygl -lGL), so TinyGLBase
 * isn't used by them. */
#ifndef PROTO_TINYGL_H
#define PROTO_TINYGL_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <clib/tinygl_protos.h>

#ifndef __NOLIBBASE__
extern struct Library *TinyGLBase;
#endif

#include <tgl/gl.h>

#endif
