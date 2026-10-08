/* Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * virgl's transport on AmigaOS (gla_virgl_os3.h). The host reaches Fast RAM
 * by the Amiga's own addresses, so memory for it is AllocVec's, an address is
 * the pointer, and a request block is one OGPU_OP_VIRGL command in a batch,
 * done when OGPU_Wait returns. */
#include "gla_virgl_os3.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/opengpu.h>
#include <opengpu/build.h>

struct Library *OpenGPUBase;

static void *os3_alloc(void *user, unsigned long bytes)
{
    (void)user;
    return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR);
}

static void os3_free(void *user, void *p)
{
    (void)user;
    if (p) FreeVec(p);
}

static unsigned long os3_addr(void *user, const void *p)
{
    (void)user;
    return (unsigned long)p;
}

static int os3_run(void *user, void *block, unsigned long bytes)
{
    ULONG stream[4], fence = 0;
    struct OGPUBatch b;
    (void)user;
    ogpu_batch_init(&b, stream, 4);
    ogpu_virgl(&b, (unsigned long)block, bytes);
    if (OGPU_Submit(stream, (ULONG)b.words, &fence) != OGPU_OK) return -1;
    return OGPU_Wait(fence) == OGPU_OK ? 0 : -1;
}

int gla_os3_virgl_transport(struct gla_virgl_transport *t)
{
    if (!OpenGPUBase && !(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) return 0;
    if (OGPU_ANSWER(OGPU_Query(OGPU_OP_VIRGL, 0)) != OGPU_FULL) return 0;
    t->alloc = os3_alloc;
    t->free = os3_free;
    t->addr = os3_addr;
    t->run = os3_run;
    t->user = NULL;
    return 1;
}
