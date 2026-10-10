/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL 2's satellite modules: SDL2_image.module and SDL2_mixer.module in
 * LIBS:OpenGPU/, beside SDL2.module. Shared and resident like it
 * (include/opengpu/module.h): their code is loaded once for every program
 * and stays loaded; each program's open gets its own copy of their data.
 * Programs link -lSDL2_image or -lSDL2_mixer (a stub, stubs/sdl2/sat_stub.h)
 * with -lSDL2, and use SDL_image.h and SDL_mixer.h unchanged.
 *
 * A satellite calls SDL through the program's own SDL: its stub hands the
 * module the program's SDL jump table (libSDL2.a's, filled by SDL2.module)
 * and SDL2.module's A4 for this program, and the module's SDL calls
 * (library/modules/sdl2/satellites/sat_sdl.c) set that A4 for each call. So
 * a program has one SDL: its surfaces, RWops and audio device are the same
 * ones whether it or the satellite made them.
 *
 * The module's table after struct OGPUModuleTable:
 *   bind   the program's SDL (its jump table, that table's size, SDL2.module's
 *          A4), then fills the stub's own jump table (the library's functions
 *          in the order of its ABI file, abi/SDL_*_procs.h). Returns how many
 *          bytes of the stub's table it filled: a stub built against a newer
 *          header than the module's knows the module is too old.
 *   close  before OGPU_ModuleClose. sdl_alive is 1 while the program's SDL
 *          is still open: the satellite then closes what it opened in SDL
 *          (SDL2_mixer's audio device); with 0 it calls no SDL.
 * libSDL2.a closes the satellites before SDL itself (SDL2Stub_AtClose), so
 * a satellite's close always finds SDL open, whatever order the program's
 * destructors run in.
 *
 * Calls from SDL into a satellite (SDL2_mixer's audio callback) come with
 * the program's A4 (SDL2.module calls anything outside itself that way);
 * the satellite passes SDL an entry that sets its own A4 first
 * (struct SatCallin, sat_entry.S). */
#ifndef SAT_MODULE_H
#define SAT_MODULE_H

#include <exec/types.h>
#include <opengpu/module.h>

#define SAT_MODULE_VERSION 3            /* what the modules give */
#define SAT_MODULE_OPEN_VERSION 2       /* what a stub asks for: the first, with module ABI 2's A4.
                                           Version 3 (10 October 2026): the same table; a file name
                                           goes on as a full path (library/modules/common/ogpu_path.h). */

struct SatModuleTable {
    struct OGPUModuleTable head;        /* SAT_MODULE_VERSION, a4, caller_a4 */
    ULONG (*bind)(const void *sdl_table, ULONG sdl_size, APTR sdl_a4, void *table, ULONG size);
    void (*close)(LONG sdl_alive);
};

typedef struct OGPUModuleTable *(*SatModuleEntry)(struct OGPUModuleArgs *args);

#endif
