/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GL.module's table (include/opengpu/module.h): what libGL.a (stubs/gl) and
 * the module (library/modules/gl/module) share.
 *
 * The module hands out its calls by name: every GL and GLES entry point
 * Mesa's glapi has, and the GLA calls (gla/gla_core.h and the OS 3 ones),
 * sorted by name. The stub binds its own sorted list against it in one
 * pass, so a program built against an older or newer module still runs: a
 * call the module doesn't have returns 0.
 *
 * Version 2 (8 October 2026): the table starts with struct OGPUModuleTable's
 * A4 (residency step 2: the module is shared, and every call sets A4). */
#ifndef GL_MODULE_H
#define GL_MODULE_H

#include <opengpu/module.h>

#define GL_MODULE_NAME    "GL"
#define GL_MODULE_FILE    "LIBS:OpenGPU/GL.module"
#define GL_MODULE_VERSION 2

struct GLModuleTable {
    struct OGPUModuleTable head;                /* GL_MODULE_VERSION, a4, caller_a4 */
    void (*close)(void);                        /* before OGPU_ModuleClose */
    ULONG count;
    const struct OGPUModuleExport *exports;     /* count of them, sorted by name (strcmp) */
};

#endif
