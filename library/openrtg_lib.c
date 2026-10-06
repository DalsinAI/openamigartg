/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openrtg.library 0.1: phase 2's first part (DESIGN.md, sections 4 and 10).
 * It finds the ACRTG boards, one per RTG monitor (serial number = monitor),
 * and builds each monitor's mode table once (modes.c), so the display
 * database calls can be answered from it. It does not claim the boards yet:
 * Picasso96's acrtg.card still drives them until OpenRTG opens screens of
 * its own. Built bare by library/build.sh.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include "modes.h"
#include "displaydb.h"
#include "screens.h"
#include "pixels.h"
#include "../include/openrtg/openrtg.h"

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#define LIB_VERSION 0
#define LIB_REVISION 8

/* The ACRTG board (amigachrome's common/protocol/acrtg.h): Zorro III,
 * Dalsin (0xDA15; 2011 before 1 October 2026), product 9; video RAM at
 * +0x10000, 0xFE0000 bytes; pictures up to 1920x1200 in 8, 16 and 32-bit. */
#define DALSIN          0xDA15
#define DALSIN_OLD      2011
#define ACRTG_PRODUCT   9
#define ACRTG_VRAM      0x00FE0000UL
#define ACRTG_MAX_W     1920
#define ACRTG_MAX_H     1200

struct OpenRTGBase {
    struct Library lib;
    BPTR seglist;
    ULONG monitors;                                     /* RTG monitors found */
    struct ConfigDev *board[ORTG_MAX_MONITORS + 1];     /* [n]: monitor n's board */
    struct ortg_mode_table *table[ORTG_MAX_MONITORS + 1];
    struct Library *gfx;
    int patched;
};

struct ExecBase *SysBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "openrtg.library";
static const char lib_id[] = "openrtg.library 0.8 (6.10.2026) OpenRTG, Dalsin Limited\r\n";

static struct Library *lib_init(REG(d0, struct OpenRTGBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenRTGBase *base));
static BPTR lib_close(REG(a6, struct OpenRTGBase *base));
static BPTR lib_expunge(REG(a6, struct OpenRTGBase *base));
static ULONG lib_null(void);
static ULONG ORTG_MonitorCount(REG(a6, struct OpenRTGBase *base));
static ULONG ORTG_NextMode(REG(d0, ULONG monitor), REG(d1, ULONG previous), REG(a6, struct OpenRTGBase *base));
static BOOL ORTG_GetMode(REG(d0, ULONG mode_id), REG(a0, struct OpenRTGMode *out), REG(a6, struct OpenRTGBase *base));
static ULONG ORTG_BestMode(REG(d0, ULONG monitor), REG(d1, ULONG width), REG(d2, ULONG height), REG(d3, ULONG depth), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_SetModeList(REG(d0, ULONG monitor), REG(d1, ULONG all), REG(a6, struct OpenRTGBase *base));
static APTR ORTG_BoardAddress(REG(d0, ULONG monitor), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_DisplayDatabase(REG(d0, ULONG on), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_Screens(REG(d0, ULONG on), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_WritePixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGPixels *px), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_ReadPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, struct OpenRTGPixels *px), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_FillPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(d2, LONG w), REG(d3, LONG h), REG(d4, ULONG argb), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_InvertPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(d2, LONG w), REG(d3, LONG h), REG(a6, struct OpenRTGBase *base));
static BOOL ORTG_BitMapInfo(REG(a0, struct BitMap *bm), REG(a1, struct OpenRTGBitMapInfo *info), REG(a6, struct OpenRTGBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)ORTG_MonitorCount, (APTR)ORTG_NextMode, (APTR)ORTG_GetMode,
    (APTR)ORTG_BestMode, (APTR)ORTG_SetModeList, (APTR)ORTG_BoardAddress, (APTR)ORTG_DisplayDatabase, (APTR)ORTG_Screens,
    (APTR)ORTG_WritePixels, (APTR)ORTG_ReadPixels, (APTR)ORTG_FillPixels, (APTR)ORTG_InvertPixels, (APTR)ORTG_BitMapInfo, (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct OpenRTGBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static const struct ortg_caps acrtg_caps = { ACRTG_VRAM, ACRTG_MAX_W, ACRTG_MAX_H, 7 };

static void find_boards(struct OpenRTGBase *base) {
    struct Library *ExpansionBase = OpenLibrary("expansion.library", 37);
    static const ULONG makers[2] = { DALSIN, DALSIN_OLD };
    if (!ExpansionBase) return;
    for (int k = 0; k < 2; k++) {
        struct ConfigDev *cd = NULL;
        while ((cd = FindConfigDev(cd, makers[k], ACRTG_PRODUCT)) != NULL) {
            ULONG n = cd->cd_Rom.er_SerialNumber;          /* serial = monitor */
            if (n < 1 || n > ORTG_MAX_MONITORS || base->board[n] || !cd->cd_BoardAddr) continue;
            struct ortg_mode_table *t = AllocVec(sizeof *t, MEMF_ANY | MEMF_CLEAR);
            if (!t) continue;
            ortg_build_modes(t, (int)n, &acrtg_caps, 0);   /* Standard, the default */
            base->board[n] = cd; base->table[n] = t; base->monitors++;
        }
    }
    CloseLibrary(ExpansionBase);
}

static void free_tables(struct OpenRTGBase *base) {
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
        if (base->table[n]) { FreeVec(base->table[n]); base->table[n] = NULL; }
}

/* ---- the library ---------------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct OpenRTGBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    find_boards(base);
    base->gfx = OpenLibrary("graphics.library", 39);
    return &base->lib;
}
static struct Library *lib_open(REG(a6, struct OpenRTGBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}
static BPTR lib_close(REG(a6, struct OpenRTGBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}
static BPTR lib_expunge(REG(a6, struct OpenRTGBase *base))
{
    if (base->lib.lib_OpenCnt || base->patched) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }   /* patched: stays */
    BPTR seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    free_tables(base);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}
static ULONG lib_null(void) { return 0; }

/* ---- OpenRTG's calls ------------------------------------------------------------ */

static struct ortg_mode_table *table_of(struct OpenRTGBase *base, ULONG monitor) {
    return monitor >= 1 && monitor <= ORTG_MAX_MONITORS ? base->table[monitor] : NULL;
}

static ULONG ORTG_MonitorCount(REG(a6, struct OpenRTGBase *base))
{
    return base->monitors;
}

/* The listed mode after `previous` (0: the first); 0 after the last. */
static ULONG ORTG_NextMode(REG(d0, ULONG monitor), REG(d1, ULONG previous), REG(a6, struct OpenRTGBase *base))
{
    struct ortg_mode_table *t = table_of(base, monitor);
    if (!t || !t->count) return 0;
    if (!previous) return t->modes[0].mode_id;
    for (int i = 0; i + 1 < t->count; i++)
        if (t->modes[i].mode_id == previous) return t->modes[i + 1].mode_id;
    return 0;
}

static BOOL ORTG_GetMode(REG(d0, ULONG mode_id), REG(a0, struct OpenRTGMode *out), REG(a6, struct OpenRTGBase *base))
{
    for (ULONG n = 1; n <= ORTG_MAX_MONITORS; n++) {
        const struct ortg_mode *m = base->table[n] ? ortg_find_mode(base->table[n], mode_id) : NULL;
        if (!m) continue;
        if (out) {
            out->mode_id = m->mode_id; out->width = m->width; out->height = m->height;
            out->depth = m->depth; out->format = m->format; out->standard = m->standard;
            for (int i = 0; i < ORTG_NAME_LEN; i++) out->name[i] = m->name[i];
        }
        return TRUE;
    }
    return FALSE;
}

static ULONG ORTG_BestMode(REG(d0, ULONG monitor), REG(d1, ULONG width), REG(d2, ULONG height), REG(d3, ULONG depth), REG(a6, struct OpenRTGBase *base))
{
    struct ortg_mode_table *t = table_of(base, monitor);
    return t ? ortg_best_mode(t, (int)width, (int)height, (int)depth) : 0;
}

/* Standard (all = 0) or All (all = 1); the number of modes now listed. */
static LONG ORTG_SetModeList(REG(d0, ULONG monitor), REG(d1, ULONG all), REG(a6, struct OpenRTGBase *base))
{
    struct ortg_mode_table *t = table_of(base, monitor);
    if (!t) return -1;
    Forbid();
    LONG n = ortg_build_modes(t, (int)monitor, &acrtg_caps, all ? 1 : 0);
    Permit();
    return n;
}

static APTR ORTG_BoardAddress(REG(d0, ULONG monitor), REG(a6, struct OpenRTGBase *base))
{
    return monitor >= 1 && monitor <= ORTG_MAX_MONITORS && base->board[monitor] ? base->board[monitor]->cd_BoardAddr : NULL;
}

/* OpenRTG's answers in the display database: on (1) or off (0). Once on,
 * the library stays in memory, since the patches point into it. */
static LONG ORTG_DisplayDatabase(REG(d0, ULONG on), REG(a6, struct OpenRTGBase *base))
{
    if (!base->gfx || (on && !base->monitors)) return 0;
    if (on) base->patched = 1;
    return ortg_displaydb(base->gfx, base->table, on ? 1 : 0);
}

/* OpenRTG's own screens (screens.c): a screen on an OpenRTG ModeID gets a
 * chunky bitmap in its board's video RAM and the board shows it, with no
 * Picasso96. The display database must be on. Once on, it stays. */
static LONG ORTG_Screens(REG(d0, ULONG on), REG(a6, struct OpenRTGBase *base))
{
    APTR boards[ORTG_MAX_MONITORS + 1];
    if (!on) return 0;
    if (!base->gfx || !base->monitors) return 0;
    for (int n = 0; n <= ORTG_MAX_MONITORS; n++) boards[n] = base->board[n] ? base->board[n]->cd_BoardAddr : NULL;
    base->patched = 1;
    return ortg_screens_on(base->gfx, base->table, boards);
}

/* ---- pixel arrays (0.4): what cybergraphics.library and Picasso96API.library pass on ---- */

static LONG ORTG_WritePixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGPixels *px), REG(a6, struct OpenRTGBase *base))
{
    return ortg_write_pixels(rp, x, y, px);
}

static LONG ORTG_ReadPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, struct OpenRTGPixels *px), REG(a6, struct OpenRTGBase *base))
{
    return ortg_read_pixels(rp, x, y, px);
}

static LONG ORTG_FillPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(d2, LONG w), REG(d3, LONG h), REG(d4, ULONG argb), REG(a6, struct OpenRTGBase *base))
{
    return ortg_fill_pixels(rp, x, y, w, h, argb);
}

static LONG ORTG_InvertPixels(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(d2, LONG w), REG(d3, LONG h), REG(a6, struct OpenRTGBase *base))
{
    return ortg_invert_pixels(rp, x, y, w, h);
}

static BOOL ORTG_BitMapInfo(REG(a0, struct BitMap *bm), REG(a1, struct OpenRTGBitMapInfo *info), REG(a6, struct OpenRTGBase *base))
{
    return ortg_bitmap_info(bm, info);
}
