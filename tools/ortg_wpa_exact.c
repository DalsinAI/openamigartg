/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGWPAExact: does WritePixelArray(RECTFMT_ARGB) onto a 32-bit OpenRTG
 * bitmap write exactly the source's colours with alpha 0? It is the path
 * every GL or SDL frame in a window takes (the row copy of 0.14, #68).
 * The source has non-zero alpha bytes everywhere, its rows start at odd
 * addresses and odd offsets, and the widths are odd. Each case draws into a
 * window partly covered by another (so the copy is clipped in pieces, some
 * of them into the window's backing store) and into a bitmap with no layers,
 * then reads the bitmap's longwords back with LockBitMapTagList: every pixel
 * drawn must be 00RRGGBB, and every other pixel as it was. Each case runs
 * with ORTG_DRAW_CPU_ONLY and then with OpenGPU on.
 * openrtg.library 0.11 or later. It opens a 32-bit screen of its own, or
 * uses the Workbench screen when it can't and that is 32-bit.
 *   OpenRTGWPAExact         Returns 0 when every case is exact, 10 when one isn't. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/openrtg.h>
#include <stdio.h>
#include <string.h>

#define IW 200                  /* the window's inside and the bitmap */
#define IH 150

struct Library *CyberGfxBase, *OpenRTGBase;

/* One WritePixelArray: the source starts `off` bytes into an aligned buffer
 * and its rows are `mod` = 4 * SW + pad bytes apart, so with an odd offset or
 * an odd pad the rows start at odd addresses. */
struct wcase { UBYTE off, pad; UWORD sx, sy, w, h, dx, dy; };
#define SW 140                  /* the source's width in pixels */
static const struct wcase cases[] = {
    { 0, 0,  0,  0,  64, 40,  10,  10 },   /* aligned, for comparison */
    { 1, 0,  1,  2,  63, 41,  11,  13 },   /* every row odd */
    { 2, 1,  3,  1,  33, 37,  40,  20 },   /* odd pad: rows odd and even in turn */
    { 3, 3,  5,  4,   1, 60,  99,   7 },   /* one pixel wide */
    { 1, 2,  2,  0, 127, 50,   7,  51 },
    { 0, 1,  7,  3,   3, 29, 150, 100 },
    { 2, 0,  0,  0, 131, 23,  33,  77 },
    { 0, 0,  1,  5,  17, 17, 181, 131 },   /* to the far corner */
};
#define CASES (int)(sizeof cases / sizeof cases[0])

static UBYTE srcbuf[70 * (SW * 4 + 3) + 64] __attribute__((aligned(4)));
static ULONG *snap_base, *snap_before, *snap_now;
static int fails, checks;
static ULONG on_tag = WA_CustomScreen;  /* WA_PubScreen on the Workbench screen */

/* The pixels at (x, y), IW x IH of them, read with LockBitMapTagList. */
static int grab(struct BitMap *bm, LONG x, LONG y, ULONG *to)
{
    ULONG base = 0, bpr = 0, bpp = 0, fmt = 0;
    struct TagItem tags[] = { { LBMI_BASEADDRESS, (ULONG)&base }, { LBMI_BYTESPERROW, (ULONG)&bpr },
                              { LBMI_BYTESPERPIX, (ULONG)&bpp }, { LBMI_PIXFMT, (ULONG)&fmt }, { TAG_DONE, 0 } };
    APTR lock = LockBitMapTagList(bm, tags);
    int r;
    if (!lock) return 0;
    if ((r = base && bpp == 4 && fmt == PIXFMT_ARGB32))
        for (LONG i = 0; i < IH; i++) memcpy(to + i * IW, (const UBYTE *)base + (y + i) * bpr + x * 4, IW * 4);
    UnLockBitMap(lock);
    return r;
}

static const UBYTE *src_of(const struct wcase *k) { return srcbuf + k->off; }
static ULONG mod_of(const struct wcase *k) { return SW * 4 + k->pad; }

/* What WritePixelArray must leave at (x, y) of the area: the source's colour
 * with alpha 0 inside the rectangle drawn, `other` outside it. */
static ULONG want(const struct wcase *k, LONG x, LONG y, ULONG other)
{
    const UBYTE *p;
    if (x < k->dx || y < k->dy || x >= k->dx + k->w || y >= k->dy + k->h) return other;
    p = src_of(k) + (k->sy + y - k->dy) * mod_of(k) + (k->sx + x - k->dx) * 4;
    return (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

/* Compares `got` with what the case should have drawn. Under the cover
 * (cw > 0) the screen must still show what `before` had there. */
static void check(int n, const char *mode, const char *where, const ULONG *got, const ULONG *other,
                  LONG cx, LONG cy, LONG cw, LONG ch)
{
    const struct wcase *k = &cases[n];
    ULONG bad = 0, alpha = 0, fx = 0, fy = 0, fgot = 0, fwant = 0;
    for (LONG y = 0; y < IH; y++)
        for (LONG x = 0; x < IW; x++) {
            ULONG g = got[y * IW + x], w;
            if (cw && x >= cx && y >= cy && x < cx + cw && y < cy + ch) w = snap_before[y * IW + x];
            else w = want(k, x, y, other[y * IW + x]);
            if (g != w) {
                if (!bad) { fx = x; fy = y; fgot = g; fwant = w; }
                bad++;
                if (g & 0xFF000000UL) alpha++;
            }
        }
    checks++;
    if (bad) {
        fails++;
        printf("case %d %-8s %-14s DIFFERENT: %lu pixels (%lu with alpha), first at %ld,%ld: %08lx, not %08lx\n", n, mode, where,
               (unsigned long)bad, (unsigned long)alpha, (long)fx, (long)fy, (unsigned long)fgot, (unsigned long)fwant);
    } else printf("case %d %-8s %-14s exact\n", n, mode, where);
}

static void wpa(const struct wcase *k, struct RastPort *rp, LONG x, LONG y)
{
    WritePixelArray((APTR)src_of(k), k->sx, k->sy, mod_of(k), rp, x + k->dx, y + k->dy, k->w, k->h, RECTFMT_ARGB);
}

/* One case in a window covered in its middle third, then with the cover gone. */
static int in_window(int n, const char *mode, struct Screen *scr)
{
    const struct wcase *k = &cases[n];
    LONG cx = k->dx + k->w / 3, cy = k->dy + k->h / 3, cw = k->w / 3 + 1, ch = k->h / 3 + 1, ix, iy;
    struct Window *win, *cover;
    win = OpenWindowTags(NULL, on_tag, (ULONG)scr, WA_Left, 20, WA_Top, 30, WA_Width, IW, WA_Height, IH,
                         WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    if (!win) return 0;
    ix = win->LeftEdge; iy = win->TopEdge;
    FillPixelArray(win->RPort, 0, 0, IW, IH, 0x00102030UL + (ULONG)n * 0x00010101UL);
    if (!grab(win->RPort->BitMap, ix, iy, snap_base)) { CloseWindow(win); return 0; }
    cover = OpenWindowTags(NULL, on_tag, (ULONG)scr, WA_Left, ix + cx, WA_Top, iy + cy, WA_Width, cw, WA_Height, ch,
                           WA_Borderless, TRUE, WA_SimpleRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    if (!cover) { CloseWindow(win); return 0; }
    SetAPen(cover->RPort, 1);
    RectFill(cover->RPort, 0, 0, cw - 1, ch - 1);
    grab(win->RPort->BitMap, ix, iy, snap_before);
    wpa(k, win->RPort, 0, 0);
    grab(win->RPort->BitMap, ix, iy, snap_now);
    check(n, mode, "window covered", snap_now, snap_base, cx, cy, cw, ch);
    /* the covered pieces went to the window's backing store, and come back now */
    CloseWindow(cover);
    grab(win->RPort->BitMap, ix, iy, snap_now);
    check(n, mode, "window", snap_now, snap_base, 0, 0, 0, 0);
    CloseWindow(win);
    return 1;
}

/* One case in a bitmap of the screen's format, with no layers. */
static int in_bitmap(int n, const char *mode, struct Screen *scr)
{
    struct BitMap *bm = AllocBitMap(IW, IH, 32, BMF_CLEAR, scr->RastPort.BitMap);
    struct RastPort rp;
    int r = 0;
    if (!bm) return 0;
    InitRastPort(&rp);
    rp.BitMap = bm;
    FillPixelArray(&rp, 0, 0, IW, IH, 0x00405060UL + (ULONG)n * 0x00010101UL);
    if (grab(bm, 0, 0, snap_base)) {
        wpa(&cases[n], &rp, 0, 0);
        grab(bm, 0, 0, snap_now);
        check(n, mode, "bitmap", snap_now, snap_base, 0, 0, 0, 0);
        r = 1;
    }
    FreeBitMap(bm);
    return r;
}

int main(void)
{
    static const char *const modes[] = { "CPU", "OpenGPU" };
    static const ULONG flags[] = { ORTG_DRAW_CPU_ONLY, ORTG_DRAW_OPENGPU };
    struct Screen *scr = NULL, *wb = NULL;
    struct OpenRTGBitMapInfo bi;
    ULONG id, i;
    int m, n, rc = 0;
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0)) || (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 11)) {
        printf("OpenRTGWPAExact: needs openrtg.library 0.11\n");
        return 20;
    }
    if (!(CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40))) {
        printf("OpenRTGWPAExact: needs cybergraphics.library\n");
        CloseLibrary(OpenRTGBase);
        return 20;
    }
    /* every byte non-zero, so every alpha byte is */
    for (i = 0; i < sizeof srcbuf; i++) srcbuf[i] = (UBYTE)(1 + (i * 2654435761UL >> 13) % 255);
    snap_base = AllocVec(IW * IH * 4 * 3, MEMF_ANY);
    if (!snap_base) { rc = 20; printf("OpenRTGWPAExact: no memory\n"); goto out; }
    snap_before = snap_base + IW * IH;
    snap_now = snap_before + IW * IH;
    {
        struct TagItem bt[] = { { CYBRBIDTG_Depth, 32 }, { CYBRBIDTG_NominalWidth, 640 }, { CYBRBIDTG_NominalHeight, 480 }, { TAG_DONE, 0 } };
        id = BestCModeIDTagList(bt);
    }
    if (id != INVALID_ID)
        scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, 32, SA_Title, (ULONG)"OpenRTGWPAExact",
                             SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Behind, TRUE, TAG_DONE);
    if (scr && !(ORTG_BitMapInfo(scr->RastPort.BitMap, &bi) && bi.depth == 32)) { CloseScreen(scr); scr = NULL; }
    if (!scr) {
        wb = LockPubScreen(NULL);
        if (!wb || !ORTG_BitMapInfo(wb->RastPort.BitMap, &bi) || bi.depth != 32) {
            printf("OpenRTGWPAExact: no 32-bit OpenRTG screen of its own, and the Workbench screen isn't one\n");
            rc = 20;
            goto out;
        }
        on_tag = WA_PubScreen;
        printf("OpenRTGWPAExact: on the Workbench screen\n");
    }
    for (m = 0; m < 2; m++) {
        ORTG_DrawStats(NULL, 0, flags[m]);
        for (n = 0; n < CASES; n++) {
            if (!in_window(n, modes[m], scr ? scr : wb)) { printf("case %d %-8s window: no windows, or no 32-bit bitmap to read\n", n, modes[m]); rc = 20; }
            if (!in_bitmap(n, modes[m], scr ? scr : wb)) { printf("case %d %-8s bitmap: no 32-bit OpenRTG bitmap\n", n, modes[m]); rc = 20; }
        }
    }
    ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
    printf("OpenRTGWPAExact: %d of %d exact%s\n", checks - fails, checks, fails ? "" : ": every pixel 00RRGGBB, nothing else touched");
    if (fails && rc < 10) rc = 10;
out:
    if (scr) CloseScreen(scr);
    if (wb) UnlockPubScreen(NULL, wb);
    if (snap_base) FreeVec(snap_base);
    CloseLibrary(CyberGfxBase);
    CloseLibrary(OpenRTGBase);
    return rc;
}
