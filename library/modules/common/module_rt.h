/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What a module built with libnix needs in place of a program's startup
 * (include/opengpu/module.h): module_start.S first on the link line, then
 * module_rt.c. The module writes ogpu_module_entry:
 *
 *   struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
 *   {
 *       if (!ogpu_module_begin(args, MY_VERSION, &my_table.head)) return NULL;
 *       ... its own start; on failure ogpu_module_end() and NULL ...
 *       return &my_table.head;
 *   }
 *
 * and calls ogpu_module_end() last in its own close call.
 *
 * Built -fbaserel32 -resident32 (a shared module: code once, data per
 * program; OGPU_MODULE_SHARED below) or without (a copy for each program).
 * The same source does both. */
#ifndef OGPU_MODULE_RT_H
#define OGPU_MODULE_RT_H

#include <opengpu/module.h>

struct Task;

#ifdef __baserel32__
#define OGPU_MODULE_SHARED 1
#else
#define OGPU_MODULE_SHARED 0
#endif

/* From the args: opengpu.library (NULL from a stub on an older one). */
extern struct Library *ogpu_module_OpenGPUBase;

/* SysBase and DOSBase from args, then libnix's init list (memory, standard
 * I/O on the calling program's Input and Output, auto-opened libraries,
 * constructors). head is the module's table: its a4 is set here, and its
 * caller_a4 cleared until the stub fills it in. 0 when args->version is
 * newer than `version` or older than OGPU_MODULE_A4_VERSION, or libnix
 * couldn't start. */
int ogpu_module_begin(const struct OGPUModuleArgs *args, ULONG version, struct OGPUModuleTable *head);

/* libnix's exit list (destructors, files left open, memory). */
void ogpu_module_end(void);

/* 1 when fn is the module's own code, 0 when it is the program's (or
 * anyone else's). */
int ogpu_module_owns(const void *fn);

/* A call from the module into code that isn't its own (a program's
 * callback): fn(a, b, c, d) with A4 set to the program's (the table's
 * caller_a4), the module's back afterwards. fn may take fewer arguments.
 * The module's own functions are called directly. On a module for each
 * program it is a plain call. */
long ogpu_module_callout(const void *fn, long a, long b, long c, long d);

/* A thread the module starts (CreateNewProc). Its NP_Entry is made with
 *   OGPU_MODULE_THREAD_ENTRY(my_entry, my_thread)
 * (my_thread is a void function of the module's), and the parent calls
 * ogpu_module_thread_a4(&proc->pr_Task) before the child runs (under Forbid,
 * as it sets the child's tc_UserData). The child's A4 then comes from its
 * tc_TrapData, as for libnix's -resident32 programs, and my_thread runs as
 * the module's own code on the copy that started it. */
void ogpu_module_thread_a4(struct Task *child);
void ogpu_module_thread_run(void (*fn)(void));
#define OGPU_MODULE_THREAD_ENTRY(name, fn) \
    void name(void) { ogpu_module_thread_run(fn); }

/* This copy's A4. */
APTR ogpu_module_a4(void);

#endif
