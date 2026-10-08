/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * libminigl.a: the library's base and its table, once opened. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <libraries/minigl_dispatch.h>

struct Library *MiniGLBase = 0;
const MGLDispatchTable *MiniGLDispatch = 0;
