/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library's module loader (ogpu_module.c): what opengpu_lib.c's
 * OGPU_ModuleOpen and OGPU_ModuleClose call, and its expunge. The library
 * base holds a struct ogpu_modules: the shared modules now loaded. The
 * loader keeps no globals: exec and dos.library come with each call. */
#ifndef OGPU_MODULE_LOADER_H
#define OGPU_MODULE_LOADER_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include "../../include/opengpu/module.h"

struct ExecBase;
struct DosLibrary;

struct ogpu_modules {
    struct SignalSemaphore lock;
    struct MinList list;                        /* struct ogpu_shared */
    ULONG loaded;                               /* shared modules in the list */
};

void ogpu_modules_init(struct ogpu_modules *m, struct ExecBase *SysBase);
/* The caller is a DOS process, and DOSBase is open. */
APTR ogpu_module_open(struct ogpu_modules *m, struct ExecBase *SysBase, struct DosLibrary *DOSBase,
                      CONST_STRPTR name, ULONG version, APTR *table, struct Library *base);
/* A shared module stays loaded; a module for each program is unloaded. */
void ogpu_module_close(struct ogpu_modules *m, struct ExecBase *SysBase, struct DosLibrary *DOSBase, APTR handle);
/* From the library's expunge (memory is short), under Forbid, from any task:
 * unloads the shared modules no program has open. The number still loaded,
 * or -1 when another task is in the loader now and nothing was done. */
LONG ogpu_modules_flush(struct ogpu_modules *m, struct ExecBase *SysBase, struct DosLibrary *DOSBase);

#endif
