/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * BestTest: what BestModeIDA answers for the requests games and SDL make,
 * and whether a 32-bit OpenRTG screen opens with SA_Depth 32. Not shipped. */
#include <exec/types.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

static void ask(const char *what, ULONG w, ULONG h, ULONG d, ULONG must, ULONG mon)
{
    struct TagItem t[6]; int n = 0;
    struct NameInfo ni;
    ULONG id;
    t[n].ti_Tag = BIDTAG_NominalWidth; t[n++].ti_Data = w;
    t[n].ti_Tag = BIDTAG_NominalHeight; t[n++].ti_Data = h;
    t[n].ti_Tag = BIDTAG_Depth; t[n++].ti_Data = d;
    if (must) { t[n].ti_Tag = BIDTAG_DIPFMustHave; t[n++].ti_Data = must; }
    if (mon) { t[n].ti_Tag = BIDTAG_MonitorID; t[n++].ti_Data = mon; }
    t[n].ti_Tag = TAG_DONE;
    id = BestModeIDA(t);
    ni.Name[0] = 0;
    if (id != INVALID_ID) GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id);
    Printf((STRPTR)"%-28s -> $%08lx %s\n", (LONG)what, id, (LONG)(id == INVALID_ID ? "(none)" : (char *)ni.Name));
}

int main(void)
{
    struct Screen *s;
    ask("320x200x8", 320, 200, 8, 0, 0);
    ask("320x240x8", 320, 240, 8, 0, 0);
    ask("640x480x8", 640, 480, 8, 0, 0);
    ask("640x480x16", 640, 480, 16, 0, 0);
    ask("800x600x32", 800, 600, 32, 0, 0);
    ask("320x256x5 (chipset)", 320, 256, 5, 0, 0);
    ask("320x200x8 must RTG", 320, 200, 8, 0x02000000, 0);
    ask("640x480x8 HAM must", 640, 480, 8, DIPF_IS_HAM, 0);
    ask("1024x768x8 monitor PAL", 1024, 768, 8, 0, PAL_MONITOR_ID);
    s = OpenScreenTags(NULL, SA_DisplayID, 0x61011200, SA_Depth, 32, SA_Title, (ULONG)"BestTest 32", SA_ShowTitle, TRUE, TAG_DONE);
    Printf((STRPTR)"OpenScreen $61011200 depth 32: %s\n", (LONG)(s ? "opened" : "refused"));
    if (s) { Delay(50); CloseScreen(s); }
    return 0;
}
