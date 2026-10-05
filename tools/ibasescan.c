/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * IBaseScan: IntuitionBase's private part, in hex, with the words that look
 * like mouse limits marked, to compare Picasso96's screens with OpenRTG's.
 * Not shipped.   IBaseScan [WORDS w1 w2 ...] */
#include <exec/types.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

extern struct IntuitionBase *IntuitionBase;

int main(void)
{
    UBYTE *b = (UBYTE *)IntuitionBase;
    ULONG n = IntuitionBase->LibNode.lib_PosSize, i;
    Printf((STRPTR)"IntuitionBase %08lx, %ld bytes; mouse %ld,%ld\n", (LONG)b, (LONG)n, (LONG)IntuitionBase->MouseX, (LONG)IntuitionBase->MouseY);
    for (i = 0; i + 1 < n; i += 2) {
        UWORD w = *(UWORD *)(b + i);
        if (w == 0x383F || w == 0x2A2F || w == 799 || w == 599 || w == 800 || w == 600 || w == 0x3840 || w == 0x2A30 || w == 1599 || w == 1199)
            Printf((STRPTR)"  +%04lx: %04lx (%ld)\n", (LONG)i, (LONG)w, (LONG)w);
    }
    for (i = 0; i < n; i++) { if (i % 16 == 0) Printf((STRPTR)"\n%04lx:", (LONG)i); Printf((STRPTR)" %02lx", (LONG)b[i]); }
    PutStr((STRPTR)"\n");
    return 0;
}
