/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * AddInfoTest: how graphics' AddDisplayInfoData makes records for a new
 * ModeID (0x6F011000, a monitor no one owns). Not shipped. */
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <string.h>
#include <inline/macros.h>

/* graphics' private calls (graphics_lib.sfd ==private), as monitor drivers use them */
#define AddDisplayInfoData(h, buf, size, tag, id) \
    LP5NR(0x2e8, AddDisplayInfoData, APTR, h, a0, UBYTE *, buf, a1, ULONG, size, d0, ULONG, tag, d1, ULONG, id, d2, , GfxBase)
#define AddDisplayInfo(rec) \
    LP1NR(0x2e2, AddDisplayInfo, APTR, rec, a0, , GfxBase)
extern struct GfxBase *GfxBase;

#define ID 0x6F011000UL

static void dump(const char *what, UBYTE *b, ULONG n)
{
    Printf((STRPTR)"  %s at %08lx:", (LONG)what, (LONG)b);
    for (ULONG i = 0; i < n; i++) { if (i % 16 == 0) PutStr((STRPTR)"\n   "); Printf((STRPTR)" %02lx", (LONG)b[i]); }
    PutStr((STRPTR)"\n");
}

int main(void)
{
    struct DisplayInfo di;
    struct DimensionInfo dm;
    struct NameInfo ni;
    DisplayInfoHandle h;
    ULONG id = INVALID_ID;
    memset(&di, 0, sizeof di); memset(&dm, 0, sizeof dm); memset(&ni, 0, sizeof ni);
    di.Header.StructID = DTAG_DISP; di.Header.DisplayID = ID; di.Header.SkipID = TAG_SKIP; di.Header.Length = (sizeof di - 16 + 7) / 8;
    di.PropertyFlags = 0x02000000UL | DIPF_IS_WB;
    di.Resolution.x = di.Resolution.y = 18;
    dm.Header.StructID = DTAG_DIMS; dm.Header.DisplayID = ID; dm.Header.SkipID = TAG_SKIP; dm.Header.Length = (sizeof dm - 16 + 7) / 8;
    dm.MaxDepth = 8; dm.Nominal.MaxX = 799; dm.Nominal.MaxY = 599;
    ni.Header.StructID = DTAG_NAME; ni.Header.DisplayID = ID; ni.Header.SkipID = TAG_SKIP; ni.Header.Length = (sizeof ni - 16 + 7) / 8;
    strcpy((char *)ni.Name, "AddInfoTest: 800x600");
    Printf((STRPTR)"before: FindDisplayInfo %08lx\n", (LONG)FindDisplayInfo(ID));
    AddDisplayInfoData(NULL, (UBYTE *)&di, sizeof di, DTAG_DISP, ID);
    h = FindDisplayInfo(ID);
    Printf((STRPTR)"after DISP with NULL handle: FindDisplayInfo %08lx\n", (LONG)h);
    if (!h) {
        ULONG *top = AllocVec(0x38, MEMF_PUBLIC | MEMF_CLEAR);
        ((UWORD *)top)[9] = (UWORD)(ID >> 16);
        AddDisplayInfo(top);
        dump("monitor record", (UBYTE *)top, 0x38);
        {
            ULONG *rec = AllocVec(0x38, MEMF_PUBLIC | MEMF_CLEAR);
            ((UWORD *)rec)[8] = (UWORD)(ID >> 16); ((UWORD *)rec)[9] = (UWORD)ID;
            rec[3] = (ULONG)top;     /* parent */
            Forbid();
            top[2] = (ULONG)rec;     /* child */
            Permit();
            AddDisplayInfoData(rec, (UBYTE *)&di, sizeof di, DTAG_DISP, ID);
            h = FindDisplayInfo(ID);
            Printf((STRPTR)"linked by hand under it (%08lx): FindDisplayInfo %08lx\n", (LONG)rec, (LONG)h);
        }
        dump("monitor record now", (UBYTE *)top, 0x38);
    }
    Printf((STRPTR)"so far: FindDisplayInfo %08lx\n", (LONG)h);
    if (h) {
        AddDisplayInfoData(h, (UBYTE *)&dm, sizeof dm, DTAG_DIMS, ID);
        AddDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, ID);
        dump("record", (UBYTE *)h, 256);
        if (((ULONG *)h)[3]) dump("parent", (UBYTE *)((ULONG *)h)[3], 96);
        Printf((STRPTR)"GetDisplayInfoData NAME: %ld\n", GetDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, 0));
    }
    while ((id = NextDisplayInfo(id)) != INVALID_ID) if ((id >> 24) == 0x6F) Printf((STRPTR)"listed: %08lx\n", id);
    return 0;
}
