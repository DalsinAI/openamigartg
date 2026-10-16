/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Entry stubs, for a library that keeps nothing writable in its own file, so
 * the same file runs from a ROM (openrtg.library, cybergraphics.library).
 *
 * Such a library keeps what it writes in a block it allocates at init, and
 * has that block's address in A4 in all its code (built -ffixed-a4, with a
 * global register variable). Code from outside (exec calling the library's
 * functions, graphics calling a SetFunction patch, OpenGfx calling a
 * provider) comes in with its own A4, so each such way in goes through a
 * stub in the block: it saves the caller's A4, loads the block's, calls the
 * function and puts the caller's A4 back. Registers and the result pass
 * through untouched.
 *
 *   ortg_stub_reg: a function that takes its arguments in registers (none
 *                  in A4; the library calls, the patches, input handlers).
 *   ortg_stub_c2:  a C function with two arguments on the stack (OpenGfx's
 *                  provider calls, include/openrtg's opengfx bridge).
 *
 * The caller clears the caches (CacheClearU) after making stubs, before
 * any is used.
 */
#ifndef ORTG_STUB_H
#define ORTG_STUB_H

#include <exec/types.h>

struct Library;

#define ORTG_STUB_REG_BYTES 18
#define ORTG_STUB_C2_BYTES 28

struct ortg_stubs {
    UBYTE *next, *end;                  /* the room left for stubs */
};

/* The stub's address, or NULL when there is no room left. */
APTR ortg_stub_reg(struct ortg_stubs *p, APTR a4, APTR fn);
APTR ortg_stub_c2(struct ortg_stubs *p, APTR a4, APTR fn);

/* A library's own calls from LVO -6 * first on (the four standard ones before
 * that keep the caller's A4): each jump in its table is pointed at a stub.
 * The number of calls given stubs, or -1 when there was no room. Called from
 * the library's init, before exec sums the table. */
LONG ortg_stub_vectors(struct ortg_stubs *p, APTR a4, struct Library *lib, int first);

#endif
