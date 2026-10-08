/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What a module built with libnix needs in place of a program's startup
 * (include/opengpu/module.h): module_start.s first on the link line, then
 * module_rt.c. The module writes ogpu_module_entry:
 *
 *   struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
 *   {
 *       if (!ogpu_module_begin(args, MY_VERSION)) return NULL;
 *       ... its own start; on failure ogpu_module_end() and NULL ...
 *       return &my_table;
 *   }
 *
 * and calls ogpu_module_end() last in its own close call. */
#ifndef OGPU_MODULE_RT_H
#define OGPU_MODULE_RT_H

#include <opengpu/module.h>

/* From the args: opengpu.library (NULL from a stub on an older one). */
extern struct Library *ogpu_module_OpenGPUBase;

/* SysBase and DOSBase from args, then libnix's init list (memory, standard
 * I/O on the calling program's Input and Output, auto-opened libraries,
 * constructors). 0 when args->version is newer than `version` or libnix
 * couldn't start. */
int ogpu_module_begin(const struct OGPUModuleArgs *args, ULONG version);

/* libnix's exit list (destructors, files left open, memory). */
void ogpu_module_end(void);

#endif
