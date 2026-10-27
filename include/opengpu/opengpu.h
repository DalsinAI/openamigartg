/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * opengpu.library's public interface, 0.1 (G1: the CPU back end).
 *
 * A program builds a batch with the calls in <opengpu/build.h> (no library
 * calls), sends it with OGPU_Submit and, when it needs the pixels, waits for
 * its fence. OGPU_Query says before that whether an operation is done fully,
 * partly or not at all for a target format, and by which back end:
 *   answer = OGPU_Query(OGPU_OP_COMPOSITE, OGPU_FMT_RGB565);
 *   OGPU_ANSWER(answer) is OGPU_FULL, OGPU_PARTIAL or OGPU_NONE;
 *   OGPU_BACKEND(answer) is the back end's index for OGPU_BackEndName;
 *   OGPU_ONGPU(answer) is 1 when that back end draws it on a graphics chip
 *   (on AmigaChrome, the PC's GPU), and 0 when a CPU does: the Amiga's, or
 *   the host's for a driver whose hardware draws with its CPU.
 * Back end 0 is always the CPU, on every Amiga. A driver says it is "a
 * driver" (index 1), not where it draws, so OGPU_BACKEND alone can't tell a
 * host GPU from a host CPU; OGPU_ONGPU can, from opengpu.library 0.21 with a
 * driver that says so (ACRTG.gpu 1.5). Before that it is always 0. */
#ifndef OPENGPU_OPENGPU_H
#define OPENGPU_OPENGPU_H

#include "stream.h"

#define OPENGPU_NAME    "opengpu.library"
#define OPENGPU_VERSION 0

#define OGPU_ANSWER(q)   ((int)((q) & 0xFFUL))
#define OGPU_BACKEND(q)  ((int)(((q) >> 8) & 0xFFUL))
#define OGPU_ONGPU(q)    ((int)(((q) >> 16) & 1UL))

#define OGPU_ONGPU_REVISION 21          /* the library's lib_Revision (version 0) that passes it on */

#define OGPU_BACKEND_CPU 0

/* OGPU_Wait(OGPU_FENCE_ALL) (opengpu.library 0.20): wait for every batch
 * submitted so far, whoever submitted it; OGPU_OK, or OGPU_ERR_DEVICE when
 * the hardware has gone. Before 0.20 it returns at once. */
#define OGPU_FENCE_ALL   0xFFFFFFFFUL
#define OGPU_FENCE_ALL_REVISION 20      /* the library's lib_Revision (version 0) that knows it */

#endif
