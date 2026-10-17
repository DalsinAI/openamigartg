/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openrtg.library 0.1: phase 2's first part (DESIGN.md, sections 4 and 10).
 * It finds the ACRTG boards, one per RTG monitor (serial number = monitor),
 * and builds each monitor's mode table once (modes.c), so the display
 * database calls can be answered from it. It does not claim the boards yet:
 * Picasso96's acrtg.card still drives them until OpenRTG opens screens of
 * its own. Built bare by library/build.sh.
 *
 * 0.18: no data or BSS in the file (ortg_state.h), so the same file runs
 * from a ROM; `library/build.sh module` builds it as openrtg.module.
 *
 * 0.19: a friend bitmap asked for as displayable (BMF_DISPLAYABLE) goes in
 * its friend's board's video RAM while such friends take at most half of it
 * (screens.c), so OpenGPU's GPU draws in SDL's window and textures.
 */
#include <exec/types.h>
#include "ortg_state.h"
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
#define LIB_REVISION 19

/* The ACRTG board (amigachrome's common/protocol/acrtg.h): Zorro III,
 * AmigaChrome Team (0xDA15; 2011 before 1 October 2026), product 9 (16 MiB) or product 15 (64 MiB, 0.14); the boot ROM page
 * at +0, video RAM from +0x10000, the register page in the last 64 KiB (0xFE0000 bytes of video RAM on the 16 MiB
 * board, 0x3FE0000 on the 64 MiB one); pictures up to 1920x1200 in 8, 16 and 32-bit. */
#define AMIGACHROME_MFR          0xDA15
#define AMIGACHROME_MFR_OLD      2011
#define ACRTG_PRODUCT   9
#define ACRTG_PRODUCT_64 15
#define ACRTG_BOARD     0x01000000UL                    /* the 16 MiB board's window */
#define ACRTG64_BOARD   0x04000000UL                    /* the 64 MiB board's */
#define ACRTG_VRAM_OF(size) ((size) - 0x20000UL)        /* ROM page and register page apart */
#define ACRTG_VRAM      ACRTG_VRAM_OF(ACRTG_BOARD)
#define ACRTG_MAX_W     1920                            /* before acrtg-v5; from v5 the board's LIMIT says */
#define ACRTG_MAX_H     1200
/* acrtg-v5 (0.17, the native screen mode's step 3): read-only registers the host publishes, in the
 * register page (the board's last 64 KiB): the size the host's window asks for, WANT_GEN up by one at
 * each new size, the board's largest picture (width << 16 | height), and ASK up by one each time a page
 * asks the Amiga to follow now. Before v5 they read 0. */
#define ACRTG_PAGE      0x10000UL
#define R_VERSION       0x04
#define R_WANT_W        0xE0
#define R_WANT_H        0xE4
#define R_WANT_GEN      0xE8
#define R_LIMIT         0xEC
#define R_ASK           0xF0
/* acrtg-v6: the board's display, the size of the picture the host shows it on (width << 16 | height,
 * 0 when not known). OpenRTG names the mode of that size as the monitor's preferred one. */
#define R_DISPLAY       0xF4

struct OpenRTGBase {
    struct Library lib;
    BPTR seglist;
    ULONG monitors;                                     /* RTG monitors found */
    struct ConfigDev *board[ORTG_MAX_MONITORS + 1];     /* [n]: monitor n's board */
    ULONG board_size[ORTG_MAX_MONITORS + 1];            /* its window: ACRTG_BOARD or ACRTG64_BOARD */
    struct ortg_mode_table *table[ORTG_MAX_MONITORS + 1];
    struct Library *gfx;
    int patched;
    struct ortg_caps caps[ORTG_MAX_MONITORS + 1];       /* what each board shows (0.17: its own, kept for ORTG_SetModeList) */
    ULONG want_gen[ORTG_MAX_MONITORS + 1];              /* WANT_GEN when the Native modes were last made */
    struct ortg_state *state;                           /* 0.18: everything else the library writes */
};

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "openrtg.library";
static const char lib_id[] = "openrtg.library 0.19.0 (17.10.2026) OpenRTG, AmigaChrome Team\r\n";

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
static ULONG ORTG_DrawStats(REG(a0, ULONG *counts), REG(d0, ULONG kinds), REG(d1, ULONG flags), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_WritePixelsAlpha(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGPixels *px), REG(d2, ULONG alpha), REG(a6, struct OpenRTGBase *base));
static LONG ORTG_PresentVirgl(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGVirglFrame *f), REG(a6, struct OpenRTGBase *base));
static BOOL ORTG_NativeSize(REG(d0, ULONG monitor), REG(a0, struct OpenRTGNativeSize *out), REG(a6, struct OpenRTGBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)ORTG_MonitorCount, (APTR)ORTG_NextMode, (APTR)ORTG_GetMode,
    (APTR)ORTG_BestMode, (APTR)ORTG_SetModeList, (APTR)ORTG_BoardAddress, (APTR)ORTG_DisplayDatabase, (APTR)ORTG_Screens,
    (APTR)ORTG_WritePixels, (APTR)ORTG_ReadPixels, (APTR)ORTG_FillPixels, (APTR)ORTG_InvertPixels, (APTR)ORTG_BitMapInfo,
    (APTR)ORTG_DrawStats, (APTR)ORTG_WritePixelsAlpha, (APTR)ORTG_PresentVirgl, (APTR)ORTG_NativeSize, (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct OpenRTGBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static ULONG board_reg(struct OpenRTGBase *base, ULONG n, ULONG off)
{
    return *(volatile ULONG *)((UBYTE *)base->board[n]->cd_BoardAddr + base->board_size[n] - ACRTG_PAGE + off);
}

/* What monitor n's board shows: its video RAM; its largest picture, from LIMIT on acrtg-v5 (0.17) and
 * 1920 x 1200 before; three formats; and on acrtg-v5 the size the host asks for (WANT_W, WANT_H, 0 when
 * none is), which the Native modes have. */
static void board_caps(struct OpenRTGBase *base, ULONG n)
{
    struct ortg_caps *c = &base->caps[n];
    ULONG limit, gen;
    c->vram_bytes = ACRTG_VRAM_OF(base->board_size[n]);
    c->max_width = ACRTG_MAX_W; c->max_height = ACRTG_MAX_H;
    c->formats = 7;
    c->native = 0; c->native_w = c->native_h = 0;
    c->display_w = c->display_h = 0;
    if (board_reg(base, n, R_VERSION) < 5) return;
    c->native = 1;
    if (board_reg(base, n, R_VERSION) >= 6) {
        ULONG d = board_reg(base, n, R_DISPLAY);
        c->display_w = (UWORD)(d >> 16); c->display_h = (UWORD)d;
    }
    limit = board_reg(base, n, R_LIMIT);
    if ((limit >> 16) >= 320 && (limit & 0xFFFF) >= 200) { c->max_width = (UWORD)(limit >> 16); c->max_height = (UWORD)limit; }
    /* the size and its generation together: read again if the host changed it in between */
    do {
        gen = board_reg(base, n, R_WANT_GEN);
        c->native_w = (UWORD)board_reg(base, n, R_WANT_W);
        c->native_h = (UWORD)board_reg(base, n, R_WANT_H);
    } while (gen != board_reg(base, n, R_WANT_GEN));
    base->want_gen[n] = gen;
}

static void find_boards(struct OpenRTGBase *base) {
    struct Library *ExpansionBase = OpenLibrary("expansion.library", 37);
    static const ULONG makers[2] = { AMIGACHROME_MFR, AMIGACHROME_MFR_OLD };
    if (!ExpansionBase) return;
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 2; p++) {
            struct ConfigDev *cd = NULL;
            ULONG product = p ? ACRTG_PRODUCT_64 : ACRTG_PRODUCT;
            while ((cd = FindConfigDev(cd, makers[k], product)) != NULL) {
                ULONG n = cd->cd_Rom.er_SerialNumber;          /* serial = monitor */
                if (n < 1 || n > ORTG_MAX_MONITORS || base->board[n] || !cd->cd_BoardAddr) continue;
                struct ortg_mode_table *t = AllocVec(sizeof *t, MEMF_ANY | MEMF_CLEAR);
                if (!t) continue;
                base->board[n] = cd; base->board_size[n] = p ? ACRTG64_BOARD : ACRTG_BOARD;
                board_caps(base, n);
                ortg_build_modes(t, (int)n, &base->caps[n], 0);   /* Standard, the default */
                base->table[n] = t; base->monitors++;
            }
        }
    CloseLibrary(ExpansionBase);
}

/* The Native modes made again when the host has asked for a new size since (WANT_GEN moved): the table
 * at once, under Forbid, and the display database's records with it. One register read when nothing
 * changed. 1 when the size changed. Safe from any task; never waits. */
static int native_refresh(struct OpenRTGBase *base, ULONG n)
{
    struct ortg_mode_table *t = n >= 1 && n <= ORTG_MAX_MONITORS ? base->table[n] : NULL;
    struct ortg_caps *c;
    ULONG gen, w, h;
    if (!t || !base->caps[n].native) return 0;
    if ((gen = board_reg(base, n, R_WANT_GEN)) == base->want_gen[n]) return 0;
    do {
        gen = board_reg(base, n, R_WANT_GEN);
        w = board_reg(base, n, R_WANT_W);
        h = board_reg(base, n, R_WANT_H);
    } while (gen != board_reg(base, n, R_WANT_GEN));
    Forbid();
    c = &base->caps[n];
    c->native_w = (UWORD)w; c->native_h = (UWORD)h;
    ortg_build_modes(t, (int)n, c, t->all);
    base->want_gen[n] = gen;
    ortg_displaydb_native(t);
    Permit();
    return 1;
}

void ortg_native_check(int monitor)
{
    struct OpenRTGBase *base = ortg_S->base;
    if (!base) return;
    if (monitor) { native_refresh(base, (ULONG)monitor); return; }
    for (ULONG n = 1; n <= ORTG_MAX_MONITORS; n++) native_refresh(base, n);
}

static void free_tables(struct OpenRTGBase *base) {
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
        if (base->table[n]) { FreeVec(base->table[n]); base->table[n] = NULL; }
}

/* ---- the library ---------------------------------------------------------------- */

/* Room for entry stubs: the 17 library calls, the display database's 5 patches and the screens' 36
 * (OpenGfx absent) take 58 register stubs; OpenGfx 1.4's 19 provider calls take 19 C stubs at each
 * hand-over (screens.c tries it at switch-on and once more from late_opengpu). */
#define STUBS_REG 80
#define STUBS_C2 48

/* The state block (ortg_state.h): the library's own fields, each file's part, then the stubs. A4 is the
 * caller's on the way in and is put back on the way out; on the way it is the new block. */
static struct Library *lib_init(REG(d0, struct OpenRTGBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    struct ortg_state *caller = ortg_S, boot;
    ULONG db = (ortg_displaydb_size() + 3) & ~3UL, scr = (ortg_screens_size() + 3) & ~3UL;
    ULONG pix = (ortg_pixels_size() + 3) & ~3UL;
    ULONG size = sizeof(struct ortg_state) + db + scr + pix + STUBS_REG * ORTG_STUB_REG_BYTES + STUBS_C2 * ORTG_STUB_C2_BYTES;
    struct ortg_state *s;
    UBYTE *at;
    boot.sys = sys;
    ortg_S = &boot;                                     /* for AllocVec and FreeMem */
    if (!(s = AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR))) {
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        ortg_S = caller;
        return NULL;
    }
    at = (UBYTE *)(s + 1);
    s->sys = sys;
    s->base = base;
    s->db = at; at += db;
    s->scr = at; at += scr;
    s->pix = at; at += pix;
    s->stubs.next = at;
    s->stubs.end = (UBYTE *)s + size;
    ortg_S = s;
    base->state = s;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    /* exec calls the library's own calls with its A4: each goes through a stub from now on */
    ortg_stub_vectors(&s->stubs, s, &base->lib, 5);
    CacheClearU();
    find_boards(base);
    base->gfx = OpenLibrary("graphics.library", 39);
    ortg_S = caller;
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
/* Called by exec, or from lib_close, with the caller's A4: the block's is set here and the caller's put back. */
static BPTR lib_expunge(REG(a6, struct OpenRTGBase *base))
{
    struct ortg_state *caller = ortg_S;
    if (base->lib.lib_OpenCnt || base->patched) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }   /* patched: stays */
    BPTR seglist = base->seglist;
    ortg_S = base->state;
    Remove(&base->lib.lib_Node);
    free_tables(base);
    if (base->gfx) CloseLibrary(base->gfx);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    FreeVec(ortg_S);
    ortg_S = caller;
    return seglist;
}
static ULONG lib_null(void) { return 0; }

/* ---- OpenRTG's calls ------------------------------------------------------------ */

static struct ortg_mode_table *table_of(struct OpenRTGBase *base, ULONG monitor) {
    return monitor >= 1 && monitor <= ORTG_MAX_MONITORS ? base->table[monitor] : NULL;
}

static ULONG ORTG_MonitorCount(REG(a6, struct OpenRTGBase *base))
{
    ortg_late_opengpu();                                /* 0.18: switched on from a ROM, opengpu.library when it can */
    return base->monitors;
}

/* The listed mode after `previous` (0: the first); 0 after the last. */
static ULONG ORTG_NextMode(REG(d0, ULONG monitor), REG(d1, ULONG previous), REG(a6, struct OpenRTGBase *base))
{
    struct ortg_mode_table *t = table_of(base, monitor);
    if (!t) return 0;
    if (!previous) { native_refresh(base, monitor); ortg_late_opengpu(); }   /* a new walk: the Native modes at the size asked for now */
    if (!t->count) return 0;
    if (!previous) return t->modes[0].mode_id;
    for (int i = 0; i + 1 < t->count; i++)
        if (t->modes[i].mode_id == previous) return t->modes[i + 1].mode_id;
    return 0;
}

static BOOL ORTG_GetMode(REG(d0, ULONG mode_id), REG(a0, struct OpenRTGMode *out), REG(a6, struct OpenRTGBase *base))
{
    if (ORTG_IS_NATIVE(mode_id)) native_refresh(base, (mode_id >> 24) & 0x0F);
    for (ULONG n = 1; n <= ORTG_MAX_MONITORS; n++) {
        const struct ortg_mode *m = base->table[n] ? ortg_find_mode(base->table[n], mode_id) : NULL;
        if (!m) continue;
        if (out) {
            out->mode_id = m->mode_id; out->width = m->width; out->height = m->height;
            out->depth = m->depth; out->format = m->format; out->standard = m->standard;
            for (int i = 0; i < ORTG_NAME_LEN; i++) out->name[i] = m->name[i];
            out->native = m->native;            /* 0.17: in the struct's last byte, which was padding */
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

/* Standard (all = 0) or All (all = 1); the number of modes now listed. With the monitor's own board's
 * caps (0.17: before, a 64 MiB board's table was made again with the 16 MiB board's), the Native size
 * as the host asks for it now. */
static LONG ORTG_SetModeList(REG(d0, ULONG monitor), REG(d1, ULONG all), REG(a6, struct OpenRTGBase *base))
{
    struct ortg_mode_table *t = table_of(base, monitor);
    if (!t) return -1;
    native_refresh(base, monitor);
    Forbid();
    LONG n = ortg_build_modes(t, (int)monitor, &base->caps[monitor], all ? 1 : 0);
    ortg_displaydb_native(t);
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
 * Picasso96. The display database must be on. Once on, it stays. A second
 * ORTG_Screens(1) only looks for opengpu.library again, when it wasn't there
 * at the first (screens.c, late_opengpu). ORTG_Screens(0) switches nothing
 * off: from 0.18 it answers 1 when the screens are on, 0 when not. */
static LONG ORTG_Screens(REG(d0, ULONG on), REG(a6, struct OpenRTGBase *base))
{
    APTR boards[ORTG_MAX_MONITORS + 1];
    if (!on) return ortg_screens_are_on();             /* 0.18: 0 asks whether they are on */
    if (!base->gfx || !base->monitors) return 0;
    for (int n = 0; n <= ORTG_MAX_MONITORS; n++) boards[n] = base->board[n] ? base->board[n]->cd_BoardAddr : NULL;
    base->patched = 1;
    return ortg_screens_on(base->gfx, base->table, boards, base->board_size);
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

static ULONG ORTG_DrawStats(REG(a0, ULONG *counts), REG(d0, ULONG kinds), REG(d1, ULONG flags), REG(a6, struct OpenRTGBase *base))
{
    return ortg_draw_stats(counts, kinds, flags);
}

static LONG ORTG_WritePixelsAlpha(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGPixels *px), REG(d2, ULONG alpha), REG(a6, struct OpenRTGBase *base))
{
    return ortg_write_pixels_alpha(rp, x, y, px, alpha);
}

static LONG ORTG_PresentVirgl(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y), REG(a0, const struct OpenRTGVirglFrame *f), REG(a6, struct OpenRTGBase *base))
{
    return ortg_present_virgl(rp, x, y, f);
}

/* ---- the Native size (0.17, the native screen mode's step 3) ---------------------------------------- */

/* What the host asks of monitor n's board: the size its window wants (0 x 0 when none), WANT_GEN, ASK
 * and the board's largest picture, so OpenBench never reads the board itself. TRUE when the board can
 * be asked (ACRTG v5 on); FALSE, with everything 0, when it can't or there is no such monitor. The
 * Native modes are made again first, when the size has changed. */
static BOOL ORTG_NativeSize(REG(d0, ULONG monitor), REG(a0, struct OpenRTGNativeSize *out), REG(a6, struct OpenRTGBase *base))
{
    ULONG gen;
    struct ortg_mode_table *t = table_of(base, monitor);
    if (out) {
        out->width = out->height = 0; out->gen = out->ask = 0;
        out->limit_width = out->limit_height = 0; out->mode_id = 0;
    }
    ortg_late_opengpu();
    if (!t || !base->board[monitor] || !base->caps[monitor].native) return FALSE;
    native_refresh(base, monitor);
    if (!out) return TRUE;
    do {
        gen = board_reg(base, monitor, R_WANT_GEN);
        out->width = (UWORD)board_reg(base, monitor, R_WANT_W);
        out->height = (UWORD)board_reg(base, monitor, R_WANT_H);
    } while (gen != board_reg(base, monitor, R_WANT_GEN));
    out->gen = gen;
    out->ask = board_reg(base, monitor, R_ASK);
    out->limit_width = base->caps[monitor].max_width;
    out->limit_height = base->caps[monitor].max_height;
    out->mode_id = ortg_native_id((int)monitor, ORTG_ARGB32);
    return TRUE;
}
