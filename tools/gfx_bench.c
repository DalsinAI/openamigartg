/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GfxBench: what graphics.library's and CyberGraphX's drawing calls cost
 * in a window on the Workbench screen, call by call, and (with
 * openrtg.library 0.11 on) whether OpenRTG drew each through OpenGPU or on
 * the CPU. For comparing OpenRTG and OpenGPU builds on the same machine.
 *   GfxBench [N=count] [DELAY=seconds before starting] [WALL=directory]
 * The Amiga's clock may not see time its host spends inside a board's
 * register write (OpenGPU's ring runs there), so WALL= also makes a file in
 * the directory before and after each test ("03a", "03b"): a host reading
 * their times gets the wall clock's answer. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/cybergraphics.h>
#include <proto/openrtg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *CyberGfxBase, *OpenRTGBase;
struct Device *TimerBase;
static struct timerequest treq;
static struct RastPort *rp;
static struct Window *win;
static ULONG before[ORTG_STAT_COUNT * 4], after[ORTG_STAT_COUNT * 4];
static ULONG *argb, *argba;
static UBYTE *pens, tmpl[16 * 64];
static int n = 50, test_no;
static const char *wall;

static void mark(char ab)
{
    char name[256];
    BPTR f;
    if (!wall) return;
    sprintf(name, "%s/%02d%c", wall, test_no, ab);
    if ((f = Open((CONST_STRPTR)name, MODE_NEWFILE)) != 0) Close(f);
}

/* CyberGraphX V43's WritePixelArrayAlpha (LVO -216), not in every SDK's headers. */
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

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

static int stats_ok(void) { return OpenRTGBase && (OpenRTGBase->lib_Version > 0 || OpenRTGBase->lib_Revision >= 11); }

static void report(const char *what, double ms)
{
    printf("%02d %-36s %9.1f us", test_no, what, ms * 1000.0 / n);
    if (stats_ok()) {
        ULONG g = 0, c = 0, k;
        ORTG_DrawStats(after, ORTG_STAT_COUNT, 0);
        for (k = 0; k < ORTG_STAT_COUNT; k++) { g += after[k * 4] - before[k * 4]; c += after[k * 4 + 1] - before[k * 4 + 1]; }
        printf("   pieces: %5lu OpenGPU, %5lu CPU", (unsigned long)g, (unsigned long)c);
    }
    printf("\n");
}

#define BENCH(what, stmt) do { \
    int i_; double t0_; \
    if (stats_ok()) ORTG_DrawStats(before, ORTG_STAT_COUNT, 0); \
    test_no++; mark('a'); \
    t0_ = now_ms(); for (i_ = 0; i_ < n; i_++) { stmt; } WaitBlit(); mark('b'); report(what, now_ms() - t0_); } while (0)

int main(int argc, char **argv)
{
    struct Screen *wb;
    struct BitMap *planar;
    struct RastPort prp;
    static UWORD pat[2] = { 0xAAAA, 0x5555 };
    int i, x0, y0, delay = 0;
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "N=", 2)) n = atoi(argv[i] + 2);
        if (!strncmp(argv[i], "DELAY=", 6)) delay = atoi(argv[i] + 6);
        if (!strncmp(argv[i], "WALL=", 5)) wall = argv[i] + 5;
    }
    if (n < 1) n = 1;
    if (delay) Delay(delay * 50);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) return 20;
    TimerBase = treq.tr_node.io_Device;
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0);
    wb = LockPubScreen(NULL);
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 20, WA_Top, 30, WA_InnerWidth, 420, WA_InnerHeight, 300,
                         WA_Title, (ULONG)"GfxBench", WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, FALSE,
                         WA_SmartRefresh, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, wb);
    if (!win) { printf("no window\n"); return 20; }
    rp = win->RPort;
    x0 = win->BorderLeft; y0 = win->BorderTop;
    argb = AllocVec(128 * 128 * 4, MEMF_ANY);
    argba = AllocVec(128 * 128 * 4, MEMF_ANY);
    pens = AllocVec(128 * 128, MEMF_ANY);
    for (i = 0; i < 128 * 128; i++) {
        argb[i] = 0xFF000000UL | (ULONG)(i * 2654435761UL & 0xFFFFFF);
        argba[i] = ((ULONG)(i * 7) & 255) << 24 | (ULONG)(i * 40503UL & 0xFFFFFF);
        pens[i] = (UBYTE)(i & 7);
    }
    for (i = 0; i < (int)sizeof tmpl; i++) tmpl[i] = (UBYTE)(i * 37 ^ (i >> 3));
    planar = AllocBitMap(128, 64, 4, BMF_CLEAR, NULL);
    InitRastPort(&prp);
    prp.BitMap = planar;
    if (planar) { SetAPen(&prp, 3); RectFill(&prp, 10, 10, 100, 50); }

    printf("GfxBench: %d calls each, on %s, %s\n", n,
           OpenRTGBase ? "openrtg.library" : "no OpenRTG", CyberGfxBase ? "with cybergraphics.library" : "no CyberGraphX");
    if (OpenRTGBase) printf("openrtg.library %d.%d\n", OpenRTGBase->lib_Version, OpenRTGBase->lib_Revision);

    SetAPen(rp, 2); SetBPen(rp, 1); SetDrMd(rp, JAM1);
    BENCH("RectFill 300x200", RectFill(rp, x0, y0, x0 + 299, y0 + 199));
    SetDrMd(rp, COMPLEMENT);
    BENCH("RectFill 300x200 COMPLEMENT", RectFill(rp, x0, y0, x0 + 299, y0 + 199));
    SetDrMd(rp, JAM2); SetAfPt(rp, pat, 1);
    BENCH("RectFill 300x200 area pattern", RectFill(rp, x0, y0, x0 + 299, y0 + 199));
    SetAfPt(rp, NULL, 0); SetDrMd(rp, JAM1);
    BENCH("Text 40 characters JAM1", (Move(rp, x0 + 4, y0 + 20), Text(rp, (STRPTR)"The quick brown fox jumps over the lazy", 40)));
    SetDrMd(rp, JAM2);
    BENCH("Text 40 characters JAM2", (Move(rp, x0 + 4, y0 + 40), Text(rp, (STRPTR)"The quick brown fox jumps over the lazy", 40)));
    SetDrMd(rp, JAM1);
    BENCH("BltTemplate 128x64", BltTemplate((PLANEPTR)tmpl, 0, 16, rp, x0 + 10, y0 + 60, 128, 64));
    BENCH("ClipBlit 300x100 in the window", ClipBlit(rp, x0, y0, rp, x0 + 8, y0 + 100, 300, 100, 0xC0));
    BENCH("ScrollRaster 300x200 by 8", ScrollRaster(rp, 0, 8, x0, y0, x0 + 299, y0 + 199));
    if (planar) BENCH("BltBitMapRastPort planar 128x64", BltBitMapRastPort(planar, 0, 0, rp, x0 + 200, y0 + 10, 128, 64, 0xC0));
    BENCH("20 lines across and down", for (int k = 0; k < 10; k++) { Move(rp, x0, y0 + k * 5); Draw(rp, x0 + 299, y0 + k * 5); Move(rp, x0 + k * 7, y0); Draw(rp, x0 + k * 7, y0 + 199); });
    BENCH("20 lines at 45 degrees", for (int k = 0; k < 20; k++) { Move(rp, x0 + k * 5, y0); Draw(rp, x0 + k * 5 + 150, y0 + 150); });
    BENCH("20 slanted lines", for (int k = 0; k < 20; k++) { Move(rp, x0, y0 + k * 3); Draw(rp, x0 + 299, y0 + 120 + k); });
    BENCH("WritePixel 100 times", for (int k = 0; k < 100; k++) WritePixel(rp, x0 + k, y0 + 250));
    BENCH("WriteChunkyPixels 128x128", WriteChunkyPixels(rp, x0 + 280, y0 + 150, x0 + 407, y0 + 277, pens, 128));
    if (CyberGfxBase) {
        BENCH("WritePixelArray ARGB 128x128", WritePixelArray(argb, 0, 0, 512, rp, x0 + 10, y0 + 150, 128, 128, RECTFMT_ARGB));
        BENCH("WritePixelArray LUT8 128x128", WritePixelArray(pens, 0, 0, 128, rp, x0 + 150, y0 + 150, 128, 128, RECTFMT_LUT8));
        BENCH("ReadPixelArray ARGB 128x128", ReadPixelArray(argb, 0, 0, 512, rp, x0 + 10, y0 + 150, 128, 128, RECTFMT_ARGB));
        BENCH("FillPixelArray 300x200", FillPixelArray(rp, x0, y0, 300, 200, 0x00336699UL));
        BENCH("InvertPixelArray 300x200", InvertPixelArray(rp, x0, y0, 300, 200));
        BENCH("WritePixelArrayAlpha 128x128", wpaa(argba, 0, 0, 512, rp, x0 + 150, y0 + 150, 128, 128, 0xFFFFFFFFUL));
    }
    /* small calls, where a patch's own cost shows (OpenGfx 1.4 patches
     * every drawing call: 8 October 2026) */
    SetDrMd(rp, JAM1);
    BENCH("RectFill 8x8, x100", for (int k = 0; k < 100; k++) RectFill(rp, x0 + (k & 31) * 9, y0 + 220, x0 + (k & 31) * 9 + 7, y0 + 227));
    BENCH("Text 1 character, x100", for (int k = 0; k < 100; k++) { Move(rp, x0 + (k & 31) * 9, y0 + 240); Text(rp, (STRPTR)"x", 1); });
    BENCH("ReadPixel x100", for (int k = 0; k < 100; k++) (void)ReadPixel(rp, x0 + k, y0 + 250));
    if (planar) BENCH("BltBitMapRastPort 16x16, x100", for (int k = 0; k < 100; k++) BltBitMapRastPort(planar, 0, 0, rp, x0 + (k & 15) * 17, y0 + 260, 16, 16, 0xC0));
    if (planar) BENCH("RectFill 8x8 off screen, x100", for (int k = 0; k < 100; k++) RectFill(&prp, (k & 7) * 9, 20, (k & 7) * 9 + 7, 27));
    if (planar) FreeBitMap(planar);
    FreeVec(argb); FreeVec(argba); FreeVec(pens);
    CloseWindow(win);
    if (OpenRTGBase) CloseLibrary(OpenRTGBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CloseDevice(&treq.tr_node);
    return 0;
}
