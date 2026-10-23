/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * opengpu.library's calls for the opengpu renderer's host harness: answered
 * by OpenGPU's own core (tests/sdl3/render_host/host.c), as the CPU back end
 * answers them on an Amiga. */
#ifndef INLINE_OPENGPU_H
#define INLINE_OPENGPU_H
#include <exec/types.h>
ULONG host_OGPU_Query(ULONG op, ULONG format);
LONG host_OGPU_Submit(APTR stream, ULONG words, ULONG *fence);
LONG host_OGPU_Wait(ULONG fence);
#define OGPU_Query(op, format) host_OGPU_Query((op), (format))
#define OGPU_Submit(stream, words, fence) host_OGPU_Submit((stream), (words), (fence))
#define OGPU_Wait(fence) host_OGPU_Wait((fence))
#endif
