/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * DumpMode: the display database's records for the modes whose name has
 * NAME in it, in hex, to compare OpenRTG's with Picasso96's. Not shipped.
 *   DumpMode NAME */
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/monitor.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <string.h>

static void dump(const char *what, UBYTE *b, ULONG n)
{
    Printf((STRPTR)"  %s (%ld bytes):", (LONG)what, (LONG)n);
    for (ULONG i = 0; i < n; i++) { if (i % 16 == 0) PutStr((STRPTR)"\n   "); Printf((STRPTR)" %02lx", (LONG)b[i]); }
    PutStr((STRPTR)"\n");
}

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"NAME/A", args, NULL);
    ULONG id = INVALID_ID;
    if (!rd) return 20;
    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct NameInfo ni;
        static UBYTE buf[256];
        DisplayInfoHandle h = FindDisplayInfo(id);
        ULONG n;
        if (!h || !GetDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, 0) || !strstr((char *)ni.Name, (char *)args[0])) continue;
        Printf((STRPTR)"$%08lx %s\n", id, (LONG)ni.Name);
        dump("handle", (UBYTE *)h, 64);
        dump("before+record+after (from handle-96)", (UBYTE *)h - 96, 448);
        if (((ULONG *)h)[3]) { Printf((STRPTR)"  parent at %08lx\n", ((ULONG *)h)[3]); dump("parent (from -32)", (UBYTE *)((ULONG *)h)[3] - 32, 160); }
        if ((n = GetDisplayInfoData(h, buf, sizeof buf, DTAG_DISP, 0))) dump("DisplayInfo", buf, n);
        if ((n = GetDisplayInfoData(h, buf, sizeof buf, DTAG_DIMS, 0))) dump("DimensionInfo", buf, n);
        if ((n = GetDisplayInfoData(h, buf, sizeof buf, DTAG_MNTR, 0))) {
            struct MonitorInfo *mi = (struct MonitorInfo *)buf;
            dump("MonitorInfo", buf, n);
            if (mi->Mspc) dump("MonitorSpec", (UBYTE *)mi->Mspc, sizeof(struct MonitorSpec));
            if (mi->Mspc && mi->Mspc->ms_Special) dump("SpecialMonitor", (UBYTE *)mi->Mspc->ms_Special, sizeof(struct SpecialMonitor));
        }
    }
    FreeArgs(rd);
    return 0;
}
