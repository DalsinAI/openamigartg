/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's modules (DESIGN.md section 5, "One library"): the big parts
 * (GL.module, which is Mesa, and SDL2.module) are files in LIBS:OpenGPU/
 * that opengpu.library loads for a program when it first needs them.
 *
 *   APTR handle = OGPU_ModuleOpen("GL", GL_MODULE_VERSION, &table);
 *   ... the program calls through table ...
 *   table's own close call (SDL2's close, GL's close), then
 *   OGPU_ModuleClose(handle);
 *
 * Programs don't do this themselves: their link stub (libSDL2.a, libGL.a)
 * does it on the first call.
 *
 * Opening:
 *   - name is a module's name ("GL" is LIBS:OpenGPU/GL.module), or a path
 *     when it holds ':' or '/' (a test's PROGDIR:Test.module).
 *   - The caller must be a DOS process: a module comes from disk.
 *   - On failure the handle and *table are NULL, and IoErr() says why:
 *     ERROR_OBJECT_NOT_FOUND when the file isn't there, or
 *     ERROR_OBJECT_WRONG_TYPE when the module refused (it is older than
 *     version, or couldn't start).
 *   - opengpu.library 0.5 has these calls; on 0.4 and older a stub loads
 *     the module itself, the same way (LoadSeg, then the entry below).
 *
 * The module's ABI:
 *   - It is a LoadSeg file, and its first code (the first hunk, after its
 *     segment link) is its entry:
 *       struct OGPUModuleTable *entry(struct OGPUModuleArgs *args);
 *     plain C, the argument on the stack and the result in D0, running in
 *     the caller's process.
 *   - It returns its table, or NULL when it won't run (args->version is
 *     newer than it is; no memory).
 *   - Its table starts with its version; what follows is the module's own
 *     (SDL2's dynapi_entry and close, GL's close and exports). The module's
 *     own close is called before OGPU_ModuleClose, which unloads it.
 *   - It isn't a program, so libnix's startup (ncrt0) isn't linked. The
 *     module runs libnix's __INIT_LIST__ itself in its entry and
 *     __EXIT_LIST__ in its close, and links ___initlibraries;
 *     library/modules/common does this for the Team's modules.
 *   - libnix's getenv doesn't work in a module; modules read variables with
 *     dos.library's GetVar (library/modules/common gives a getenv that
 *     does).
 *   - It calls opengpu.library through args->OpenGPUBase, which stays open
 *     while the module is (the caller holds it).
 *
 * Residency: in this first step each program gets its own copy (LoadSeg per
 * OGPU_ModuleOpen), so the module's globals are that program's. In the
 * second step modules are built -fbaserel32: the code is loaded once and
 * OGPU_ModuleOpen makes each program's data. Programs and their stubs don't
 * change for that; a module's threads (SDL's, through CreateNewProc in
 * SDL_systhread.c; Mesa's, through the pthread shim) must then hand A4 on. */
#ifndef OPENGPU_MODULE_H
#define OPENGPU_MODULE_H

#include <exec/types.h>

#define OGPU_MODULE_DIR "LIBS:OpenGPU/"
#define OGPU_MODULE_EXT ".module"

/* opengpu.library's revision from which OGPU_ModuleOpen is there (version 0). */
#define OGPU_MODULE_LIB_REVISION 5

struct ExecBase;
struct Library;

struct OGPUModuleArgs {
    struct ExecBase *SysBase;
    struct Library *DOSBase;
    struct Library *OpenGPUBase;    /* opengpu.library, or NULL from a stub on an older one */
    ULONG version;                  /* the module version the caller was built for */
};

struct OGPUModuleTable {
    ULONG version;                  /* the module's own version */
    /* the module's calls follow */
};

typedef struct OGPUModuleTable *(*OGPUModuleEntry)(struct OGPUModuleArgs *args);

/* The entry of a module loaded with LoadSeg. */
#define OGPU_MODULE_ENTRY(seglist) ((OGPUModuleEntry)((UBYTE *)BADDR(seglist) + 4))

/* A module that hands out its calls by name (GL.module) lists them sorted
 * by name (strcmp order); a stub binds its own sorted list in one pass. */
struct OGPUModuleExport {
    const char *name;
    APTR func;
};

#endif
