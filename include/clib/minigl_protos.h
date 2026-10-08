/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * MiniGLOpen and MiniGLClose, in libminigl.a. */
#ifndef CLIB_MINIGL_PROTOS_H
#define CLIB_MINIGL_PROTOS_H

#include <exec/types.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL MiniGLOpen(void);              /* opens minigl.library and takes its table; FALSE when it can't */
void MiniGLClose(void);

#ifdef __cplusplus
}
#endif

#endif
