/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library's module loader: OGPU_ModuleOpen and OGPU_ModuleClose
 * (include/opengpu/module.h), called from opengpu_lib.c's LVOs.
 *
 * Two kinds of module, told apart by the first code (OGPU_MODULE_IS_SHARED):
 *   - Shared (built -fbaserel32, step 2 of the residency plan): loaded once
 *     and kept in a list until the last program closes it. Each open makes
 *     that program's copy of the data and BSS (with the data-to-data
 *     relocations applied) and runs the module's entry with A4 on it.
 *   - For each program (step 1): a LoadSeg for every open, and the entry
 *     called as it is.
 * A shared module is found again by its file (SameLock on a lock kept while
 * it is loaded), so PROGDIR:Test.module from two programs in different
 * drawers are two modules, and LIBS:OpenGPU/GL.module reached by two names
 * is one. The lock also keeps the file from being replaced while it runs.
 * Built bare with the library (library/build.sh): no C library. */
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

/* A shared module, loaded once. */
struct ogpu_shared {
    struct MinNode node;
    BPTR lock;                                  /* the file, while it is loaded */
    BPTR seg;
    struct OGPUModuleHeader *hdr;
    ULONG users;                                /* opens not yet closed */
};

/* What OGPU_ModuleOpen gives a program. */
struct ogpu_module {
    ULONG magic;
    BPTR seg;                                   /* a module for each program: its own LoadSeg */
    struct ogpu_shared *shared;                 /* a shared module: the loaded one ... */
    UBYTE *data;                                /* ... and this program's copy of its data */
    struct OGPUModuleTable *table;
};

void ogpu_modules_init(struct ogpu_modules *m)
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

/* One fewer user of a shared module: the last unloads it. Under the lock. */
static void shared_release(struct ogpu_modules *mods, struct ogpu_shared *s)
{
    if (--s->users) return;
    s->node.mln_Pred->mln_Succ = s->node.mln_Succ;
    s->node.mln_Succ->mln_Pred = s->node.mln_Pred;
    mods->loaded--;
    UnLoadSeg(s->seg);
    UnLock(s->lock);
    FreeVec(s);
}

/* This program's copy of a shared module's data: the data as loaded (which
 * nothing writes: every opener works on a copy), with each pointer from
 * the data into the data moved to the copy. Its A4, or NULL. */
static UBYTE *data_copy(const struct OGPUModuleHeader *h, UBYTE **mem)
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

APTR ogpu_module_open(struct ogpu_modules *mods, CONST_STRPTR name, ULONG version, APTR *table, struct Library *base)
{
    char path[256];
    struct ogpu_module *m;
    struct ogpu_shared *s = NULL;
    struct OGPUModuleArgs args;
    struct OGPUModuleTable *t;
    BPTR lock, seg = 0;
    if (table) *table = NULL;
    if (!name || !table) return NULL;
    if (!module_path(name, path, sizeof path)) { SetIoErr(ERROR_OBJECT_NOT_FOUND); return NULL; }
    if (!(m = AllocVec(sizeof *m, MEMF_ANY | MEMF_CLEAR))) { SetIoErr(ERROR_NO_FREE_STORE); return NULL; }
    if (!(lock = Lock((CONST_STRPTR)path, SHARED_LOCK))) {
        LONG e = IoErr();
        FreeVec(m);
        SetIoErr(e ? e : ERROR_OBJECT_NOT_FOUND);
        return NULL;
    }

    /* Loaded already (a shared module), or loaded now. */
    ObtainSemaphore(&mods->lock);
    {
        struct MinNode *n;
        for (n = mods->list.mlh_Head; n->mln_Succ; n = n->mln_Succ)
            if (SameLock(((struct ogpu_shared *)n)->lock, lock) == LOCK_SAME) {
                s = (struct ogpu_shared *)n;
                s->users++;
                break;
            }
    }
    if (s) UnLock(lock);
    else if (!(seg = LoadSeg((CONST_STRPTR)path))) {
        LONG e = IoErr();
        ReleaseSemaphore(&mods->lock);
        UnLock(lock);
        FreeVec(m);
        SetIoErr(e ? e : ERROR_OBJECT_NOT_FOUND);
        return NULL;
    } else if (OGPU_MODULE_IS_SHARED(seg)) {
        if (!(s = AllocVec(sizeof *s, MEMF_ANY | MEMF_CLEAR))) {
            ReleaseSemaphore(&mods->lock);
            UnLoadSeg(seg);
            UnLock(lock);
            FreeVec(m);
            SetIoErr(ERROR_NO_FREE_STORE);
            return NULL;
        }
        s->lock = lock;
        s->seg = seg;
        s->hdr = OGPU_MODULE_HEADER(seg);
        s->users = 1;
        s->node.mln_Succ = (struct MinNode *)&mods->list.mlh_Tail;
        s->node.mln_Pred = mods->list.mlh_TailPred;
        mods->list.mlh_TailPred->mln_Succ = &s->node;
        mods->list.mlh_TailPred = &s->node;
        mods->loaded++;
        seg = 0;
    } else
        UnLock(lock);                           /* a module for each program: m->seg is its own */
    ReleaseSemaphore(&mods->lock);

    args.SysBase = SysBase;
    args.DOSBase = &DOSBase->dl_lib;
    args.OpenGPUBase = base;
    args.version = version;
    if (s) {
        UBYTE *a4 = data_copy(s->hdr, &m->data);
        t = a4 ? call_entry(s->hdr->entry, &args, a4) : NULL;
        if (!t) {
            if (m->data) FreeVec(m->data);
            ObtainSemaphore(&mods->lock);
            shared_release(mods, s);
            ReleaseSemaphore(&mods->lock);
            FreeVec(m);
            SetIoErr(a4 ? ERROR_OBJECT_WRONG_TYPE : ERROR_NO_FREE_STORE);
            return NULL;
        }
        m->shared = s;
    } else {
        t = OGPU_MODULE_ENTRY(seg)(&args);
        if (!t) {
            UnLoadSeg(seg);
            FreeVec(m);
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return NULL;
        }
        m->seg = seg;
    }
    m->magic = MODULE_MAGIC;
    m->table = t;
    *table = t;
    return m;
}

void ogpu_module_close(struct ogpu_modules *mods, APTR handle)
{
    struct ogpu_module *m = handle;
    if (!m || m->magic != MODULE_MAGIC) return;
    m->magic = 0;
    if (m->shared) {
        FreeVec(m->data);
        ObtainSemaphore(&mods->lock);
        shared_release(mods, m->shared);
        ReleaseSemaphore(&mods->lock);
    } else
        UnLoadSeg(m->seg);
    FreeVec(m);
}
