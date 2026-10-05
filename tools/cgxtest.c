/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * CGXTest: a window on Workbench drawn through cybergraphics.library
 * (WritePixelArray in RGB, WriteLUTPixelArray, FillPixelArray,
 * InvertPixelArray, ScalePixelArray), with what it reads back printed.
 * Not shipped.   CGXTest [SECS n] */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <inline/macros.h>

struct Library *CyberGfxBase;
#define GetCyberMapAttr(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, bm, a0, ULONG, a, d0, , CyberGfxBase)
#define WritePixelArray(s, sx, sy, sm, rp, dx, dy, w, h, f) \
    LP10(0x7e, ULONG, WritePixelArray, APTR, s, a0, UWORD, sx, d0, UWORD, sy, d1, UWORD, sm, d2, struct RastPort *, rp, a1, UWORD, dx, d3, UWORD, dy, d4, UWORD, w, d5, UWORD, h, d6, UBYTE, f, d7, , CyberGfxBase)
#define ScalePixelArray(s, sw, sh, sm, rp, dx, dy, dw, dh, f) \
    LP10(0x5a, ULONG, ScalePixelArray, APTR, s, a0, UWORD, sw, d0, UWORD, sh, d1, UWORD, sm, d2, struct RastPort *, rp, a1, UWORD, dx, d3, UWORD, dy, d4, UWORD, dw, d5, UWORD, dh, d6, UBYTE, f, d7, , CyberGfxBase)
#define FillPixelArray(rp, x, y, w, h, p) \
    LP6(0x96, ULONG, FillPixelArray, struct RastPort *, rp, a1, UWORD, x, d0, UWORD, y, d1, UWORD, w, d2, UWORD, h, d3, ULONG, p, d4, , CyberGfxBase)
#define InvertPixelArray(rp, x, y, w, h) \
    LP5(0x90, ULONG, InvertPixelArray, struct RastPort *, rp, a1, UWORD, x, d0, UWORD, y, d1, UWORD, w, d2, UWORD, h, d3, , CyberGfxBase)
#define ReadRGBPixel(rp, x, y) LP3(0x6c, ULONG, ReadRGBPixel, struct RastPort *, rp, a1, UWORD, x, d0, UWORD, y, d1, , CyberGfxBase)

static UBYTE rgb[64 * 64 * 3];

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"SECS/K/N", args, NULL);
    LONG secs = args[0] ? *(LONG *)args[0] : 10;
    struct Window *w;
    struct RastPort *rp;
    if (rd) FreeArgs(rd);
    if (!(CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 41))) { PutStr((STRPTR)"no cybergraphics.library\n"); return 20; }
    w = OpenWindowTags(NULL, WA_Left, 200, WA_Top, 60, WA_Width, 420, WA_Height, 220, WA_Title, (ULONG)"CGXTest",
                       WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, FALSE, WA_SimpleRefresh, TRUE, TAG_DONE);
    if (!w) { CloseLibrary(CyberGfxBase); return 20; }
    rp = w->RPort;
    Printf((STRPTR)"ISCYBERGFX %ld, PIXFMT %ld, XMOD %ld\n", GetCyberMapAttr(rp->BitMap, 0x80000008), GetCyberMapAttr(rp->BitMap, 0x80000004), GetCyberMapAttr(rp->BitMap, 0x80000001));
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            UBYTE *p = rgb + (y * 64 + x) * 3;
            p[0] = (UBYTE)(x * 4); p[1] = (UBYTE)(y * 4); p[2] = (UBYTE)(255 - x * 4);
        }
    Printf((STRPTR)"WritePixelArray: %ld\n", WritePixelArray(rgb, 0, 0, 64 * 3, rp, 20, 30, 64, 64, 0));
    Printf((STRPTR)"ScalePixelArray: %ld\n", ScalePixelArray(rgb, 64, 64, 64 * 3, rp, 100, 30, 128, 128, 0));
    Printf((STRPTR)"FillPixelArray: %ld\n", FillPixelArray(rp, 250, 30, 60, 40, 0x00FF0000));
    Printf((STRPTR)"FillPixelArray: %ld\n", FillPixelArray(rp, 250, 80, 60, 40, 0x00000000));
    Printf((STRPTR)"InvertPixelArray: %ld\n", InvertPixelArray(rp, 320, 30, 60, 90));
    Printf((STRPTR)"ReadRGBPixel(260, 40) %08lx, (21, 31) %08lx\n", ReadRGBPixel(rp, 260, 40), ReadRGBPixel(rp, 21, 31));
    Delay(secs * 50);
    CloseWindow(w);
    CloseLibrary(CyberGfxBase);
    return 0;
}
