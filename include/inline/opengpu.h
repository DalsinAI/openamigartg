/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Inline calls for bebbo's m68k-amigaos-gcc, from library/opengpu/opengpu_lib.sfd. */
#ifndef _INLINE_OPENGPU_H
#define _INLINE_OPENGPU_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif
#ifndef OPENGPU_BASE_NAME
#define OPENGPU_BASE_NAME OpenGPUBase
#endif
#define OGPU_Query(op, format) \
    LP2(0x1e, ULONG, OGPU_Query, ULONG, op, d0, ULONG, format, d1, , OPENGPU_BASE_NAME)
#define OGPU_BackEndName(index) \
    LP1(0x24, STRPTR, OGPU_BackEndName, ULONG, index, d0, , OPENGPU_BASE_NAME)
#define OGPU_Submit(stream, words, fence) \
    LP3(0x2a, LONG, OGPU_Submit, APTR, stream, a0, ULONG, words, d0, ULONG *, fence, a1, , OPENGPU_BASE_NAME)
#define OGPU_Wait(fence) \
    LP1(0x30, LONG, OGPU_Wait, ULONG, fence, d0, , OPENGPU_BASE_NAME)
#endif
