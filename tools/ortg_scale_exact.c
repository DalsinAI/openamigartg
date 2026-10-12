/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGScaleExact: does a pixel array written at another size
 * (ORTG_WritePixels with dest_width and dest_height, as ScalePixelArray and
 * OpenPlay's films use it) draw the same pixels on the CPU and through
 * OpenGPU's COMPOSITE (0.16)? Each target pixel must show the source pixel
 * under its centre: for target pixel r of d, source pixel s of n is
 * (r * step + step / 2) >> 16 with step = (n << 16) / d.
 *
 * The cases enlarge and shrink by whole and odd ratios, from one pixel to
 * many, from rows at odd addresses, and over a window's edge. Each draws
 * into a window covered in its middle third (so the write is clipped in
 * pieces, some of them into the backing store) and into a bitmap with no
 * layers, on a 32-bit screen and on a 16-bit one where it can open them:
 * ARGB colours on both (every alpha byte non-zero: a 32-bit pixel must come
 * out 00RRGGBB), and on 16-bit a 16-bit bitmap's own pixels (RAW) too. The
 * pixels are read back from the bitmap's memory (ORTG_BitMapInfo). Each
 * case runs with ORTG_DRAW_CPU_ONLY and then with OpenGPU on, and the
 * OpenGPU run says how many pieces OpenGPU drew.
 *   OpenRTGScaleExact     Returns 0 when every case is exact, 10 when one isn't.
 * openrtg.library 0.16 or later. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/openrtg.h>
#include <stdio.h>
#include <string.h>

#define IW 200                  /* the window's inside and the bitmap */
#define IH 150

struct Library *OpenRTGBase;

/* One write: the source rectangle sx, sy, w x h of a buffer whose rows are
 * `mod` = bpp * SW + pad bytes apart and start `off` bytes into it, drawn at
 * dw x dh at dx, dy. */
struct scase { UBYTE off, pad; UWORD sx, sy, w, h; WORD dx, dy; UWORD dw, dh; };
#define SW 140                  /* the source's width in pixels */
#define SH 70
static const struct scase cases[] = {
    { 0, 0,  0,  0,  64, 40,  10,  10, 128,  80 },   /* twice the size */
    { 1, 0,  1,  2,  63, 41,  11,  13, 100,  97 },   /* odd ratios, rows at odd addresses */
    { 2, 1,  3,  1, 131, 60,   5,  20,  50,  23 },   /* smaller */
    { 3, 3,  5,  4,   1,  1,  99,   7,  40,  30 },   /* one pixel to many */
    { 0, 1,  7,  3, 129, 61,   0,   0, 199, 149 },   /* nearly the whole area */
    { 2, 0,  0,  0,  40, 30, 150, 100,  49,  49 },   /* to the far corner */
    { 0, 0,  1,  5,  17, 17, 181, 131,  30,  30 },   /* over the far edges: clipped there */
    { 1, 2,  9,  9,  33,  7, -12,  40,  80,  21 },   /* from beyond the left edge */
    { 0, 0,  0,  0,   3, 64,  60,  60, 120,   9 },   /* wider and much shorter */
};
#define CASES (int)(sizeof cases / sizeof cases[0])

static UBYTE srcbuf[SH * (SW * 4 + 3) + 64] __attribute__((aligned(4)));
static ULONG *snap_base, *snap_before, *snap_now;
static int fails, checks, depth;          /* depth: 16 or 32, of the screen in use */
static ULONG fmt_now;                     /* ORTG_PIX_ARGB or ORTG_PIX_RAW */

static int bpp_of(void) { return fmt_now == ORTG_PIX_RAW ? 2 : 4; }
static const UBYTE *src_of(const struct scase *k) { return srcbuf + k->off; }
static LONG mod_of(const struct scase *k) { return SW * bpp_of() + k->pad; }

/* The pixels at (x, y), IW x IH of them, from the bitmap's memory. */
static int grab(struct BitMap *bm, LONG x, LONG y, ULONG *to)
{
    struct OpenRTGBitMapInfo bi;
    LONG i, j;
    if (!ORTG_BitMapInfo(bm, &bi) || !bi.memory || (bi.depth != 16 && bi.depth != 32)) return 0;
    WaitBlit();
    for (j = 0; j < IH; j++) {
        const UBYTE *row = (const UBYTE *)bi.memory + (y + j) * bi.bytes_per_row;
        for (i = 0; i < IW; i++)
            to[j * IW + i] = bi.depth == 32 ? ((const ULONG *)row)[x + i] : ((const UWORD *)row)[x + i];
    }
    return 1;
}

static LONG scaled(LONG r, LONG n, LONG d)
{
    ULONG step = ((ULONG)n << 16) / (ULONG)d;
    LONG s = (LONG)(((ULONG)r * step + (step >> 1)) >> 16);
    return s >= n ? n - 1 : s;
}

/* What the write must leave at (x, y) of the area: the source pixel under
 * the target pixel's centre, encoded for the bitmap, inside the rectangle
 * drawn; `other` outside it. */
static ULONG want(const struct scase *k, LONG x, LONG y, ULONG other)
{
    const UBYTE *p;
    LONG sx, sy;
    if (x < k->dx || y < k->dy || x >= k->dx + k->dw || y >= k->dy + k->dh) return other;
    sx = k->sx + scaled(x - k->dx, k->w, k->dw);
    sy = k->sy + scaled(y - k->dy, k->h, k->dh);
    p = src_of(k) + sy * mod_of(k) + sx * bpp_of();
    if (fmt_now == ORTG_PIX_RAW) return (ULONG)p[0] << 8 | p[1];
    if (depth == 16) return ((ULONG)p[1] & 0xF8) << 8 | ((ULONG)p[2] & 0xFC) << 3 | p[3] >> 3;
    return (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static void check(int n, const char *mode, const char *where, const ULONG *got, const ULONG *other,
                  LONG cx, LONG cy, LONG cw, LONG ch)
{
    const struct scase *k = &cases[n];
    ULONG bad = 0, alpha = 0, fx = 0, fy = 0, fgot = 0, fwant = 0;
    for (LONG y = 0; y < IH; y++)
        for (LONG x = 0; x < IW; x++) {
            ULONG g = got[y * IW + x], w;
            if (cw && x >= cx && y >= cy && x < cx + cw && y < cy + ch) w = snap_before[y * IW + x];
            else w = want(k, x, y, other[y * IW + x]);
            if (g != w) {
                if (!bad) { fx = x; fy = y; fgot = g; fwant = w; }
                bad++;
                if (depth == 32 && (g & 0xFF000000UL)) alpha++;
            }
        }
    checks++;
    if (bad) {
        fails++;
        printf("%2d-bit %-4s case %d %-8s %-14s DIFFERENT: %lu pixels (%lu with alpha), first at %ld,%ld: %08lx, not %08lx\n",
               depth, fmt_now == ORTG_PIX_RAW ? "RAW" : "ARGB", n, mode, where, (unsigned long)bad, (unsigned long)alpha,
               (long)fx, (long)fy, (unsigned long)fgot, (unsigned long)fwant);
    } else printf("%2d-bit %-4s case %d %-8s %-14s exact\n", depth, fmt_now == ORTG_PIX_RAW ? "RAW" : "ARGB", n, mode, where);
}

static void write_scaled(const struct scase *k, struct RastPort *rp)
{
    struct OpenRTGPixels px;
    memset(&px, 0, sizeof px);
    px.data = (void *)src_of(k);
    px.x = k->sx; px.y = k->sy;
    px.modulo = mod_of(k);
    px.format = fmt_now;
    px.width = k->w; px.height = k->h;
    px.dest_width = k->dw; px.dest_height = k->dh;
    ORTG_WritePixels(rp, k->dx, k->dy, &px);
}

/* One case in a window covered in its middle third, then with the cover gone. */
static int in_window(int n, const char *mode, struct Screen *scr)
{
    const struct scase *k = &cases[n];
    LONG cx = 60, cy = 50, cw = 67, ch = 51, ix, iy;
    struct Window *win, *cover;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 20, WA_Top, 30, WA_Width, IW, WA_Height, IH,
                         WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    if (!win) return 0;
    ix = win->LeftEdge; iy = win->TopEdge;
    ORTG_FillPixels(win->RPort, 0, 0, IW, IH, 0x00102030UL + (ULONG)n * 0x00010101UL);
    if (!grab(win->RPort->BitMap, ix, iy, snap_base)) { CloseWindow(win); return 0; }
    cover = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, ix + cx, WA_Top, iy + cy, WA_Width, cw, WA_Height, ch,
                           WA_Borderless, TRUE, WA_SimpleRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    if (!cover) { CloseWindow(win); return 0; }
    ORTG_FillPixels(cover->RPort, 0, 0, cw, ch, 0x00FF00FFUL);
    grab(win->RPort->BitMap, ix, iy, snap_before);
    write_scaled(k, win->RPort);
    grab(win->RPort->BitMap, ix, iy, snap_now);
    check(n, mode, "window covered", snap_now, snap_base, cx, cy, cw, ch);
    CloseWindow(cover);             /* the covered pieces went to the backing store, and come back now */
    grab(win->RPort->BitMap, ix, iy, snap_now);
    check(n, mode, "window", snap_now, snap_base, 0, 0, 0, 0);
    CloseWindow(win);
    return 1;
}

/* One case in a bitmap of the screen's format, with no layers. */
static int in_bitmap(int n, const char *mode, struct Screen *scr)
{
    struct BitMap *bm = AllocBitMap(IW, IH, depth, BMF_CLEAR, scr->RastPort.BitMap);
    struct RastPort rp;
    int r = 0;
    if (!bm) return 0;
    InitRastPort(&rp);
    rp.BitMap = bm;
    ORTG_FillPixels(&rp, 0, 0, IW, IH, 0x00405060UL + (ULONG)n * 0x00010101UL);
    if (grab(bm, 0, 0, snap_base)) {
        write_scaled(&cases[n], &rp);
        grab(bm, 0, 0, snap_now);
        check(n, mode, "bitmap", snap_now, snap_base, 0, 0, 0, 0);
        r = 1;
    }
    FreeBitMap(bm);
    return r;
}

/* A screen of its own, 640 x 480 at depth d, behind; NULL when OpenRTG has
 * no such mode. */
static struct Screen *open_depth(int d)
{
    struct OpenRTGBitMapInfo bi;
    struct Screen *s;
    ULONG id = ORTG_BestMode(1, 640, 480, (ULONG)d);
    if (id == INVALID_ID || !id) return NULL;
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, d, SA_Title, (ULONG)"OpenRTGScaleExact",
                       SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Behind, TRUE, TAG_DONE);
    if (s && !(ORTG_BitMapInfo(s->RastPort.BitMap, &bi) && bi.depth == d)) { CloseScreen(s); s = NULL; }
    return s;
}

static void run(struct Screen *scr, int *rc)
{
    static const char *const modes[] = { "CPU", "OpenGPU" };
    static const ULONG flags[] = { ORTG_DRAW_CPU_ONLY, ORTG_DRAW_OPENGPU };
    ULONG counts[ORTG_STAT_COUNT * 4];
    int m, n;
    for (m = 0; m < 2; m++) {
        ORTG_DrawStats(NULL, 0, flags[m] | ORTG_DRAW_RESET);
        for (n = 0; n < CASES; n++) {
            if (!in_window(n, modes[m], scr)) { printf("case %d %s window: no window, or its bitmap can't be read\n", n, modes[m]); *rc = 20; }
            if (!in_bitmap(n, modes[m], scr)) { printf("case %d %s bitmap: no bitmap of the screen's format\n", n, modes[m]); *rc = 20; }
        }
        ORTG_DrawStats(counts, ORTG_STAT_COUNT, 0);
        printf("%2d-bit %-4s %-8s pixel pieces: %lu by OpenGPU, %lu by the CPU\n", depth, fmt_now == ORTG_PIX_RAW ? "RAW" : "ARGB",
               modes[m], (unsigned long)counts[ORTG_STAT_PIXELS * 4], (unsigned long)counts[ORTG_STAT_PIXELS * 4 + 1]);
    }
    ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
}

int main(void)
{
    static const int depths[] = { 32, 16 };
    struct Screen *scr;
    ULONG i;
    int d, rc = 0, ran = 0;
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0)) || (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 16)) {
        printf("OpenRTGScaleExact: needs openrtg.library 0.16\n");
        if (OpenRTGBase) CloseLibrary(OpenRTGBase);
        return 20;
    }
    /* every byte non-zero, so every alpha byte is */
    for (i = 0; i < sizeof srcbuf; i++) srcbuf[i] = (UBYTE)(1 + (i * 2654435761UL >> 13) % 255);
    if (!(snap_base = AllocVec(IW * IH * 4 * 3, MEMF_ANY))) { printf("OpenRTGScaleExact: no memory\n"); CloseLibrary(OpenRTGBase); return 20; }
    snap_before = snap_base + IW * IH;
    snap_now = snap_before + IW * IH;
    for (d = 0; d < 2; d++) {
        if (!(scr = open_depth(depths[d]))) { printf("OpenRTGScaleExact: no %d-bit OpenRTG screen, so no %d-bit cases\n", depths[d], depths[d]); continue; }
        depth = depths[d];
        fmt_now = ORTG_PIX_ARGB;
        run(scr, &rc);
        if (depth == 16) { fmt_now = ORTG_PIX_RAW; run(scr, &rc); }
        CloseScreen(scr);
        ran++;
    }
    if (!ran) rc = 20;
    printf("OpenRTGScaleExact: %d of %d exact%s\n", checks - fails, checks, fails ? "" : ": the CPU and OpenGPU draw the same pixels");
    if (fails && rc < 10) rc = 10;
    FreeVec(snap_base);
    CloseLibrary(OpenRTGBase);
    return rc;
}
