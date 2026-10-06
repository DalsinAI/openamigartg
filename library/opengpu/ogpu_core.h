/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's core: carries out an OGPU stream v1 (include/opengpu/stream.h).
 * Plain C with no C library and no floating point, so the same file builds
 * for the 68k (the CPU back end), for the Cradle's runtime and for the
 * PiStorm's spare ARM core. */
#ifndef OGPU_CORE_H
#define OGPU_CORE_H

#include "../../include/opengpu/stream.h"

typedef unsigned char  ogpu_u8;
typedef unsigned short ogpu_u16;
typedef unsigned long  ogpu_u32;   /* at least 32 bits; only the low 32 are used */

struct ogpu_surface {
    ogpu_u8  *pixels;       /* row 0, mapped; 0 when the slot is empty */
    long      bpr;          /* bytes a row */
    int       w, h;
    int       format;       /* OGPU_FMT_ */
};

struct ogpu_core {
    /* Turn an Amiga address and a length into memory this back end can use;
     * 0 when it can't. The CPU back end returns the address itself. */
    ogpu_u8 *(*map)(void *user, ogpu_u32 address, ogpu_u32 length);
    /* Called for each FENCE once everything before it is done (may be 0). */
    void (*fence)(void *user, ogpu_u32 id);
    void *user;

    struct ogpu_surface slot[OGPU_MAX_SLOTS];
    int target;             /* slot, or -1 */
    int cx0, cy0, cx1, cy1; /* clip: x0 <= x < x1; cx1 0 = the whole target */
    int last_error;         /* the first error the last run met */
    long error_word;        /* and where: the word index of its command */
};

void ogpu_core_init(struct ogpu_core *c);

/* Carry out `words` words of stream (read big-endian from `stream`).
 * Returns the number of commands done; c->last_error holds the first error
 * (OGPU_OK if none). A bad length stops the run; other errors skip the
 * command and go on. */
long ogpu_core_run(struct ogpu_core *c, const ogpu_u8 *stream, long words);

/* OGPU_FULL, OGPU_PARTIAL or OGPU_NONE for an opcode drawing into a format. */
int ogpu_core_supports(int op, int format);

#endif
