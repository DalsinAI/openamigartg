/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openrtg.monitor: the monitor driver's job (tools/openrtg_monitor.c,
 * DEVS:Monitors/OpenRTG) done by a ROM module, so a ROM that holds OpenRTG
 * (`library/build.sh module`) gives RTG with nothing of OpenRTG's on the
 * disk.
 *
 * Its RomTag is RTF_COLDSTART at priority -38. exec's InitCode runs the
 * COLDSTART modules by priority, highest first (the priorities below are
 * those in amigachrome-guest's 1M trial ROM map), so it runs:
 *   - after romboot (105), which configures the boards: the ACRTG board is
 *     in expansion's list;
 *   - after graphics (65) and intuition (10), the last of what it uses, and
 *     syscheck (-35);
 *   - before amigachrome-guest's openrtg.boot (-40), which loads its own
 *     copy of openrtg.library into RAM unless one is in memory: with this
 *     module in the same ROM it finds this one's and steps aside;
 *   - before bootmenu (-50), the first module that may open a screen, strap
 *     (-60) and dos, whose boot opens the Workbench screen with its first
 *     window.
 *
 * Its init:
 *   - steps aside, keeping nothing, when Picasso96 (rtg.library) is in
 *     memory or no ACRTG board is fitted;
 *   - makes openrtg.library from its RomTag (before dos, exec's OpenLibrary
 *     finds only what is in memory, and ramlib, which would look for a
 *     resident, is not running yet), unless it is in memory already;
 *   - puts OpenRTG's modes in the display database (ORTG_DisplayDatabase),
 *     switches its screens on (ORTG_Screens) and makes cybergraphics.library
 *     the same way, which stays open, as with the monitor driver.
 *
 * opengpu.library is on the disk, so OpenRTG draws with the CPU until a
 * process asks for its screens again (screens.c, late_opengpu): IPrefs'
 * Workbench screen, or DEVS:Monitors/OpenRTG, which sees OpenRTG on
 * (ORTG_Screens(0) answers 1) and registers nothing a second time.
 *
 * No data or BSS: it runs from the ROM.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include "../include/openrtg/openrtg.h"

#define REG(r, decl) register decl __asm(#r)

#define AMIGACHROME_MFR          0xDA15                  /* openrtg_lib.c's ACRTG board: AmigaChrome Team, or 2011 before 1 October 2026 */
#define AMIGACHROME_MFR_OLD      2011
#define ACRTG_PRODUCT   9                       /* 16 MiB */
#define ACRTG_PRODUCT_64 15                     /* 64 MiB */
#define ACRTG_PRODUCT_256 16                    /* 256 MiB */

#define ORM_PRI (-38)

/* Run as a program, it does nothing. This must stay the first code in the
 * file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char orm_name[] = "openrtg.monitor";
static const char orm_id[] = "openrtg.monitor 0.18 (15.10.2026) OpenRTG, AmigaChrome Team\r\n";

static APTR orm_init(REG(d0, ULONG zero), REG(a0, BPTR seglist), REG(a6, struct ExecBase *SysBase));

const struct Resident orm_romtag = {
    RTC_MATCHWORD, (struct Resident *)&orm_romtag, (APTR)(&orm_romtag + 1),
    RTF_COLDSTART, 0, NT_UNKNOWN, ORM_PRI, (char *)orm_name, (char *)orm_id, (APTR)orm_init,
};

/* openrtg.library's calls (openrtg_lib.sfd), with its registers */
static ULONG ortg_count(struct Library *b)
{
    register struct Library *a6 __asm("a6") = b;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -30(a6)" : "=r"(d0), "+r"(a6) : : "d1", "a0", "a1", "cc", "memory");
    return d0;
}
static LONG ortg_on(struct Library *b, LONG lvo)
{
    register struct Library *a6 __asm("a6") = b;
    register ULONG d0 __asm("d0") = 1;
    if (lvo == -66)
        __asm volatile ("jsr -66(a6)" : "+r"(d0), "+r"(a6) : : "d1", "a0", "a1", "cc", "memory");
    else
        __asm volatile ("jsr -72(a6)" : "+r"(d0), "+r"(a6) : : "d1", "a0", "a1", "cc", "memory");
    return (LONG)d0;
}

/* The library of that name, made from its RomTag first when it isn't in memory. */
static struct Library *rom_library(struct ExecBase *SysBase, const char *name)
{
    struct Resident *r;
    struct Node *n;
    Forbid();
    n = FindName(&SysBase->LibList, (CONST_STRPTR)name);
    Permit();
    if (!n && (r = FindResident((CONST_STRPTR)name)) != NULL && r->rt_Type == NT_LIBRARY)
        InitResident(r, 0);
    return OpenLibrary((CONST_STRPTR)name, 0);
}

static int acrtg_fitted(struct ExecBase *SysBase)
{
    struct Library *ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 37);
    int yes;
    if (!ExpansionBase) return 0;
    yes = FindConfigDev(NULL, AMIGACHROME_MFR, ACRTG_PRODUCT) || FindConfigDev(NULL, AMIGACHROME_MFR, ACRTG_PRODUCT_64) ||
          FindConfigDev(NULL, AMIGACHROME_MFR, ACRTG_PRODUCT_256) ||
          FindConfigDev(NULL, AMIGACHROME_MFR_OLD, ACRTG_PRODUCT) || FindConfigDev(NULL, AMIGACHROME_MFR_OLD, ACRTG_PRODUCT_64) ||
          FindConfigDev(NULL, AMIGACHROME_MFR_OLD, ACRTG_PRODUCT_256);
    CloseLibrary(ExpansionBase);
    return yes;
}

static APTR orm_init(REG(d0, ULONG zero), REG(a0, BPTR seglist), REG(a6, struct ExecBase *SysBase))
{
    struct Library *ortg;
    int p96;
    (void)zero; (void)seglist;
    Forbid();
    p96 = FindName(&SysBase->LibList, (CONST_STRPTR)"rtg.library") != NULL;
    Permit();
    if (p96 || !acrtg_fitted(SysBase)) return NULL;
    if (!(ortg = rom_library(SysBase, OPENRTG_NAME))) return NULL;
    if ((ortg->lib_Version == 0 && ortg->lib_Revision < 18) || ortg_count(ortg) == 0) {   /* ORTG_Screens(0) from 0.18 */
        CloseLibrary(ortg);
        return NULL;
    }
    if (!ortg_on(ortg, -66) || !ortg_on(ortg, -72)) return NULL;     /* stays open: its patches are in */
    rom_library(SysBase, "cybergraphics.library");                                            /* stays open, as with C:OpenRTG */
    return NULL;
}
