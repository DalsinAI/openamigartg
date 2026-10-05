/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ScreenTest: phase 2's first screen. Opens an 8-bit screen on OpenRTG
 * monitor 1 (C:OpenRTG SCREENS first), a window on it, and draws what the
 * Workbench draws most: fills, lines, text, a blit, a scroll. Not shipped.
 *   ScreenTest [SECS n]
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/openrtg.h>

struct Library *OpenRTGBase;

int main(void)
{
    LONG args[1] = { 0 }, secs = 20;
    struct RDArgs *rd = ReadArgs((STRPTR)"SECS/K/N", args, NULL);
    struct Screen *s;
    struct Window *w;
    ULONG id;
    static ULONG pal[1 + 3 * 16 + 1];
    static const UBYTE rgb[16][3] = {
        {0xaa,0xaa,0xaa},{0x00,0x00,0x00},{0xff,0xff,0xff},{0x66,0x88,0xbb},{0xe9,0x54,0x20},{0x3f,0x9a,0x3a},{0x36,0x5f,0xa3},{0xf4,0xd3,0x6b},
        {0x12,0x18,0x25},{0xe8,0xea,0xee},{0x8a,0x94,0xa6},{0xcf,0xda,0xee},{0x7c,0xc0,0x6d},{0xd9,0x3f,0x3f},{0x6f,0x9a,0xd8},{0x40,0x40,0x40} };
    if (rd) { if (args[0]) secs = *(LONG *)args[0]; FreeArgs(rd); }
    if (!(OpenRTGBase = OpenLibrary((STRPTR)"openrtg.library", 0))) { PutStr((STRPTR)"ScreenTest: no openrtg.library\n"); return 20; }
    id = ORTG_BestMode(1, 800, 600, 8);
    Printf((STRPTR)"ScreenTest: mode $%08lx\n", id);
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Depth, 8, SA_Title, (ULONG)"OpenRTG: our own screen", SA_ShowTitle, TRUE,
                       SA_Pens, (ULONG)"\xff\xff", SA_FullPalette, TRUE, SA_LikeWorkbench, TRUE, TAG_DONE);
    if (!s) { PutStr((STRPTR)"ScreenTest: the screen didn't open\n"); CloseLibrary(OpenRTGBase); return 10; }
    pal[0] = 16 << 16;
    for (int i = 0; i < 16; i++) for (int k = 0; k < 3; k++) pal[1 + 3 * i + k] = (ULONG)rgb[i][k] * 0x01010101UL;
    LoadRGB32(&s->ViewPort, pal);
    PutStr((STRPTR)"ScreenTest: screen open\n");
    {
        /* a ruler on the screen itself: a line every 100 pixels */
        struct RastPort *srp = &s->RastPort;
        SetAPen(srp, 1);
        for (int x = 100; x < 800; x += 100) { Move(srp, x, 480); Draw(srp, x, 590); }
        SetAPen(srp, 4); RectFill(srp, 100, 560, 199, 590);
        Printf((STRPTR)"screen %ld x %ld, bitmap %ld bytes a row, rastport bitmap %lx, screen bitmap %lx\n", (LONG)s->Width, (LONG)s->Height,
               (LONG)srp->BitMap->BytesPerRow, (ULONG)srp->BitMap, (ULONG)&s->BitMap);
    }
    w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 60, WA_Top, 60, WA_Width, 520, WA_Height, 360,
                       WA_Title, (ULONG)"A window on OpenRTG", WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                       WA_SizeGadget, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE, TAG_DONE);
    if (w) {
        struct RastPort *rp = w->RPort;
        int x0 = w->BorderLeft + 10, y0 = w->BorderTop + 10;
        for (int i = 0; i < 16; i++) { SetAPen(rp, i); RectFill(rp, x0 + i * 28, y0, x0 + i * 28 + 24, y0 + 40); }
        SetAPen(rp, 1);
        Move(rp, x0, y0 + 60); Draw(rp, x0 + 440, y0 + 160);
        Move(rp, x0, y0 + 160); Draw(rp, x0 + 440, y0 + 60);
        SetAPen(rp, 6); DrawEllipse(rp, x0 + 220, y0 + 110, 80, 40);
        SetAPen(rp, 1); SetBPen(rp, 0); SetDrMd(rp, JAM2);
        Move(rp, x0, y0 + 190); Text(rp, (STRPTR)"Hello from OpenRTG: no Picasso96 here.", 38);
        SetAPen(rp, 2); SetBPen(rp, 6);
        Move(rp, x0, y0 + 210); Text(rp, (STRPTR)"JAM2 white on blue", 18);
        SetDrMd(rp, JAM1);
        ClipBlit(rp, x0, y0, rp, x0, y0 + 230, 200, 40, 0xC0);
        ScrollRaster(rp, 0, 8, x0 + 240, y0 + 230, x0 + 440, y0 + 290);
        PutStr((STRPTR)"ScreenTest: drawn\n");
        {
            /* what came out: the pen along the colour row, as runs */
            LONG prev = -2, start = 0, y = y0 + 20;
            Printf((STRPTR)"window at %ld,%ld size %ld x %ld, borders %ld %ld\n", (LONG)w->LeftEdge, (LONG)w->TopEdge, (LONG)w->Width, (LONG)w->Height, (LONG)w->BorderLeft, (LONG)w->BorderTop);
            for (LONG x = 0; x <= w->Width; x++) {
                LONG v = x < w->Width ? ReadPixel(rp, x, y) : -3;
                if (v != prev) { if (prev != -2) Printf((STRPTR)"  %ld-%ld pen %ld\n", start, x - 1, prev); prev = v; start = x; }
            }
        }
    }
    Delay(secs * 50);
    if (w) CloseWindow(w);
    CloseScreen(s);
    PutStr((STRPTR)"ScreenTest: closed\n");
    CloseLibrary(OpenRTGBase);
    return 0;
}
