/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * C:OpenRTG 0.1: what openrtg.library found, from a Shell.
 *   OpenRTG                       the RTG monitors and their boards
 *   OpenRTG MODES [MONITOR n]     each monitor's listed modes
 *   OpenRTG ALL|STANDARD [MONITOR n]   switch the list (until the prefs app
 *                                 keeps the choice)
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/openrtg.h>

static const char version[] = "$VER: OpenRTG 0.1 (4.10.2026) Dalsin Limited";
struct Library *OpenRTGBase;

int main(void)
{
    LONG args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs("MODES/S,ALL/S,STANDARD/S,MONITOR/K/N", args, NULL);
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
    if (only && !ORTG_BoardAddress(only)) { Printf("OpenRTG: there is no monitor %ld.\n", (LONG)only); rc = RETURN_WARN; }
    CloseLibrary(OpenRTGBase);
    FreeArgs(rd);
    return rc;
}
