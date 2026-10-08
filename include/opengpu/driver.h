/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The back end interface (G2 onwards). Each file in LIBS:OpenGPU/ (ACRTG.gpu,
 * PiStorm.gpu, AGA.gpu) is an Amiga library whose first call,
 * OGPU_DriverGet (LVO -30, no arguments, A6 the driver's base), hands
 * opengpu.library this table, or NULL when its hardware isn't there (the
 * library is then closed again). Every back end takes the same OGPU stream
 * v1 bytes.
 *
 * opengpu.library 0.3 opens the drivers the first time a program (a DOS
 * process) asks it anything, takes the first that answers, and keeps it
 * open. The table's functions are plain C with the m68k GCC stack
 * convention (arguments on the stack, the result in D0); opengpu.library
 * calls them in the caller's task, one at a time per driver (it holds its
 * own semaphore around each call), so a driver needs no locking of its own
 * for these. */
#ifndef OPENGPU_DRIVER_H
#define OPENGPU_DRIVER_H

#include "stream.h"

#define OGPU_DRIVER_VERSION 1
#define OGPU_DRIVER_DIR     "LIBS:OpenGPU/"
#define OGPU_DRIVER_LVO     -30             /* struct OGPUDriver *OGPU_DriverGet(void) */

struct OGPUDriver {
    unsigned long version;          /* OGPU_DRIVER_VERSION */
    const char   *name;             /* "ACRTG.gpu on the Cradle" */
    /* OGPU_FULL, OGPU_PARTIAL or OGPU_NONE for an opcode into a format, on
     * the surface at `address` (a back end may reach only some memory, such
     * as one board's video RAM); address 0 asks about any memory. */
    int  (*supports)(int op, int format, unsigned long address);
    /* Queue a batch; *fence is set to a number that grows with each batch.
     * Returns OGPU_OK, or an error when the batch was refused whole
     * (OGPU_ERR_DEVICE: the hardware is gone, and opengpu.library stops
     * using this driver). */
    long (*submit)(const void *stream, unsigned long words, unsigned long *fence);
    /* Wait until the batch with that fence is done; its first error. */
    long (*wait)(unsigned long fence);
};

#endif
