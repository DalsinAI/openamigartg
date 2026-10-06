/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The back end interface (G2 onwards): each file in LIBS:OpenGPU/ (ACRTG.gpu,
 * PiStorm.gpu, AGA.gpu) is a library whose first call hands opengpu.library
 * this table. Every back end takes the same OGPU stream v1 bytes. In 0.1
 * only the built-in CPU back end exists; this header fixes the shape the
 * others will have. */
#ifndef OPENGPU_DRIVER_H
#define OPENGPU_DRIVER_H

#include "stream.h"

#define OGPU_DRIVER_VERSION 1

struct OGPUDriver {
    unsigned long version;          /* OGPU_DRIVER_VERSION */
    const char   *name;             /* "ACRTG.gpu on the Cradle" */
    /* OGPU_FULL, OGPU_PARTIAL or OGPU_NONE for an opcode into a format, on
     * the surface at `address` (a back end may reach only some memory, such
     * as one board's video RAM). */
    int  (*supports)(int op, int format, unsigned long address);
    /* Queue a batch; *fence is set to a number that grows with each batch.
     * Returns OGPU_OK, or an error when the batch was refused whole. */
    long (*submit)(const void *stream, unsigned long words, unsigned long *fence);
    /* Wait until the batch with that fence is done; its first error. */
    long (*wait)(unsigned long fence);
};

#endif
