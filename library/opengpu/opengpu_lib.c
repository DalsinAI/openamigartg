/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library 0.2 (G1 with stream v1.1, DESIGN.md section 5): the library and its
 * built-in CPU back end, which runs ogpu_core.c on the Amiga's own 68k.
 * Batches are carried out as they are submitted, so a fence is done by the
 * time OGPU_Submit returns; OGPU_Wait gives the batch's first error. The
 * Cradle's and the PiStorm's back ends (G2, G3) will queue instead, behind
 * the same calls. Built bare by library/build.sh.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <proto/exec.h>

#include "ogpu_core.h"
#include "../../include/opengpu/opengpu.h"

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#define LIB_VERSION 0
#define LIB_REVISION 2
#define RESULTS 16                              /* the last batches' results, by fence */

struct OpenGPUBase {
    struct Library lib;
    BPTR seglist;
    struct SignalSemaphore lock;                /* fences and results */
    ULONG fence;                                /* the last one given */
    LONG result[RESULTS];
};

struct ExecBase *SysBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "opengpu.library";
static const char lib_id[] = "opengpu.library 0.2 (6.10.2026) OpenGPU, Dalsin Limited\r\n";
/* For C:Version, which looks for "$VER:" in the file. */
static const char lib_ver[] __attribute__((used)) = "\0$VER: opengpu.library 0.2 (6.10.2026) OpenGPU, Dalsin Limited";
static const char cpu_name[] = "CPU";

static struct Library *lib_init(REG(d0, struct OpenGPUBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenGPUBase *base));
static BPTR lib_close(REG(a6, struct OpenGPUBase *base));
static BPTR lib_expunge(REG(a6, struct OpenGPUBase *base));
static ULONG lib_null(void);
static ULONG OGPU_Query(REG(d0, ULONG op), REG(d1, ULONG format), REG(a6, struct OpenGPUBase *base));
static STRPTR OGPU_BackEndName(REG(d0, ULONG index), REG(a6, struct OpenGPUBase *base));
static LONG OGPU_Submit(REG(a0, APTR stream), REG(d0, ULONG words), REG(a1, ULONG *fence), REG(a6, struct OpenGPUBase *base));
static LONG OGPU_Wait(REG(d0, ULONG fence), REG(a6, struct OpenGPUBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)OGPU_Query, (APTR)OGPU_BackEndName, (APTR)OGPU_Submit, (APTR)OGPU_Wait, (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct OpenGPUBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- the library ---------------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct OpenGPUBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    InitSemaphore(&base->lock);
    return &base->lib;
}
static struct Library *lib_open(REG(a6, struct OpenGPUBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}
static BPTR lib_close(REG(a6, struct OpenGPUBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}
static BPTR lib_expunge(REG(a6, struct OpenGPUBase *base))
{
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    BPTR seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}
static ULONG lib_null(void) { return 0; }

/* ---- the CPU back end ------------------------------------------------------------ */

/* The 68k reaches every address as it is. */
static ogpu_u8 *cpu_map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user; (void)length;
    return (ogpu_u8 *)address;
}

/* ---- OpenGPU's calls ------------------------------------------------------------- */

static ULONG OGPU_Query(REG(d0, ULONG op), REG(d1, ULONG format), REG(a6, struct OpenGPUBase *base))
{
    (void)base;
    return (ULONG)ogpu_core_supports((int)op, (int)format) | (OGPU_BACKEND_CPU << 8);
}

static STRPTR OGPU_BackEndName(REG(d0, ULONG index), REG(a6, struct OpenGPUBase *base))
{
    (void)base;
    return index == OGPU_BACKEND_CPU ? (STRPTR)cpu_name : NULL;
}

static LONG OGPU_Submit(REG(a0, APTR stream), REG(d0, ULONG words), REG(a1, ULONG *fence), REG(a6, struct OpenGPUBase *base))
{
    struct ogpu_core core;      /* on the caller's stack, so callers never wait on each other */
    ULONG f;
    ogpu_core_init(&core);
    core.map = cpu_map;
    core.fence = 0;
    core.ext = 0;               /* the CPU back end carries no GPU APIs: OGPU_OP_VIRGL is BADOP */
    core.user = 0;
    ogpu_core_run(&core, (const ogpu_u8 *)stream, (long)words);
    ObtainSemaphore(&base->lock);
    f = ++base->fence;
    if (!f) f = ++base->fence;                  /* 0 is never a fence */
    base->result[f % RESULTS] = core.last_error;
    ReleaseSemaphore(&base->lock);
    if (fence) *fence = f;
    return core.last_error;
}

static LONG OGPU_Wait(REG(d0, ULONG fence), REG(a6, struct OpenGPUBase *base))
{
    LONG r = OGPU_OK;
    ObtainSemaphore(&base->lock);
    /* Batches run as they are submitted; only recent results are kept, and
     * an older fence says so rather than claim the batch went well. */
    if (fence && fence <= base->fence) r = base->fence - fence < RESULTS ? base->result[fence % RESULTS] : OGPU_ERR_EXPIRED;
    ReleaseSemaphore(&base->lock);
    return r;
}
