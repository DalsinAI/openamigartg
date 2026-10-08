/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: its name and the version a program asks for. Version 29
 * goes with dispatch ABI 5 (libraries/minigl_dispatch.h). */
#ifndef LIBRARIES_MINIGL_H
#define LIBRARIES_MINIGL_H

#include <exec/libraries.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINIGLNAME "minigl.library"

#ifndef MINIGL_VERSION
#define MINIGL_VERSION 29
#endif

extern struct Library *MiniGLBase;

#ifdef __cplusplus
}
#endif

#endif
