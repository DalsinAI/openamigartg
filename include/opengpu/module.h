/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPU's modules (DESIGN.md section 5, "One library"): the big parts
 * (GL.module, which is Mesa, and SDL2.module) are files in LIBS:OpenGPU/
 * that opengpu.library loads for a program when it first needs them.
 *
 *   APTR handle = OGPU_ModuleOpen("GL", GL_MODULE_VERSION, &table);
 *   ... the program calls through table, with A4 set (below) ...
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
 *     ERROR_OBJECT_NOT_FOUND when the file isn't there,
 *     ERROR_OBJECT_WRONG_TYPE when the module refused (it is older than
 *     version, or the caller is older than the module's A4 calls: a
 *     version below OGPU_MODULE_A4_VERSION; or it couldn't start), or
 *     ERROR_NO_FREE_STORE.
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
 *     newer than it is, or older than OGPU_MODULE_A4_VERSION; no memory).
 *   - Its table starts with struct OGPUModuleTable; what follows is the
 *     module's own (SDL2's dynapi_entry and close, GL's close and exports).
 *     The module's own close is called before OGPU_ModuleClose.
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
 * Residency (DESIGN.md section 5, "Residency"). A module is one of two
 * kinds, told apart by its first code:
 *   - Shared: built -fbaserel32 -resident32, so its code reaches every
 *     global through A4. Its first code is a BRA.W over a header (struct
 *     OGPUModuleHeader, OGPU_MODULE_SHARED_MAGIC). opengpu.library loads the
 *     file once, the first time a program opens it, and from 0.9 keeps it
 *     loaded after the last program closes it, until the system resets or
 *     memory runs short (the library's expunge unloads the shared modules
 *     no program has open). Each OGPU_ModuleOpen gets a copy of the data and
 *     BSS, with the data-to-data relocations applied, and the module's entry
 *     runs (libnix's init list and the constructors) with A4 on that copy;
 *     OGPU_ModuleClose frees that copy. GL.module and SDL2.module are shared.
 *     A module file replaced while loaded is loaded anew at the next open.
 *   - For each program: built without -fbaserel32; each open is a LoadSeg
 *     of its own, the first code jumps to the entry, and the close unloads
 *     it.
 *   A stub that loads a shared module itself (an opengpu.library older than
 *   0.5) calls its first code as before; the module then sets A4 on the
 *   data LoadSeg gave it, which is that program's alone.
 *
 * A4 (module ABI 2, OGPU_MODULE_A4_VERSION). Every call into a module's
 * table runs with A4 = table->a4 and puts the caller's A4 back afterwards;
 * libSDL2.a and libGL.a do this in each entry, so programs don't change.
 * A module that calls back into the program (SDL's audio callback, timers,
 * a thread's function) sets A4 to table->caller_a4 for the call, which the
 * stub fills in when it opens the module. A program built -fbaserel itself
 * so gets its own A4 in its callbacks; other programs ignore A4. Threads a
 * module starts get the module's A4 from the thread that started them.
 * On a module for each program, A4 means nothing to the module, and setting
 * it does no harm. */
#ifndef OPENGPU_MODULE_H
#define OPENGPU_MODULE_H

#include <exec/types.h>

#define OGPU_MODULE_DIR "LIBS:OpenGPU/"
#define OGPU_MODULE_EXT ".module"

/* opengpu.library's revision from which OGPU_ModuleOpen is there (version 0). */
#define OGPU_MODULE_LIB_REVISION 5

/* The first version of each module's table with struct OGPUModuleTable's
 * a4 and caller_a4 (the Team's modules were all version 1 before). */
#define OGPU_MODULE_A4_VERSION 2

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
    APTR a4;                        /* A4 for every call into this copy (the module sets it) */
    APTR caller_a4;                 /* the program's A4, for the module's calls back (the stub sets it) */
    /* the module's calls follow */
};

typedef struct OGPUModuleTable *(*OGPUModuleEntry)(struct OGPUModuleArgs *args);

/* The entry of a module loaded with LoadSeg (either kind). */
#define OGPU_MODULE_ENTRY(seglist) ((OGPUModuleEntry)((UBYTE *)BADDR(seglist) + 4))

/* A shared module's header, at its first code + 4 (after the BRA.W, 0x6000). */
#define OGPU_MODULE_SHARED_MAGIC 0x4F47534DUL   /* "OGSM" */
#define OGPU_MODULE_A4_OFFSET 0x7ffe            /* A4 = data + 0x7ffe (libnix's -resident32 layout) */
struct OGPUModuleHeader {
    ULONG magic;                    /* OGPU_MODULE_SHARED_MAGIC */
    OGPUModuleEntry entry;          /* the entry, to be called with A4 on a data copy */
    UBYTE *a4_init;                 /* A4 for the data as loaded: its start + OGPU_MODULE_A4_OFFSET */
    ULONG data_size;                /* data and BSS together, bytes */
    const ULONG *relocs;            /* data-to-data relocations: a count, then offsets into the data */
};
#define OGPU_MODULE_HEADER(seglist) ((struct OGPUModuleHeader *)((UBYTE *)BADDR(seglist) + 8))
#define OGPU_MODULE_IS_SHARED(seglist) \
    (*(UWORD *)((UBYTE *)BADDR(seglist) + 4) == 0x6000 && OGPU_MODULE_HEADER(seglist)->magic == OGPU_MODULE_SHARED_MAGIC)

/* A module that hands out its calls by name (GL.module) lists them sorted
 * by name (strcmp order); a stub binds its own sorted list in one pass. */
struct OGPUModuleExport {
    const char *name;
    APTR func;
};

/* Calling a module's table from C (a stub, or a test like ModuleCheck):
 *
 *   { OGPU_A4(table->a4); result = table->call(x, y); }
 *
 * sets A4 for the scope and puts it back as the scope ends, after the
 * result is taken. Only in a file compiled with -ffixed-a4, so the
 * compiler keeps no value of its own in A4 meanwhile; and the call's
 * arguments must not read the program's globals through A4 (a program
 * built -fbaserel): take them into locals first. */
#if defined(__GNUC__) && defined(__mc68000__)
static __inline__ APTR ogpu_a4_swap(APTR a4)
{
    APTR old;
    __asm__ __volatile__("move.l %%a4,%0\n\tmove.l %1,%%a4" : "=&d"(old) : "d"(a4) : "memory");
    return old;
}
static __inline__ void ogpu_a4_restore(APTR *old)
{
    __asm__ __volatile__("move.l %0,%%a4" : : "d"(*old) : "memory");
}
static __inline__ APTR ogpu_a4_get(void)
{
    APTR a4;
    __asm__ __volatile__("move.l %%a4,%0" : "=d"(a4));
    return a4;
}
#define OGPU_A4(a4) APTR ogpu_a4_saved_ __attribute__((cleanup(ogpu_a4_restore))) = ogpu_a4_swap(a4)
#endif

#endif
