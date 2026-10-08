/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library 0.5 (G2 with stream v1.2, DESIGN.md section 5): the library, its
 * built-in CPU back end, which runs ogpu_core.c on the Amiga's own 68k, and
 * the drivers in LIBS:OpenGPU/ (include/opengpu/driver.h). The first time a
 * DOS process asks anything, the library opens the drivers there and keeps
 * the first that finds its hardware (ACRTG.gpu: the Cradle's command ring).
 * From then on every batch goes to that driver, except one with a command
 * the driver doesn't carry out (a newer stream than its hardware), which the
 * CPU runs once the driver's batches are done; the CPU takes over for good
 * if the driver says its hardware has gone. Without a driver, batches
 * are carried out on the CPU as they are submitted, so a fence is done by
 * the time OGPU_Submit returns. OGPU_Wait gives a batch's first error.
 * OGPU_ModuleOpen loads a module from LIBS:OpenGPU/ (GL.module, SDL2.module)
 * for the calling program (include/opengpu/module.h): a shared one once for
 * every program, with data of each program's own (ogpu_module.c).
 * 0.6: OpenGfx inside (library/ogfx, include/opengpu/gfx.h): opengfx.library's
 * calls are the LVOs from 66, and graphics.library's drawing and text patches
 * are thin entries into it.
 * 0.7: OpenGfx's look hook (LVOs 150 and 156): OpenLook draws window frames
 * through OpenGfx's RectFill and Text instead of patching them a second time.
 * 0.8: OpenGfx patches every graphics.library drawing call (22), and OpenRTG
 * provides the fourteen new ones instead of patching them itself.
 * Built bare by library/build.sh.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/memory.h>
#include "ogpu_core.h"
#ifdef OGPU_WITH_3D
#include "ogpu_3d.h"
#endif
#include "../../include/opengpu/opengpu.h"
#include "../../include/opengpu/driver.h"
#include "../../include/opengpu/module.h"
#include "ogpu_module.h"
#include "../ogfx/ogfx.h"
#include <stddef.h>

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#define LIB_VERSION 0
#define LIB_REVISION 8
#define RESULTS 16                              /* the last batches' results, by fence */
#define MAX_DRIVERS 8                           /* files looked at in LIBS:OpenGPU/ */
#define ON_CPU 0                                /* dfence[] for a batch the CPU ran */

struct OpenGPUBase {
    struct Library lib;
    BPTR seglist;
    struct SignalSemaphore lock;                /* fences and results */
    ULONG fence;                                /* the last one given */
    LONG result[RESULTS];
    unsigned long dfence[RESULTS];              /* the driver's fence for that batch, or ON_CPU */
    struct SignalSemaphore drv_lock;            /* the driver: loading it, and each call into it */
    int drv_tried;                              /* LIBS:OpenGPU/ looked at */
    struct Library *drv_lib;                    /* the driver's library, kept open */
    struct OGPUDriver *drv;                     /* its table, or NULL: the CPU does everything */
    UBYTE drv_ok[256];                          /* opcodes the driver carries out (for some format) */
    unsigned long drv_last;                     /* the driver's last fence, before a batch runs on the CPU */
    struct ogpu_modules modules;                /* shared modules loaded (ogpu_module.c) */
    struct ogfx_state ogfx;                     /* OpenGfx (library/ogfx) */
};
const ULONG ogpu_ogfx_at = offsetof(struct OpenGPUBase, ogfx);

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "opengpu.library";
static const char lib_id[] = "opengpu.library 0.8 (8.10.2026) OpenGPU, Dalsin Limited\r\n";
/* For C:Version, which looks for "$VER:" in the file. */
static const char lib_ver[] __attribute__((used)) = "\0$VER: opengpu.library 0.8 (8.10.2026) OpenGPU, Dalsin Limited";
static const char cpu_name[] = "CPU";
static const char dos_name[] = "dos.library";
static const char drv_dir[] = "LIBS:OpenGPU";
/* Tried first, in this order; any other *.gpu there after them. */
static const char *const drv_first[] = { "ACRTG.gpu", "PiStorm.gpu", "AGA.gpu", NULL };

static struct Library *lib_init(REG(d0, struct OpenGPUBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenGPUBase *base));
static BPTR lib_close(REG(a6, struct OpenGPUBase *base));
static BPTR lib_expunge(REG(a6, struct OpenGPUBase *base));
static ULONG lib_null(void);
static ULONG OGPU_Query(REG(d0, ULONG op), REG(d1, ULONG format), REG(a6, struct OpenGPUBase *base));
static STRPTR OGPU_BackEndName(REG(d0, ULONG index), REG(a6, struct OpenGPUBase *base));
static LONG OGPU_Submit(REG(a0, APTR stream), REG(d0, ULONG words), REG(a1, ULONG *fence), REG(a6, struct OpenGPUBase *base));
static LONG OGPU_Wait(REG(d0, ULONG fence), REG(a6, struct OpenGPUBase *base));
static APTR OGPU_ModuleOpen(REG(a0, CONST_STRPTR name), REG(d0, ULONG version), REG(a1, APTR *table), REG(a6, struct OpenGPUBase *base));
static void OGPU_ModuleClose(REG(a0, APTR handle), REG(a6, struct OpenGPUBase *base));
static void drivers_close(struct OpenGPUBase *base);

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)OGPU_Query, (APTR)OGPU_BackEndName, (APTR)OGPU_Submit, (APTR)OGPU_Wait,
    (APTR)OGPU_ModuleOpen, (APTR)OGPU_ModuleClose,
    /* 0.6: OpenGfx, from 66 (library/ogfx/ogfx.h) */
    (APTR)OGFX_Version, (APTR)OGFX_InstallPatches, (APTR)OGFX_SetEnabled, (APTR)OGFX_Status,
    (APTR)OGFX_RegisterProvider, (APTR)OGFX_UnregisterProvider,
    (APTR)OGFX_Text, (APTR)OGFX_TextLength, (APTR)OGFX_TextExtent, (APTR)OGFX_TextFit,
    (APTR)OGFX_RectFill, (APTR)OGFX_BltBitMap, (APTR)OGFX_BltTemplate, (APTR)OGFX_ScrollRaster,
    /* 0.7: OpenGfx's look hook, 150 and 156 */
    (APTR)OGFX_RegisterLook, (APTR)OGFX_UnregisterLook,
    (APTR)-1,
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
    InitSemaphore(&base->drv_lock);
    ogpu_modules_init(&base->modules);
    ogfx_init(&base->ogfx, sys);
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
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP) && ogfx_may_expunge(&base->ogfx))
        return lib_expunge(base);
    return 0;
}
static BPTR lib_expunge(REG(a6, struct OpenGPUBase *base))
{
    /* graphics.library's patched vectors point into the library: it stays. */
    if (base->lib.lib_OpenCnt || !ogfx_may_expunge(&base->ogfx)) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    BPTR seglist = base->seglist;
    drivers_close(base);
    ogfx_expunge(&base->ogfx);
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

#ifdef OGPU_WITH_3D
/* 1 when the batch has a 3D command (stream3d.h's range). */
static int has_3d(const UBYTE *s, ULONG words)
{
    ULONG at = 0;
    while (at < words) {
        UWORD op = (UWORD)(s[at * 4] << 8 | s[at * 4 + 1]), len = (UWORD)(s[at * 4 + 2] << 8 | s[at * 4 + 3]);
        if (op >= 0x0030 && op <= 0x003F) return 1;
        if (!len) return 0;
        at += len;
    }
    return 0;
}
#endif

/* A batch on the 68k: done when this returns. */
static LONG cpu_run(APTR stream, ULONG words)
{
    struct ogpu_core core;      /* on the caller's stack, so callers never wait on each other */
#ifdef OGPU_WITH_3D
    struct ogpu3d *d3 = NULL;   /* 3D state: about 1.7 KB, too much for a small stack, so from memory */
#endif
    ogpu_core_init(&core);
    core.map = cpu_map;
    core.fence = 0;
    core.ext = 0;               /* the CPU back end carries no GPU APIs: OGPU_OP_VIRGL is BADOP */
    core.user = 0;
#ifdef OGPU_WITH_3D
    if (has_3d((const UBYTE *)stream, words) && (d3 = AllocVec(sizeof *d3, MEMF_ANY)) != NULL) {
        ogpu3d_init(d3, &core);
        core.d3 = d3;
    }
#endif
    ogpu_core_run(&core, (const ogpu_u8 *)stream, (long)words);
#ifdef OGPU_WITH_3D
    if (d3) FreeVec(d3);
#endif
    return core.last_error;
}

/* ---- the drivers in LIBS:OpenGPU/ ------------------------------------------------ */

/* dos.library, opened by the first process that needs it (the library may
 * start from ROM, before dos.library is there). */
static int dos_open(struct OpenGPUBase *base)
{
    if (!DOSBase) {
        ObtainSemaphore(&base->drv_lock);
        if (!DOSBase) DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)dos_name, 36);
        ReleaseSemaphore(&base->drv_lock);
    }
    return DOSBase != NULL;
}

/* A driver's first call: its table, or NULL. */
static struct OGPUDriver *driver_get(struct Library *lib)
{
    register struct OGPUDriver *table __asm("d0");
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -30(%%a6)" : "=r"(table) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return table;
}

static int name_is_gpu(const char *n)
{
    int len = 0;
    while (n[len]) len++;
    return len > 4 && n[len - 4] == '.' && (n[len - 3] | 32) == 'g' && (n[len - 2] | 32) == 'p' && (n[len - 1] | 32) == 'u';
}

static int same_name(const char *a, const char *b)
{
    while (*a && (*a | 32) == (*b | 32)) { a++; b++; }
    return *a == 0 && *b == 0;
}

/* Open LIBS:OpenGPU/name; 1 when it is the driver now in use. */
static int driver_try(struct OpenGPUBase *base, const char *name)
{
    char path[64];
    int i = 0, j = 0;
    struct Library *lib;
    struct OGPUDriver *d;
    while (drv_dir[j]) path[i++] = drv_dir[j++];
    path[i++] = '/';
    for (j = 0; name[j] && i < (int)sizeof path - 1; ) path[i++] = name[j++];
    path[i] = 0;
    if (!(lib = OpenLibrary((CONST_STRPTR)path, 0))) return 0;
    d = driver_get(lib);
    if (!d || d->version < OGPU_DRIVER_VERSION || !d->supports || !d->submit || !d->wait) {
        CloseLibrary(lib);
        return 0;
    }
    base->drv_lib = lib;
    base->drv = d;
    {
        int op, f;
        for (op = 0; op < 256; op++) {
            base->drv_ok[op] = 0;
            for (f = 1; f < OGPU_FMT_COUNT && !base->drv_ok[op]; f++)
                if (d->supports(op, f, 0) != OGPU_NONE) base->drv_ok[op] = 1;
        }
        base->drv_ok[OGPU_OP_NOP] = 1;
    }
    return 1;
}

/* The first time a process asks: look in LIBS:OpenGPU/. A plain task (an
 * input handler, a device's task) can't load from disk, so until a process
 * has asked, everything runs on the CPU. */
static void drivers_load(struct OpenGPUBase *base)
{
    struct Task *me;
    static char names[MAX_DRIVERS][32];         /* under drv_lock */
    int n = 0, i, k;
    BPTR lock;
    struct FileInfoBlock *fib;
    if (base->drv_tried) return;
    me = FindTask(NULL);
    if (me->tc_Node.ln_Type != NT_PROCESS) return;
    ObtainSemaphore(&base->drv_lock);
    if (base->drv_tried) { ReleaseSemaphore(&base->drv_lock); return; }
    dos_open(base);
    if (DOSBase && (fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        struct Process *pr = (struct Process *)me;
        APTR win = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;            /* no "Please insert volume" requester */
        if ((lock = Lock((CONST_STRPTR)drv_dir, ACCESS_READ)) != 0) {
            if (Examine(lock, fib) && fib->fib_DirEntryType > 0) {
                while (n < MAX_DRIVERS && ExNext(lock, fib)) {
                    if (fib->fib_DirEntryType >= 0 || !name_is_gpu((const char *)fib->fib_FileName)) continue;
                    for (k = 0; k < 31 && fib->fib_FileName[k]; k++) names[n][k] = fib->fib_FileName[k];
                    names[n][k] = 0;
                    n++;
                }
            }
            UnLock(lock);
        }
        pr->pr_WindowPtr = win;
        FreeDosObject(DOS_FIB, fib);
        /* The known drivers in their order, then the rest as found. */
        for (k = 0; drv_first[k] && !base->drv; k++)
            for (i = 0; i < n; i++)
                if (names[i][0] && same_name(names[i], drv_first[k])) {
                    names[i][0] = 0;
                    driver_try(base, drv_first[k]);
                    break;
                }
        for (i = 0; i < n && !base->drv; i++)
            if (names[i][0]) driver_try(base, names[i]);
    }
    base->drv_tried = 1;
    ReleaseSemaphore(&base->drv_lock);
}

static void drivers_close(struct OpenGPUBase *base)
{
    if (base->drv_lib) CloseLibrary(base->drv_lib);
    base->drv_lib = NULL;
    base->drv = NULL;
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
}

/* 1 when the driver carries out every command in the batch. */
static int driver_takes(struct OpenGPUBase *base, const UBYTE *s, ULONG words)
{
    ULONG at = 0;
    while (at < words) {
        UWORD op = (UWORD)(s[at * 4] << 8 | s[at * 4 + 1]), len = (UWORD)(s[at * 4 + 2] << 8 | s[at * 4 + 3]);
        if (op > 255 || !base->drv_ok[op]) return 0;
        if (!len) return 1;                     /* a bad length: the driver says so, as the CPU would */
        at += len;
    }
    return 1;
}

/* ---- OpenGPU's calls ------------------------------------------------------------- */

static ULONG OGPU_Query(REG(d0, ULONG op), REG(d1, ULONG format), REG(a6, struct OpenGPUBase *base))
{
    int answer = -1;
    drivers_load(base);
    if (base->drv) {
        ObtainSemaphore(&base->drv_lock);
        /* The driver's answer holds for what it carries out; the rest runs here. */
        if (base->drv) answer = base->drv->supports((int)op, (int)format, 0);
        ReleaseSemaphore(&base->drv_lock);
    }
    if (answer > OGPU_NONE) return (ULONG)answer | (1UL << 8);
    return (ULONG)ogpu_core_supports((int)op, (int)format) | (OGPU_BACKEND_CPU << 8);
}

static STRPTR OGPU_BackEndName(REG(d0, ULONG index), REG(a6, struct OpenGPUBase *base))
{
    drivers_load(base);
    if (index == OGPU_BACKEND_CPU) return (STRPTR)cpu_name;
    if (index == 1 && base->drv) return (STRPTR)base->drv->name;
    return NULL;
}

static LONG OGPU_Submit(REG(a0, APTR stream), REG(d0, ULONG words), REG(a1, ULONG *fence), REG(a6, struct OpenGPUBase *base))
{
    LONG r = OGPU_OK;
    ULONG f;
    unsigned long df = ON_CPU;
    int on_driver = 0;
    drivers_load(base);
    if (base->drv) {
        ObtainSemaphore(&base->drv_lock);
        if (base->drv && !driver_takes(base, (const UBYTE *)stream, words)) {
            /* A command the driver doesn't know (a newer stream than its
             * hardware): this batch on the CPU, once the driver's are done. */
            if (base->drv_last) base->drv->wait(base->drv_last);
        } else if (base->drv) {
            on_driver = 1;
            r = base->drv->submit(stream, words, &df);
            base->drv_last = df;
            if (r == OGPU_ERR_DEVICE) {
                /* The hardware has gone: this batch and every later one on the CPU. */
                base->drv = NULL;
                on_driver = 0;
                df = ON_CPU;
            }
        }
        ReleaseSemaphore(&base->drv_lock);
    }
    if (!on_driver) r = cpu_run(stream, words);
    ObtainSemaphore(&base->lock);
    f = ++base->fence;
    if (!f) f = ++base->fence;                  /* 0 is never a fence */
    base->result[f % RESULTS] = r;
    base->dfence[f % RESULTS] = df;
    ReleaseSemaphore(&base->lock);
    if (fence) *fence = f;
    return r;
}

static LONG OGPU_Wait(REG(d0, ULONG fence), REG(a6, struct OpenGPUBase *base))
{
    LONG r = OGPU_OK;
    unsigned long df = ON_CPU;
    ObtainSemaphore(&base->lock);
    /* Only recent results are kept, and an older fence says so rather than
     * claim the batch went well. */
    if (fence && fence <= base->fence) {
        if (base->fence - fence < RESULTS) {
            r = base->result[fence % RESULTS];
            df = base->dfence[fence % RESULTS];
        } else r = OGPU_ERR_EXPIRED;
    }
    ReleaseSemaphore(&base->lock);
    if (df != ON_CPU && r == OGPU_OK) {
        ObtainSemaphore(&base->drv_lock);
        if (base->drv) r = base->drv->wait(df);
        ReleaseSemaphore(&base->drv_lock);
    }
    return r;
}

/* ---- modules (include/opengpu/module.h, ogpu_module.c) --------------------------- */

static APTR OGPU_ModuleOpen(REG(a0, CONST_STRPTR name), REG(d0, ULONG version), REG(a1, APTR *table), REG(a6, struct OpenGPUBase *base))
{
    struct Task *me = FindTask(NULL);
    if (table) *table = NULL;
    if (me->tc_Node.ln_Type != NT_PROCESS || !dos_open(base)) return NULL;
    return ogpu_module_open(&base->modules, name, version, table, &base->lib);
}

/* After the module's own close call. */
static void OGPU_ModuleClose(REG(a0, APTR handle), REG(a6, struct OpenGPUBase *base))
{
    ogpu_module_close(&base->modules, handle);
}
