/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * C:OpenRTG 0.2: what openrtg.library found, from a Shell.
 *   OpenRTG                       the RTG monitors and their boards
 *   OpenRTG MODES [MONITOR n]     each monitor's listed modes
 *   OpenRTG ALL|STANDARD [MONITOR n]   switch the list (until the prefs app
 *                                 keeps the choice)
 *   OpenRTG ACTIVATE [FORCE] | OFF  OpenRTG's modes in the display database
 *                                 (FORCE: even beside Picasso96, for tests)
 *   OpenRTG LISTDB                  every mode the display database lists
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/openrtg.h>

static const char version[] = "$VER: OpenRTG 0.2 (4.10.2026) Dalsin Limited";
struct Library *OpenRTGBase;

int main(void)
{
    LONG args[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs("MODES/S,ALL/S,STANDARD/S,MONITOR/K/N,ACTIVATE/S,OFF/S,FORCE/S,LISTDB/S,SCREENS/S", args, NULL);
    int rc = RETURN_OK;
    (void)version;
    if (!rd) { PrintFault(IoErr(), "OpenRTG"); return RETURN_FAIL; }
    OpenRTGBase = OpenLibrary(OPENRTG_NAME, OPENRTG_VERSION);
    if (!OpenRTGBase) {
        Printf("OpenRTG: openrtg.library isn't in LIBS: (copy it there, or run the OpenRTG Installer).\n");
        FreeArgs(rd);
        return RETURN_FAIL;
    }
    ULONG count = ORTG_MonitorCount();
    ULONG only = args[3] ? *(ULONG *)args[3] : 0;
    Printf("openrtg.library %ld.%ld: %ld RTG monitor%s\n", (LONG)OpenRTGBase->lib_Version, (LONG)OpenRTGBase->lib_Revision,
           (LONG)count, count == 1 ? "" : "s");
    if (count == 0)
        Printf("No ACRTG board was found. In AmigaChrome, fit one in Cradle (Hardware, Display).\n");
    for (ULONG n = 1; n <= 4; n++) {
        APTR board = ORTG_BoardAddress(n);
        if (!board || (only && n != only)) continue;
        if (args[1] || args[2]) {
            LONG listed = ORTG_SetModeList(n, args[1] ? 1 : 0);
            Printf("Monitor %ld: now listing %s (%ld modes)\n", (LONG)n, args[1] ? (LONG)"All" : (LONG)"Standard", listed);
        }
        ULONG id = 0, modes = 0;
        while ((id = ORTG_NextMode(n, id)) != 0) modes++;
        Printf("Monitor %ld: ACRTG at $%08lx, %ld modes listed\n", (LONG)n, (ULONG)board, (LONG)modes);
        if (!args[0]) continue;
        struct OpenRTGMode m;
        id = 0;
        while ((id = ORTG_NextMode(n, id)) != 0)
            if (ORTG_GetMode(id, &m))
                Printf("  $%08lx  %s%s\n", m.mode_id, (LONG)m.name, m.standard ? (LONG)"" : (LONG)"  (All)");
    }
    if (args[4] || args[5]) {
        int p96;
        Forbid();
        p96 = FindName(&SysBase->LibList, (STRPTR)"rtg.library") != NULL;
        Permit();
        if (args[4] && p96 && !args[6]) {
            Printf("OpenRTG: Picasso96 is running. OpenRTG and Picasso96 don't run together; turn Picasso96's monitors off first.\n");
            rc = RETURN_WARN;
        } else if (!ORTG_DisplayDatabase(args[4] ? 1 : 0)) {
            Printf("OpenRTG: the display database could not be changed (no monitors?).\n");
            rc = RETURN_WARN;
        } else Printf("OpenRTG: the display database %s OpenRTG's modes.\n", args[4] ? (LONG)"now lists" : (LONG)"no longer lists");
    }
    if (args[8]) {
        /* OpenRTG's own screens: its modes in the display database, and
         * screens on them drawn by OpenRTG, with no Picasso96 */
        int p96;
        Forbid();
        p96 = FindName(&SysBase->LibList, (STRPTR)"rtg.library") != NULL;
        Permit();
        if (p96 && !args[6]) {
            Printf("OpenRTG: Picasso96 is running. OpenRTG's screens need Picasso96's monitors off.\n");
            rc = RETURN_WARN;
        } else if (!ORTG_DisplayDatabase(1) || !ORTG_Screens(1)) {
            Printf("OpenRTG: screens could not be switched on (no monitors?).\n");
            rc = RETURN_WARN;
        } else Printf("OpenRTG: screens on OpenRTG's modes are now OpenRTG's own.\n");
    }
    if (args[7]) {
        struct Library *GfxBase = OpenLibrary((STRPTR)"graphics.library", 39);
        ULONG id = INVALID_ID, count = 0;
        if (GfxBase) {
            while ((id = NextDisplayInfo(id)) != INVALID_ID) {
                struct NameInfo ni;
                struct DimensionInfo di;
                DisplayInfoHandle h = FindDisplayInfo(id);
                ni.Name[0] = 0;
                if (!h || !GetDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, 0)) ni.Name[0] = 0;
                if (!h || !GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DIMS, 0)) di.MaxDepth = 0, di.Nominal.MaxX = di.Nominal.MaxY = -1;
                Printf("  $%08lx  %-30s %ldx%ld, depth %ld%s\n", id, ni.Name[0] ? (LONG)ni.Name : (LONG)"(no name)",
                       (LONG)(di.Nominal.MaxX + 1), (LONG)(di.Nominal.MaxY + 1), (LONG)di.MaxDepth,
                       ModeNotAvailable(id) ? (LONG)"  (not available)" : (LONG)"");
                count++;
            }
            Printf("The display database lists %ld modes.\n", (LONG)count);
            CloseLibrary(GfxBase);
        }
    }
    if (only && !ORTG_BoardAddress(only)) { Printf("OpenRTG: there is no monitor %ld.\n", (LONG)only); rc = RETURN_WARN; }
    CloseLibrary(OpenRTGBase);
    FreeArgs(rd);
    return rc;
}
