/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGExact: does OpenRTG draw the same pixels through OpenGPU as with
 * its CPU code? Each test draws the same calls twice, once with
 * ORTG_DRAW_CPU_ONLY and once through OpenGPU, into two windows of the same
 * size on the Workbench screen, each partly covered by a small window so
 * the drawing is clipped in pieces, and compares the windows' pixels. Then
 * the same into two bitmaps of the screen's format, with no layers.
 * openrtg.library 0.11 or later, on an OpenRTG Workbench screen.
 *   OpenRTGExact            Returns 0 when every test matches, 10 when one doesn't. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <proto/openrtg.h>
#include <stdio.h>
#include <string.h>

struct Library *CyberGfxBase, *OpenRTGBase;
static UBYTE tmpl[16 * 64], pens[64 * 64];
static ULONG argb[64 * 64], argba[64 * 64];
static UWORD pat[4] = { 0xF0F0, 0x3C3C, 0x0F0F, 0xC3C3 };
static int fails, tests;

static ULONG wpaa(APTR src, UWORD sx, UWORD sy, UWORD smod, struct RastPort *r, UWORD dx, UWORD dy, UWORD w, UWORD h, ULONG alpha)
{
    register APTR a0 __asm("a0") = src;
    register struct RastPort *a1 __asm("a1") = r;
    register ULONG d0 __asm("d0") = sx, d1 __asm("d1") = sy, d2 __asm("d2") = smod, d3 __asm("d3") = dx;
    register ULONG d4 __asm("d4") = dy, d5 __asm("d5") = w, d6 __asm("d6") = h, d7 __asm("d7") = alpha;
    register struct Library *a6 __asm("a6") = CyberGfxBase;
    __asm volatile ("jsr -216(a6)" : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "r"(d2), "r"(d3), "r"(d4), "r"(d5), "r"(d6), "r"(d7), "r"(a6) : "cc", "memory");
    return d0;
}

/* Everything the tests draw, at (x, y) in rp. */
static void draw(int test, struct RastPort *rp, int x, int y)
{
    int k;
    SetAPen(rp, 1); SetBPen(rp, 0); SetDrMd(rp, JAM2); SetAfPt(rp, NULL, 0); rp->Mask = 0xFF; SetDrPt(rp, 0xFFFF);
    RectFill(rp, x, y, x + 199, y + 149);
    switch (test) {
    case 0: SetAPen(rp, 3); RectFill(rp, x + 10, y + 10, x + 150, y + 90); break;
    case 1: SetDrMd(rp, COMPLEMENT); RectFill(rp, x + 5, y + 5, x + 180, y + 120); break;
    case 2: SetAPen(rp, 2); SetBPen(rp, 3); SetAfPt(rp, pat, 2); RectFill(rp, x + 3, y + 7, x + 190, y + 140); break;
    case 3: SetDrMd(rp, JAM1); SetAfPt(rp, pat, 2); SetAPen(rp, 2); RectFill(rp, x + 1, y + 1, x + 101, y + 77); break;
    case 4: SetAPen(rp, 2); SetDrMd(rp, JAM1); Move(rp, x + 3, y + 20); Text(rp, (STRPTR)"Hello, OpenGPU 0123", 19);
            SetDrMd(rp, JAM2); SetBPen(rp, 3); Move(rp, x + 3, y + 40); Text(rp, (STRPTR)"JAM2 text, Workbench", 20);
            SetDrMd(rp, JAM2 | INVERSVID); Move(rp, x + 3, y + 60); Text(rp, (STRPTR)"inverse", 7);
            SetDrMd(rp, COMPLEMENT); Move(rp, x + 3, y + 80); Text(rp, (STRPTR)"complement", 10); break;
    case 5: SetAPen(rp, 2); BltTemplate((PLANEPTR)tmpl, 3, 16, rp, x + 10, y + 10, 100, 64);
            SetDrMd(rp, JAM1); SetAPen(rp, 3); BltTemplate((PLANEPTR)tmpl, 0, 16, rp, x + 60, y + 50, 128, 64); break;
    case 6: SetAPen(rp, 3); RectFill(rp, x + 20, y + 20, x + 120, y + 100);
            ClipBlit(rp, x + 10, y + 10, rp, x + 40, y + 30, 120, 90, 0xC0);
            ClipBlit(rp, x + 40, y + 30, rp, x + 30, y + 20, 120, 90, 0xC0); break;
    case 7: SetAPen(rp, 2); RectFill(rp, x + 10, y + 10, x + 190, y + 50); SetBPen(rp, 3);
            ScrollRaster(rp, 5, 8, x, y, x + 199, y + 149); ScrollRaster(rp, -7, -3, x + 20, y + 20, x + 150, y + 100); break;
    case 8: SetAPen(rp, 3);
            for (k = 0; k < 12; k++) { Move(rp, x + 2, y + 2 + k * 11); Draw(rp, x + 197, y + 2 + k * 11); Move(rp, x + 2 + k * 15, y + 2); Draw(rp, x + 2 + k * 15, y + 147); }
            for (k = 0; k < 8; k++) { Move(rp, x + k * 9, y); Draw(rp, x + k * 9 + 140, y + 140); Move(rp, x + 190 - k * 7, y + 4); Draw(rp, x + 60 - k * 7, y + 134); }
            SetDrMd(rp, COMPLEMENT); Move(rp, x, y + 75); Draw(rp, x + 199, y + 75); Draw(rp, x + 199, y + 10);
            SetDrMd(rp, JAM1); SetDrPt(rp, 0xF0F0); Move(rp, x, y + 100); Draw(rp, x + 199, y + 100); break;
    case 9: WriteChunkyPixels(rp, x + 10, y + 10, x + 73, y + 73, pens, 64);
            if (CyberGfxBase) {
                WritePixelArray(argb, 0, 0, 256, rp, x + 80, y + 5, 64, 64, RECTFMT_ARGB);
                WritePixelArray(pens, 0, 0, 64, rp, x + 120, y + 80, 64, 64, RECTFMT_LUT8);
                FillPixelArray(rp, x + 5, y + 100, 60, 30, 0x00A0B0C0UL);
                InvertPixelArray(rp, x + 30, y + 90, 90, 40);
                wpaa(argba, 0, 0, 256, rp, x + 60, y + 60, 64, 64, 0xC0000000UL);
            }
            break;
    case 10: if (CyberGfxBase) {   /* read back what was drawn, and write it again elsewhere */
                static ULONG back[64 * 64];
                WritePixelArray(argb, 0, 0, 256, rp, x + 10, y + 10, 64, 64, RECTFMT_ARGB);
                ReadPixelArray(back, 0, 0, 256, rp, x + 10, y + 10, 64, 64, RECTFMT_ARGB);
                WritePixelArray(back, 0, 0, 256, rp, x + 100, y + 40, 64, 64, RECTFMT_ARGB);
             }
             break;
    case 11: SetAPen(rp, 3); RectFill(rp, x + 20, y + 20, x + 120, y + 100);
             BltBitMapRastPort(rp->BitMap, 0, 0, rp, x + 10, y + 10, 50, 40, 0x00);     /* a clear */
             BltBitMapRastPort(rp->BitMap, 0, 0, rp, x + 70, y + 50, 50, 40, 0x50);     /* an invert */
             break;
    }
}

static const char *const names[] = { "fill", "complement", "pattern JAM2", "pattern JAM1", "text", "template", "copies",
                                     "scroll", "lines", "pixel arrays", "read back", "minterms" };
#define TESTS 12

/* The pixels of a window's inside, read with the CPU (ORTG_ReadPixels RAW). */
static void grab(struct Window *w, UBYTE *to, int bpp)
{
    struct OpenRTGPixels px;
    memset(to, 0, 200 * 150 * bpp);
    px.data = to; px.x = px.y = 0; px.modulo = 200 * bpp; px.format = ORTG_PIX_RAW; px.ctable = NULL;
    px.width = 200; px.height = 150; px.dest_width = px.dest_height = 0;
    ORTG_ReadPixels(w->RPort, w->BorderLeft, w->BorderTop, &px);
}

static void check(int t, const char *where, const UBYTE *a, const UBYTE *b, ULONG bytes)
{
    ULONG i, diff = 0, first = 0;
    for (i = 0; i < bytes; i++) if (a[i] != b[i]) { if (!diff) first = i; diff++; }
    tests++;
    if (diff) { fails++; printf("%-14s %-8s DIFFERENT: %lu bytes, first at %lu\n", names[t], where, (unsigned long)diff, (unsigned long)first); }
    else printf("%-14s %-8s the same\n", names[t], where);
}

int main(void)
{
    struct Screen *wb;
    struct Window *wa, *wbw, *cover;
    struct OpenRTGBitMapInfo bi;
    UBYTE *ga, *gb;
    int t, bpp, i;
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0)) || (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 11)) {
        printf("OpenRTGExact: needs openrtg.library 0.11\n");
        return 20;
    }
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    wb = LockPubScreen(NULL);
    if (!wb || !ORTG_BitMapInfo(wb->RastPort.BitMap, &bi)) { printf("OpenRTGExact: the Workbench screen isn't OpenRTG's\n"); return 20; }
    bpp = bi.depth / 8;
    for (i = 0; i < (int)sizeof tmpl; i++) tmpl[i] = (UBYTE)(i * 73 ^ (i >> 2));
    for (i = 0; i < 64 * 64; i++) {
        pens[i] = (UBYTE)((i ^ (i >> 6)) & 7);
        argb[i] = 0xFF000000UL | (ULONG)(i * 2654435761UL & 0xFFFFFF);
        argba[i] = ((ULONG)(i * 13) & 255) << 24 | (ULONG)(i * 40503UL & 0xFFFFFF);
    }
    wa = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 10, WA_Top, 20, WA_InnerWidth, 200, WA_InnerHeight, 150,
                        WA_Title, (ULONG)"Exact", WA_Activate, FALSE, WA_SmartRefresh, TRUE, WA_DragBar, TRUE, TAG_DONE);
    wbw = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 260, WA_Top, 20, WA_InnerWidth, 200, WA_InnerHeight, 150,
                         WA_Title, (ULONG)"Exact", WA_Activate, FALSE, WA_SmartRefresh, TRUE, WA_DragBar, TRUE, TAG_DONE);
    /* one small window over each, at the same place and looking the same
     * (no title, neither active), so both draw in pieces and copies that
     * read from under them read the same pixels */
    cover = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 60, WA_Top, 70, WA_Width, 60, WA_Height, 40, WA_Activate, FALSE, WA_DragBar, TRUE, TAG_DONE);
    {
        struct Window *c2 = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 310, WA_Top, 70, WA_Width, 60, WA_Height, 40, WA_Activate, FALSE, WA_DragBar, TRUE, TAG_DONE);
        ga = AllocVec(200 * 150 * 4, MEMF_ANY);
        gb = AllocVec(200 * 150 * 4, MEMF_ANY);
        if (!wa || !wbw || !cover || !c2 || !ga || !gb) { printf("OpenRTGExact: no windows or memory\n"); return 20; }
        for (t = 0; t < TESTS; t++) {
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
            draw(t, wa->RPort, wa->BorderLeft, wa->BorderTop);
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
            draw(t, wbw->RPort, wbw->BorderLeft, wbw->BorderTop);
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
            grab(wa, ga, bpp); grab(wbw, gb, bpp);
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
            check(t, "window", ga, gb, 200 * 150 * bpp);
        }
        CloseWindow(c2);
    }
    /* the same into two bitmaps of the screen's format, no layers */
    {
        struct BitMap *ba = AllocBitMap(200, 150, bi.depth, BMF_CLEAR, wb->RastPort.BitMap);
        struct BitMap *bb = AllocBitMap(200, 150, bi.depth, BMF_CLEAR, wb->RastPort.BitMap);
        struct OpenRTGBitMapInfo ia, ib;
        struct RastPort ra, rb;
        if (ba && bb && ORTG_BitMapInfo(ba, &ia) && ORTG_BitMapInfo(bb, &ib)) {
            InitRastPort(&ra); InitRastPort(&rb); ra.BitMap = ba; rb.BitMap = bb;
            SetFont(&ra, wb->RastPort.Font); SetFont(&rb, wb->RastPort.Font);
            for (t = 0; t < TESTS; t++) {
                ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
                draw(t, &ra, 0, 0);
                ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
                draw(t, &rb, 0, 0);
                for (i = 0; i < 150; i++) {
                    memcpy(ga + i * 200 * bpp, (UBYTE *)ia.memory + i * ia.bytes_per_row, 200 * bpp);
                    memcpy(gb + i * 200 * bpp, (UBYTE *)ib.memory + i * ib.bytes_per_row, 200 * bpp);
                }
                check(t, "bitmap", ga, gb, 200 * 150 * bpp);
            }
        } else printf("OpenRTGExact: no OpenRTG bitmaps of the screen's format; that part is skipped\n");
        if (ba) FreeBitMap(ba);
        if (bb) FreeBitMap(bb);
    }
    CloseWindow(cover); CloseWindow(wa); CloseWindow(wbw);
    UnlockPubScreen(NULL, wb);
    FreeVec(ga); FreeVec(gb);
    printf("OpenRTGExact: %d of %d the same%s\n", tests - fails, tests, fails ? "" : ": OpenGPU draws exactly what the CPU code draws");
    CloseLibrary(OpenRTGBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    return fails ? 10 : 0;
}
