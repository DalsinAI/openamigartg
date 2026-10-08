/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * opengpu.library's module loader (ogpu_module.c): what opengpu_lib.c's
 * OGPU_ModuleOpen and OGPU_ModuleClose call. The library base holds a
 * struct ogpu_modules: the shared modules now loaded. */
#ifndef OGPU_MODULE_LOADER_H
#define OGPU_MODULE_LOADER_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include "../../include/opengpu/module.h"

struct ogpu_modules {
    struct SignalSemaphore lock;
    struct MinList list;                        /* struct ogpu_shared */
    ULONG loaded;                               /* shared modules in the list */
};

void ogpu_modules_init(struct ogpu_modules *m);
/* The caller is a DOS process with dos.library open (DOSBase). */
APTR ogpu_module_open(struct ogpu_modules *m, CONST_STRPTR name, ULONG version, APTR *table, struct Library *base);
void ogpu_module_close(struct ogpu_modules *m, APTR handle);

#endif
