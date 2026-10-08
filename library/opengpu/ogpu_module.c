/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library's module loader: OGPU_ModuleOpen and OGPU_ModuleClose
 * (include/opengpu/module.h), called from opengpu_lib.c's LVOs, and the
 * flush its expunge calls.
 *
 * Two kinds of module, told apart by the first code (OGPU_MODULE_IS_SHARED):
 *   - Shared (built -fbaserel32): loaded once, the first time a program asks,
 *     and then resident. Each open makes that program's copy of the data and
 *     BSS (with the data-to-data relocations applied) and runs the module's
 *     entry with A4 on it; each close frees that copy. The code stays loaded
 *     after the last close, until the system resets or memory runs short:
 *     only ogpu_modules_flush, from the library's expunge, unloads it.
 *   - For each program (a module not built -fbaserel32): a LoadSeg for every
 *     open, the entry called as it is, and an UnLoadSeg at its close.
 * A shared module is found again by its file: the full name NameFromLock
 * gives (so PROGDIR:Test.module from two programs in different drawers are
 * two modules, and LIBS:OpenGPU/GL.module reached by two names is one), its
 * date and its size. The loader holds no lock on the file, so an installer
 * can replace it. A replaced file is a new module: the next open loads it,
 * programs still on the old one keep it, and the old one goes at once if
 * no program has it open, or at the next flush.
 * Built bare with the library (library/build.sh): no C library, no globals. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ogpu_module.h"

#define MODULE_MAGIC 0x4F474D44UL               /* "OGMD": a handle this library gave */
#define NAME_MAX 256                            /* a module's full name, with its volume */

/* A shared module, loaded once and kept. */
struct ogpu_shared {
    struct MinNode node;
    BPTR seg;
    struct OGPUModuleHeader *hdr;
    ULONG users;                                /* opens not yet closed */
    struct DateStamp date;                      /* the file's, when it was loaded */
    LONG size;
    UBYTE replaced;                             /* the file has changed since: never found again */
    char name[1];                               /* the file's full name (allocated to fit) */
};

/* What OGPU_ModuleOpen gives a program. */
struct ogpu_module {
    ULONG magic;
    BPTR seg;                                   /* a module for each program: its own LoadSeg */
    struct ogpu_shared *shared;                 /* a shared module: the loaded one ... */
    UBYTE *data;                                /* ... and this program's copy of its data */
    struct OGPUModuleTable *table;
};

void ogpu_modules_init(struct ogpu_modules *m, struct ExecBase *SysBase)
{
    InitSemaphore(&m->lock);
    m->list.mlh_Head = (struct MinNode *)&m->list.mlh_Tail;
    m->list.mlh_Tail = NULL;
    m->list.mlh_TailPred = (struct MinNode *)&m->list.mlh_Head;
    m->loaded = 0;
}

/* The entry with A4 on a copy of the data. The module's code is C built
 * -fbaserel32: it keeps A4 as it is, and may change D0-D1/A0-A1 (and FP0-FP1). */
static struct OGPUModuleTable *call_entry(OGPUModuleEntry entry, struct OGPUModuleArgs *args, APTR a4)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1") = (ULONG)a4;
    register APTR a0 __asm("a0") = (APTR)entry;
    register APTR a1 __asm("a1") = args;
    __asm volatile ("move.l %%a4,-(%%sp)\n\t"
                    "move.l %%d1,%%a4\n\t"
                    "move.l %%a1,-(%%sp)\n\t"
                    "jsr (%%a0)\n\t"
                    "addq.l #4,%%sp\n\t"
                    "move.l (%%sp)+,%%a4"
                    : "=r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : : "cc", "memory");
    return (struct OGPUModuleTable *)d0;
}

/* name -> path: LIBS:OpenGPU/name.module, or name itself when it holds ':' or
 * '/'. 0 when it doesn't fit. */
static int module_path(CONST_STRPTR name, char *path, int size)
{
    static const char dir[] = OGPU_MODULE_DIR, ext[] = OGPU_MODULE_EXT;
    int i = 0, j, is_path = 0;
    for (j = 0; name[j]; j++)
        if (name[j] == ':' || name[j] == '/') is_path = 1;
    if (!is_path)
        for (j = 0; dir[j]; j++) path[i++] = dir[j];
    for (j = 0; name[j] && i < size - 8; j++) path[i++] = (char)name[j];
    if (name[j]) return 0;
    if (!is_path)
        for (j = 0; ext[j]; j++) path[i++] = ext[j];
    path[i] = 0;
    return 1;
}

/* AmigaDOS names: the same letters, either case. */
static int same_name(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static int same_date(const struct DateStamp *a, const struct DateStamp *b)
{
    return a->ds_Days == b->ds_Days && a->ds_Minute == b->ds_Minute && a->ds_Tick == b->ds_Tick;
}

/* Out of the list and out of memory. Under the lock, with no program on it.
 * UnLoadSeg only frees memory (the Team's modules have no overlays), so this
 * is safe from the expunge too. */
static void shared_unload(struct ogpu_modules *mods, struct ExecBase *SysBase, struct DosLibrary *DOSBase, struct ogpu_shared *s)
{
    s->node.mln_Pred->mln_Succ = s->node.mln_Succ;
    s->node.mln_Succ->mln_Pred = s->node.mln_Pred;
    mods->loaded--;
    UnLoadSeg(s->seg);
    FreeVec(s);
}

/* This program's copy of a shared module's data: the data as loaded (which
 * nothing writes: every opener works on a copy), with each pointer from
 * the data into the data moved to the copy. Its A4, or NULL. */
static UBYTE *data_copy(struct ExecBase *SysBase, const struct OGPUModuleHeader *h, UBYTE **mem)
{
    const UBYTE *orig = h->a4_init - OGPU_MODULE_A4_OFFSET;
    const ULONG *r = h->relocs;
    ULONG n, delta;
    UBYTE *copy = AllocVec(h->data_size ? h->data_size : 4, MEMF_ANY);
    if (!(*mem = copy)) return NULL;
    CopyMem((APTR)orig, copy, h->data_size);
    delta = (ULONG)copy - (ULONG)orig;
    for (n = r ? *r++ : 0; n; n--, r++)
        *(ULONG *)(copy + *r) += delta;
    return copy + OGPU_MODULE_A4_OFFSET;
}

/* The file: its full name (or path, when NameFromLock can't say), date and
 * size. 0 when it isn't there, with IoErr() saying why. */
static int file_of(struct DosLibrary *DOSBase, const char *path, char *full, struct DateStamp *date, LONG *size)
{
    BPTR lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
    struct FileInfoBlock *fib;
    int i, ok = 0;
    if (!lock) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib) && fib->fib_DirEntryType < 0) {
            *date = fib->fib_Date;
            *size = fib->fib_Size;
            ok = 1;
        } else
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        FreeDosObject(DOS_FIB, fib);
    } else
        SetIoErr(ERROR_NO_FREE_STORE);
    if (ok && !NameFromLock(lock, (STRPTR)full, NAME_MAX)) {
        for (i = 0; path[i] && i < NAME_MAX - 1; i++) full[i] = path[i];
        full[i] = 0;
    }
    UnLock(lock);
    return ok;
}

static APTR open_fail(struct ExecBase *SysBase, struct DosLibrary *DOSBase, struct ogpu_module *m, char *full, LONG err)
{
    if (full) FreeVec(full);
    if (m) FreeVec(m);
    SetIoErr(err);
    return NULL;
}

APTR ogpu_module_open(struct ogpu_modules *mods, struct ExecBase *SysBase, struct DosLibrary *DOSBase,
                      CONST_STRPTR name, ULONG version, APTR *table, struct Library *base)
{
    char path[NAME_MAX], *full;
    struct ogpu_module *m;
    struct ogpu_shared *s = NULL;
    struct OGPUModuleArgs args;
    struct OGPUModuleTable *t;
    struct DateStamp date;
    LONG size = 0, e;
    BPTR seg = 0;
    if (table) *table = NULL;
    if (!name || !table) return NULL;
    if (!module_path(name, path, sizeof path)) return open_fail(SysBase, DOSBase, NULL, NULL, ERROR_OBJECT_NOT_FOUND);
    m = AllocVec(sizeof *m, MEMF_ANY | MEMF_CLEAR);
    full = AllocVec(NAME_MAX, MEMF_ANY);
    if (!m || !full) return open_fail(SysBase, DOSBase, m, full, ERROR_NO_FREE_STORE);
    if (!file_of(DOSBase, path, full, &date, &size)) {
        e = IoErr();
        return open_fail(SysBase, DOSBase, m, full, e ? e : ERROR_OBJECT_NOT_FOUND);
    }

    /* Loaded already (a shared module), or loaded now. Nothing here allocates
     * while the list is walked: an allocation that fails runs the expunge,
     * which may unload what has no users. */
    ObtainSemaphore(&mods->lock);
    {
        struct MinNode *n, *next;
        for (n = mods->list.mlh_Head; (next = n->mln_Succ) != NULL; n = next) {
            struct ogpu_shared *x = (struct ogpu_shared *)n;
            if (x->replaced || !same_name(x->name, full)) continue;
            if (same_date(&x->date, &date) && x->size == size) {
                s = x;
                s->users++;
            } else {
                /* The file was replaced: the old code serves the programs
                 * still on it, and goes now if there are none. */
                x->replaced = 1;
                if (!x->users) shared_unload(mods, SysBase, DOSBase, x);
            }
            break;
        }
    }
    if (!s) {
        if (!(seg = LoadSeg((CONST_STRPTR)full))) {
            e = IoErr();
            ReleaseSemaphore(&mods->lock);
            return open_fail(SysBase, DOSBase, m, full, e ? e : ERROR_OBJECT_NOT_FOUND);
        }
        if (OGPU_MODULE_IS_SHARED(seg)) {
            int len = 0;
            while (full[len]) len++;
            if (!(s = AllocVec(sizeof *s + len, MEMF_ANY | MEMF_CLEAR))) {
                ReleaseSemaphore(&mods->lock);
                UnLoadSeg(seg);
                return open_fail(SysBase, DOSBase, m, full, ERROR_NO_FREE_STORE);
            }
            CopyMem(full, s->name, len + 1);
            s->seg = seg;
            s->hdr = OGPU_MODULE_HEADER(seg);
            s->users = 1;
            s->date = date;
            s->size = size;
            s->node.mln_Succ = (struct MinNode *)&mods->list.mlh_Tail;
            s->node.mln_Pred = mods->list.mlh_TailPred;
            mods->list.mlh_TailPred->mln_Succ = &s->node;
            mods->list.mlh_TailPred = &s->node;
            mods->loaded++;
            seg = 0;
        }                                       /* else a module for each program: m->seg is its own */
    }
    ReleaseSemaphore(&mods->lock);
    FreeVec(full);

    args.SysBase = SysBase;
    args.DOSBase = &DOSBase->dl_lib;
    args.OpenGPUBase = base;
    args.version = version;
    if (s) {
        UBYTE *a4 = data_copy(SysBase, s->hdr, &m->data);
        t = a4 ? call_entry(s->hdr->entry, &args, a4) : NULL;
        if (!t) {
            if (m->data) FreeVec(m->data);
            ObtainSemaphore(&mods->lock);
            s->users--;                         /* it stays loaded */
            ReleaseSemaphore(&mods->lock);
            return open_fail(SysBase, DOSBase, m, NULL, a4 ? ERROR_OBJECT_WRONG_TYPE : ERROR_NO_FREE_STORE);
        }
        m->shared = s;
    } else {
        t = OGPU_MODULE_ENTRY(seg)(&args);
        if (!t) {
            UnLoadSeg(seg);
            return open_fail(SysBase, DOSBase, m, NULL, ERROR_OBJECT_WRONG_TYPE);
        }
        m->seg = seg;
    }
    m->magic = MODULE_MAGIC;
    m->table = t;
    *table = t;
    return m;
}

void ogpu_module_close(struct ogpu_modules *mods, struct ExecBase *SysBase, struct DosLibrary *DOSBase, APTR handle)
{
    struct ogpu_module *m = handle;
    if (!m || m->magic != MODULE_MAGIC) return;
    m->magic = 0;
    if (m->shared) {
        FreeVec(m->data);
        ObtainSemaphore(&mods->lock);
        m->shared->users--;                     /* the code stays loaded for the next program */
        ReleaseSemaphore(&mods->lock);
    } else
        UnLoadSeg(m->seg);
    FreeVec(m);
}

LONG ogpu_modules_flush(struct ogpu_modules *mods, struct ExecBase *SysBase, struct DosLibrary *DOSBase)
{
    struct MinNode *n, *next;
    LONG left = 0;
    if (!mods->loaded) return 0;
    /* Never wait here (exec's low-memory handlers mustn't): a task opening
     * or closing a module now means another time. When the task that holds
     * the lock is the one whose allocation failed (a LoadSeg in
     * ogpu_module_open), the lock is its own and this goes ahead: the open
     * isn't walking the list then, and what it opens has a user already. */
    if (!AttemptSemaphore(&mods->lock)) return -1;
    for (n = mods->list.mlh_Head; (next = n->mln_Succ) != NULL; n = next) {
        struct ogpu_shared *s = (struct ogpu_shared *)n;
        if (s->users) left++;
        else shared_unload(mods, SysBase, DOSBase, s);
    }
    ReleaseSemaphore(&mods->lock);
    return left;
}
