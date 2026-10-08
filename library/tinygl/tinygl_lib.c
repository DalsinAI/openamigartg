/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * tinygl.library 53 for AmigaOS 3: a stand-in. TinyGL programs open
 * tinygl.library before they start GL; on OpenGPU their calls are in
 * libtinygl.a and libGL.a (linked into the program), and GL itself is
 * OpenGPU's GL.module. This library has no calls of its own: it is there so
 * the program's OpenLibrary works, and it opens only where opengpu.library
 * is installed, so a program on a system without OpenGPU stops at its own
 * "can't open tinygl.library" message rather than later.
 *
 * Built bare by library/tinygl/build.sh: no startup code, no C library. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>

#define TGL_LIB_VERSION  53
#define TGL_LIB_REVISION 1

#ifndef REG
#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#endif

struct TGLBase {
    struct Library lib;
    BPTR seglist;
    struct ExecBase *sys;
};

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "tinygl.library";
static const char lib_id[] = "tinygl.library 53.1 (8.10.2026) TinyGL on OpenGPU, Dalsin Limited\r\n";
static const char lib_ver[] __attribute__((used)) = "\0$VER: tinygl.library 53.1 (8.10.2026) TinyGL on OpenGPU, Dalsin Limited";

static struct Library *lib_init(REG(d0, struct TGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct TGLBase *base));
static BPTR lib_close(REG(a6, struct TGLBase *base));
static BPTR lib_expunge(REG(a6, struct TGLBase *base));
static ULONG lib_null(void) { return 0; }

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct TGLBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, TGL_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static struct Library *lib_init(REG(d0, struct TGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    base->seglist = seglist;
    base->sys = sys;
    base->lib.lib_Revision = TGL_LIB_REVISION;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct TGLBase *base))
{
    struct ExecBase *SysBase = base->sys;
    struct Library *ogpu;
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    ogpu = OpenLibrary((CONST_STRPTR) "opengpu.library", 0);
    if (!ogpu) {
        base->lib.lib_OpenCnt--;
        return 0;
    }
    CloseLibrary(ogpu);
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct TGLBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct TGLBase *base))
{
    struct ExecBase *SysBase = base->sys;
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}
