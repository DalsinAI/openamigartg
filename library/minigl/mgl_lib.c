/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library 29: the library itself. Its one function (at -30) answers
 * the dispatch table (mgl_table.c), through which programs make every call;
 * MiniGLOpen() in libminigl.a opens the library and fetches it. Built bare
 * by library/minigl/build.sh: no startup code, no C library.
 *
 * The Team wrote this library. Hyperion's MiniGL 1.2 and the 2026 68k
 * minigl.library were references for the interface and its behaviour only;
 * no code was taken from either. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdarg.h>

#include "mgl_internal.h"
#include "../../include/opengpu/opengpu.h"
#include "../../include/proto/opengpu.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *CyberGfxBase;
struct Library *UtilityBase;
struct Library *OpenGPUBase;
struct Device *TimerBase;

GLcontext mgl_current;
struct mgl_prefs mgl_prefs;
long mgl_trace;

struct MGLBase {
    struct Library lib;
    BPTR seglist;
    struct timerequest treq;
    int timer_open;
};
static struct MGLBase *MGLBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "minigl.library";
static const char lib_id[] = "minigl.library 29.10 (8.10.2026) OpenRTG MiniGL, Dalsin Limited\r\n";
static const char lib_ver[] __attribute__((used)) = "\0$VER: minigl.library 29.10 (8.10.2026) OpenRTG MiniGL, Dalsin Limited";

static struct Library *lib_init(REG(d0, struct MGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct MGLBase *base));
static BPTR lib_close(REG(a6, struct MGLBase *base));
static BPTR lib_expunge(REG(a6, struct MGLBase *base));
static ULONG lib_null(void) { return 0; }
static const MGLDispatchTable *lib_dispatch(REG(a6, struct MGLBase *base)) { return &mgl_table; }

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)lib_dispatch,             /* -30: the table */
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct MGLBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, MGL_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static struct Library *lib_init(REG(d0, struct MGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    MGLBase = base;
    base->seglist = seglist;
    base->lib.lib_Revision = MGL_LIB_REVISION;
    mgl_prefs.depth = 16;
    mgl_prefs.buffers = 2;
    mgl_prefs.zbits = 16;
    mgl_prefs.route = -1;
    return &base->lib;
}

/* What the library uses, opened on the first open (exec's own call, so disk
 * libraries load in the opener's task). */
static int open_libs(struct MGLBase *base)
{
    if (!DOSBase) DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!UtilityBase) UtilityBase = OpenLibrary("utility.library", 39);
    if (!CyberGfxBase) CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    if (!DOSBase || !GfxBase || !IntuitionBase || !UtilityBase || !CyberGfxBase) return 0;
    if (!base->timer_open) {
        if (OpenDevice((STRPTR)"timer.device", UNIT_MICROHZ, &base->treq.tr_node, 0) == 0) {
            TimerBase = base->treq.tr_node.io_Device;
            base->timer_open = 1;
        }
    }
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary("opengpu.library", 0);
        mgl_prefs.gpu_3d = 0;
        if (OpenGPUBase) {
            ULONG a = OGPU_Query(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32);
            if (OGPU_ANSWER(a) != OGPU_NONE) {
                mgl_prefs.gpu_3d = 1;
                mgl_prefs.gpu_host = OGPU_BACKEND(a) != OGPU_BACKEND_CPU;
            }
        }
    }
    mgl_read_prefs();
    return 1;
}

static struct Library *lib_open(REG(a6, struct MGLBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    if (!open_libs(base)) { base->lib.lib_OpenCnt--; return 0; }
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct MGLBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct MGLBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    if (base->timer_open) CloseDevice(&base->treq.tr_node);
    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    OpenGPUBase = CyberGfxBase = UtilityBase = 0;
    IntuitionBase = 0; GfxBase = 0; DOSBase = 0; TimerBase = 0;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

/* ---- preferences ---------------------------------------------------------------------- */

/* ENV:MiniGL/Driver   Auto, CPU or OpenGPU (Auto: OpenGPU when its back end
 *                     is not this 68k)
 * ENV:MiniGL/ZBuffer  16 or 32: bits of depth, unless the program chose
 * ENV:MiniGL/Flip     1: a screen changes buffers to show a frame instead of
 *                     copying it (smoother, slower on RTG)
 * ENV:MiniGL/Trace    n: the next n trace lines to the serial port
 * Read again for each new context. */
void mgl_read_prefs(void)
{
    char v[16];
    mgl_prefs.route = -1;
    if (GetVar((STRPTR)"MiniGL/Driver", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0) {
        if (v[0] == 'C' || v[0] == 'c') mgl_prefs.route = ROUTE_CPU;
        else if (v[0] == 'O' || v[0] == 'o' || v[0] == 'G' || v[0] == 'g') mgl_prefs.route = ROUTE_GPU;
    }
    if (GetVar((STRPTR)"MiniGL/ZBuffer", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 && v[0] == '3') mgl_prefs.zbits = 32;
    mgl_prefs.flip = GetVar((STRPTR)"MiniGL/Flip", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 && v[0] == '1';
    mgl_trace = 0;
    if (GetVar((STRPTR)"MiniGL/Trace", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0) StrToLong((STRPTR)v, &mgl_trace);
}

/* ---- tracing --------------------------------------------------------------------------- */

/* Straight to the serial chip, so it works from any task and inside any lock. */
static void serput(REG(d0, UBYTE c), REG(a3, void *data))
{
    volatile UWORD *serdatr = (volatile UWORD *)0xDFF018, *serdat = (volatile UWORD *)0xDFF030;
    long spin = 1000000;
    (void)data;
    if (!c) c = '\n';
    while (!(*serdatr & 0x2000) && --spin) ;
    *serdat = (UWORD)(0x100 | c);
}

void mgl_log(const char *fmt, ...)
{
    va_list ap;
    if (mgl_trace <= 0) return;
    mgl_trace--;
    va_start(ap, fmt);
    RawDoFmt((STRPTR)fmt, (APTR)ap, (void (*)())serput, 0);
    va_end(ap);
}

/* ---- memory ---------------------------------------------------------------------------- */

void *mgl_alloc(ULONG bytes) { return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR); }
void mgl_free(void *p) { if (p) FreeVec(p); }

void mgl_copy(void *d, const void *s, ULONG n)
{
    if (!(((ULONG)d | (ULONG)s | n) & 3)) {
        ULONG *dl = d;
        const ULONG *sl = s;
        n >>= 2;
        while (n--) *dl++ = *sl++;
        return;
    }
    CopyMem((APTR)s, d, n);
}

void mgl_zero(void *d, ULONG n)
{
    UBYTE *p = d;
    while (n && ((ULONG)p & 3)) { *p++ = 0; n--; }
    while (n >= 4) { *(ULONG *)p = 0; p += 4; n -= 4; }
    while (n--) *p++ = 0;
}

/* ---- time, for GLUT_ELAPSED_TIME ------------------------------------------------------- */

ULONG mgl_millis(void)
{
    struct timeval tv;
    if (!TimerBase) {
        struct DateStamp ds;
        DateStamp(&ds);
        return (ULONG)ds.ds_Minute * 60000UL + (ULONG)ds.ds_Tick * 20UL;
    }
    GetSysTime(&tv);
    return tv.tv_secs * 1000UL + tv.tv_micro / 1000UL;
}
