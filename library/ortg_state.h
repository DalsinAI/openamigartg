/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openrtg.library's state (0.18): everything the library writes.
 *
 * The library keeps no data or BSS in its file, so the same file runs from
 * a ROM (`library/build.sh module`) as from LIBS:. What it writes, the
 * patches' old vectors, the display database's records, the screens' video
 * RAM blocks, pen tables and the rest (0.17's file had 28,912 bytes of BSS,
 * measured on lab t5-rtg's copy, 15 October 2026), is in one block lib_init
 * allocates. Its address is in A4 in all of
 * the library's code: every file is built -ffixed-a4 and includes this
 * header first, and each way in from outside goes through an entry stub
 * (ortg_stub.h) that loads A4. displaydb.c, screens.c and pixels.c each
 * keep their own part of the block (ortg_S->db, ->scr, ->pix), as a struct
 * only that file knows.
 *
 * This header must come before any proto/ header: the libraries' bases are
 * read from the block too (EXEC_BASE_NAME and the rest).
 */
#ifndef ORTG_STATE_H
#define ORTG_STATE_H

#include <exec/types.h>
#include "ortg_stub.h"

struct ExecBase;
struct GfxBase;
struct IntuitionBase;
struct Library;
struct OpenRTGBase;

struct ortg_state {
    struct ExecBase *sys;
    struct GfxBase *gfx;
    struct IntuitionBase *intuition;
    struct Library *utility;
    struct Library *opengpu;
    struct OpenRTGBase *base;           /* the library's base */
    volatile ULONG palette_gen;         /* screens.c's, read by pixels.c */
    void *db, *scr, *pix;               /* displaydb.c's, screens.c's and pixels.c's own parts */
    struct ortg_stubs stubs;            /* the room for entry stubs, at the block's end */
};

register struct ortg_state *ortg_S __asm__("a4");

#define EXEC_BASE_NAME      (ortg_S->sys)
#define GRAPHICS_BASE_NAME  (ortg_S->gfx)
#define INTUITION_BASE_NAME (ortg_S->intuition)
#define UTILITY_BASE_NAME   (ortg_S->utility)
#define OPENGPU_BASE_NAME   (ortg_S->opengpu)

/* The sizes of each file's part, for lib_init. */
ULONG ortg_displaydb_size(void);
ULONG ortg_screens_size(void);
ULONG ortg_pixels_size(void);

/* An entry stub for a function called from outside the library (NULL when the
 * room for stubs has run out). */
#define ortg_entry(fn) ortg_stub_reg(&ortg_S->stubs, ortg_S, (APTR)(fn))
#define ortg_entry_c2(fn) ortg_stub_c2(&ortg_S->stubs, ortg_S, (APTR)(fn))

#endif
