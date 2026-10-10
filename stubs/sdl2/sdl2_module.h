/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl2) and the module (library/modules/sdl2/module) share
 * it. It is the module ABI of include/opengpu/module.h: an entry taking
 * SysBase, DOSBase, OpenGPUBase and a version, returning the module's table,
 * which starts with struct OGPUModuleTable (its version and the A4 for
 * calls into it).
 *
 * The table's first function is SDL's own SDL_DYNAPI_entry: it fills the
 * caller's SDL jump table, so every SDL_ call a program makes goes to the
 * module's code, with the table's A4 set by the stub.
 *
 * Version 2 (8 October 2026): the A4 calls (residency step 2). A version 1
 * stub is refused, as it would call the module without A4.
 *
 * Version 3 (8 October 2026): set_gl, SDL_GL_* on OpenGPU's GL module. A
 * stub still asks for version 2 (SDL2_MODULE_OPEN_VERSION), so a program
 * runs on a version 2 module too, without OpenGL; it calls set_gl only when
 * the table says 3 or later.
 *
 * Version 4 (10 October 2026): the same table and calls. SDL_RWFromFile opens
 * a file by its full path (PROGDIR:, a relative name and an assign mean the
 * same in every process), and SDL's threads start with their parent's home
 * folder and current directory and no requester window. A stub asks for 2
 * as before, so it runs on any module. */
#ifndef SDL2_MODULE_H
#define SDL2_MODULE_H

#include <exec/types.h>
#include <exec/execbase.h>
#include <opengpu/module.h>

#define SDL2_MODULE_NAME    "SDL2"
#define SDL2_MODULE_FILE    "LIBS:OpenGPU/SDL2.module"
#define SDL2_MODULE_VERSION 4
#define SDL2_MODULE_OPEN_VERSION 2      /* what a stub asks for */
#define SDL2_MODULE_GL_VERSION 3        /* the first table with set_gl */

typedef struct OGPUModuleArgs SDL2ModuleArgs;

struct SDL2GLBridge;

struct SDL2ModuleTable {
    struct OGPUModuleTable head;    /* SDL2_MODULE_VERSION, a4, caller_a4 */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before OGPU_ModuleClose: SDL_Quit, libnix's exit list */
    /* Version 3: the program's GL (below), or NULL for none. */
    void (*set_gl)(const struct SDL2GLBridge *bridge);
};

/* SDL_GL_* on OpenGPU's GL module (GL.module and libGL.a, library/modules/gl).
 *
 * A program has one copy of GL: the one its libGL.a opens. SDL's GL calls
 * must reach that copy, or SDL's context would be in another copy of Mesa
 * from the program's gl* calls. So the program hands SDL its GL: libSDL2.a's
 * SDL2_gl.o (linked when the program calls any SDL_GL_ function, and then
 * needing -lGL) holds these calls, which call libGL.a's GLA entries, and the
 * stub gives them to the module with set_gl after it opens it. The module
 * calls them with the program's A4 (ogpu_module_callout), so each takes at
 * most four arguments. A program that never calls SDL_GL_ links no GL at all.
 *
 * Structures the module and the program share. SDL2GLConfig is GLA's
 * struct gla_config (gla_core.h), field for field. */
#define SDL2_GL_BRIDGE_VERSION 1

struct SDL2GLConfig {
    LONG profile;               /* 0 compatibility, 1 core, 2 GLES */
    LONG major, minor;          /* 0, 0: the highest the profile has */
    LONG doublebuf;
    LONG depth, stencil;        /* bits */
    LONG alpha;
};

/* Where a window's GL frames go. In the module's memory; the module fills
 * rp, left, top, width and height, and the program's half keeps its own
 * state (GLA's present hook and target) in priv. */
struct RastPort;
struct SDL2GLTarget {
    struct RastPort *rp;        /* the window's (GimmeZeroZero: its inside) */
    LONG left, top;             /* the GL area's corner in rp */
    ULONG width, height;
    ULONG priv[8];
};

struct SDL2GLBridge {
    ULONG version;              /* SDL2_GL_BRIDGE_VERSION */
    /* A display: the PC's graphics chip through virgl when opengpu.library
       carries it (and cpu_only is 0), else softpipe on this CPU. */
    APTR (*display)(LONG cpu_only);
    void (*display_destroy)(APTR display);
    const char *(*driver)(APTR display);            /* "virgl" or "softpipe" */
    APTR (*context)(APTR display, const struct SDL2GLConfig *cfg, APTR share);
    void (*context_destroy)(APTR context);
    /* A buffer of target's width and height, shown in target (which must
       outlive it). */
    APTR (*buffer)(APTR display, const struct SDL2GLConfig *cfg, struct SDL2GLTarget *target);
    void (*buffer_resize)(APTR buffer, ULONG width, ULONG height);
    void (*buffer_destroy)(APTR buffer);
    LONG (*make_current)(APTR context, APTR buffer);  /* context NULL: none current */
    /* Finish and show the frame, in target's rp, left and top as they are now. */
    void (*swap)(APTR context, APTR buffer, struct SDL2GLTarget *target);
    APTR (*get_proc_address)(const char *name);
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct OGPUModuleTable *(*SDL2ModuleEntry)(SDL2ModuleArgs *args);

#endif
