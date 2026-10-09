/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * DEVS:Monitors/OpenRTG: OpenRTG's monitor driver. LoadMonDrvs runs it with
 * the other monitors, before IPrefs opens Workbench's screen, so a Workbench
 * on an OpenRTG mode needs no line in S:Startup-Sequence. It does what
 * "C:OpenRTG SCREENS" does, quietly: OpenRTG's modes in the display database,
 * screens on them drawn by OpenRTG, and LIBS:OpenRTG/cybergraphics.library
 * opened once. With Picasso96 running, or no ACRTG board, it does nothing.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/openrtg.h>

static const char version[] __attribute__((used)) = "$VER: OpenRTG-Monitor 0.7 (6.10.2026) Dalsin Limited";
struct Library *OpenRTGBase;

int main(void)
{
    int p96, rc = RETURN_OK;
    (void)version;
    Forbid();
    p96 = FindName(&SysBase->LibList, (STRPTR)"rtg.library") != NULL;
    Permit();
    if (p96)
        return RETURN_OK;                   /* Picasso96 has the boards */
    if (!(OpenRTGBase = OpenLibrary(OPENRTG_NAME, OPENRTG_VERSION)))
        return RETURN_WARN;
    if (ORTG_MonitorCount() == 0 || !ORTG_DisplayDatabase(1) || !ORTG_Screens(1))
        rc = RETURN_WARN;
    else
        OpenLibrary((STRPTR)"LIBS:OpenRTG/cybergraphics.library", 41);   /* stays open, as with C:OpenRTG */
    /* openrtg.library stays open too: its screens are in use from now on */
    return rc;
}
