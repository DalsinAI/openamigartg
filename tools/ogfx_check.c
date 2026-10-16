/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGfxCheck: does OpenGfx, inside opengpu.library, draw exactly what
 * graphics.library draws, and how much faster?
 *
 * It draws the same random calls three ways on standard (planar) bitmaps:
 *   - graphics.library's own code (OpenGfx switched off for the call);
 *   - opengpu.library's OGFX_ LVOs, OpenGfx on;
 *   - through graphics.library with OpenGfx on, so through the patches'
 *     entries, when the patches are in;
 * and compares the bitmaps byte for byte after every call, with each call's
 * result. RectFill (JAM1, JAM2, COMPLEMENT, area patterns, plane masks),
 * BltBitMap (every minterm, plane masks, overlapping copies within one
 * bitmap and copies between two), ScrollRaster, BltTemplate and the four
 * text calls, on 1 to 8 planes. From opengpu.library 0.10, the calls
 * OpenGfx's core (openamigagfx library/core) draws: Draw, PolyDraw,
 * WritePixel, ReadPixel, SetRast, BltPattern, WritePixelArray8,
 * ReadPixelArray8 and WriteChunkyPixels, every DrawMode; these have no
 * LVO, so they go through the patches (graphics.library's pixels are
 * copied to the other ways when the patches are not in). From 0.11 a
 * third of them draw in a layer (a simple refresh window with another in
 * front of it). From 0.12 BltBitMapRastPort too, from the way's other
 * bitmap. From 0.13 ClipBlit (within the RastPort, or from the way's
 * other one) and ScrollRaster too, and the window is a smart refresh one,
 * where the core takes those two. From 0.14 area fills (one or two
 * polygons and maybe an ellipse, some with an outline), DrawEllipse and
 * EraseRect too. On AmigaChrome (OGFX_STATUS_AMIGACHROME)
 * OpenGfx's leaves take Chip RAM, so the leaves are what is checked; on a
 * real Amiga they leave Chip RAM to the blitter.
 *
 *   OpenGfxCheck [INSTALL] [ON|OFF] [QUICK] [NOCHECK] [VERBOSE]
 * INSTALL puts the patches in first; ON or OFF switches OpenGfx's own
 * drawing and leaves it so. Without NOCHECK the check runs (OpenGfx's
 * switch is put back afterwards); without QUICK it also times full-screen
 * fills, copies and scrolls with OpenGfx on and off. VERBOSE names each
 * call before it is made.
 * With opengpu.library 0.7 the check also tries the look hook (gfx.h): a
 * look registered for the check's own RastPort is asked first on RectFill
 * and Text, by the LVOs and through the patches, sees 16-bit arguments, ends
 * the call when it draws, and stops being asked once taken out. It is
 * skipped while another look (OpenLook) is in.
 * Returns 0 when everything matches, 10 when something doesn't, 20 when
 * opengpu.library has no OpenGfx. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/timer.h>
#include <proto/opengpu.h>
#include <stdio.h>
#include <string.h>

struct Library *OpenGPUBase;
struct Device *TimerBase;
static struct timerequest treq;

#define W 160
#define H 100
#define N_DEPTHS 6
static const int depths[N_DEPTHS] = { 1, 2, 3, 4, 5, 8 };

static ULONG rng = 2463534242UL;
static ULONG rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static LONG rnd_in(LONG lo, LONG hi) { return lo + (LONG)(rnd() % (ULONG)(hi - lo + 1)); }

/* graphics.library; OpenGfx's LVOs; through the patches; OpenGfx's LVOs on
 * bitmaps in Fast RAM, which only OpenGfx's leaves draw (the blitter can't
 * reach them), against graphics.library's pixels in Chip RAM */
enum { REF, LVO, PAT, FAST, WAYS };
static const char *const way_name[WAYS] = { "graphics.library", "OGFX LVO", "patched", "OGFX LVO, Fast RAM" };
static struct BitMap fast_bm[2];
static int have_fast;
static LONG fast_r;                    /* the Fast RAM way's result */
static int p96_counts;                 /* BltBitMap answers counted Picasso96's way */

struct way {
    struct BitMap *bm[2];              /* two of the same depth, for copies between bitmaps */
    struct RastPort rp[2];
};

static struct way ways[WAYS];
static int depth, patched, fails, calls, shown, verbose;
static char args[128];                 /* the call's arguments, for a report */
static int fails_of[6];                /* by call: RectFill, BltBitMap, ScrollRaster, BltTemplate, text, core */
static int kind;
static UWORD area_ptrn[4] = { 0xAAAA, 0x5555, 0xF0F0, 0x0F0F };
static UBYTE *tmpl;                    /* a template in Chip RAM: 32 x 16, 4 bytes a row */

static void on(int yes) { (void)OGFX_SetEnabled(yes ? 1 : 0); }
static void say(const char *what) { if (verbose) { printf("  %s, %d planes\n", what, depth); fflush(stdout); } }

/* Row by row: the bitmaps' rows may be padded differently (AllocBitMap's
 * and the Fast RAM way's own). */
static UBYTE *at(struct BitMap *bm, int p, int y) { return (UBYTE *)bm->Planes[p] + (ULONG)y * bm->BytesPerRow; }

static void copy_bm(struct BitMap *from, struct BitMap *to)
{
    int p, y;
    for (p = 0; p < depth; p++)
        for (y = 0; y < H; y++) CopyMem(at(from, p, y), at(to, p, y), W / 8);
}

static int parts_same(int w, int fix);

/* Where way w first differs from graphics.library's: a report line. */
static void where(int w)
{
    int k, p, y, x, y2, x2;
    for (k = 0; k < 2; k++) {
        struct BitMap *a = ways[REF].bm[k], *b = ways[w].bm[k];
        for (p = 0; p < depth; p++)
            for (y = 0; y < H; y++)
                for (x = 0; x < W / 8; x++)
                    if (at(a, p, y)[x] != at(b, p, y)[x]) {
                        ULONG d = 0;
                        for (y2 = 0; y2 < H; y2++)
                            for (x2 = 0; x2 < W / 8; x2++) d += at(a, p, y2)[x2] != at(b, p, y2)[x2];
                        printf("    first in bitmap %c plane %d at x %d..%d, y %d: %02x, graphics.library %02x (%lu bytes differ in that plane)\n",
                               'a' + k, p, x * 8, x * 8 + 7, y, at(b, p, y)[x], at(a, p, y)[x], (unsigned long)d);
                        return;
                    }
    }
    if (!parts_same(w, 0)) printf("    in the window's covered part (the smart refresh layer's own bitmap)\n");
}

static int same(int w)
{
    int k, p, y;
    for (k = 0; k < 2; k++)
        for (p = 0; p < depth; p++)
            for (y = 0; y < H; y++)
                if (memcmp(at(ways[REF].bm[k], p, y), at(ways[w].bm[k], p, y), W / 8)) return 0;
    return parts_same(w, 0);
}

/* The Fast RAM way takes graphics.library's pixels, for a call only
 * graphics.library draws (it would reach for them with the blitter). */
static void sync_fast(LONG r0)
{
    fast_r = r0;
    if (!have_fast) return;
    WaitBlit();
    copy_bm(ways[REF].bm[0], ways[FAST].bm[0]);
    copy_bm(ways[REF].bm[1], ways[FAST].bm[1]);
}

static void compare(const char *what, LONG r0, LONG r1, LONG r2)
{
    int w;
    LONG r[WAYS];
    r[REF] = r0; r[LVO] = r1; r[PAT] = r2;
    WaitBlit();
    calls++;
    r[FAST] = fast_r;
    for (w = LVO; w < WAYS; w++) {
        if (w == PAT && !patched) continue;
        if (w == FAST && !have_fast) continue;
        if (!same(w) || r[w] != r[REF]) {
            fails++;
            if (fails_of[kind]++ < 4 && shown++ < 20) {
                printf("  DIFFERENT: %s(%s), %d planes, %s: %s (result %ld, graphics.library %ld)\n",
                       what, args, depth, way_name[w], same(w) ? "same pixels" : "pixels differ", (long)r[w], (long)r[REF]);
                if (!same(w)) where(w);
            }
            /* start the next call from the same pixels */
            copy_bm(ways[REF].bm[0], ways[w].bm[0]);
            copy_bm(ways[REF].bm[1], ways[w].bm[1]);
            parts_same(w, 1);
        }
    }
}

/* A way takes graphics.library's pixels, for a call it can't make. */
static void sync_way(int w)
{
    WaitBlit();
    copy_bm(ways[REF].bm[0], ways[w].bm[0]);
    copy_bm(ways[REF].bm[1], ways[w].bm[1]);
    parts_same(w, 1);
}

static void set_rp(struct RastPort *rp, ULONG fg, ULONG bg, ULONG mode, ULONG mask, int ptrn)
{
    SetAPen(rp, fg); SetBPen(rp, bg); SetDrMd(rp, mode);
    rp->Mask = (UBYTE)mask;
    if (ptrn) { SetAfPt(rp, area_ptrn, 2); } else { SetAfPt(rp, NULL, 0); }
}

static void one_rectfill(void)
{
    LONG x0 = rnd_in(0, W - 1), y0 = rnd_in(0, H - 1), x1 = rnd_in(x0, W - 1), y1 = rnd_in(y0, H - 1);
    ULONG fg = rnd() & 255, bg = rnd() & 255, mask = rnd() & 3 ? 0xFF : rnd() & 255;
    static const ULONG modes[] = { JAM1, JAM2, COMPLEMENT, JAM2, JAM1 | INVERSVID };
    ULONG mode = modes[rnd() % 5];
    int ptrn = (rnd() & 7) == 0, k = rnd() & 1, w;
    for (w = 0; w < WAYS; w++) set_rp(&ways[w].rp[k], fg, bg, mode, mask, ptrn);
    sprintf(args, "%ld,%ld,%ld,%ld fg %lu mode %lu mask %02lx%s", (long)x0, (long)y0, (long)x1, (long)y1, (unsigned long)fg,
            (unsigned long)mode, (unsigned long)mask, ptrn ? " pattern" : "");
    kind = 0;
    on(0); RectFill(&ways[REF].rp[k], x0, y0, x1, y1);
    on(1); OGFX_RectFill(&ways[LVO].rp[k], x0, y0, x1, y1);
    if (patched) RectFill(&ways[PAT].rp[k], x0, y0, x1, y1);
    if (have_fast) { OGFX_RectFill(&ways[FAST].rp[k], x0, y0, x1, y1); fast_r = 0; }
    else sync_fast(0);
    compare("RectFill", 0, 0, 0);
}

static void one_bltbitmap(void)
{
    LONG w = rnd_in(1, W), h = rnd_in(1, H);
    LONG sx = rnd_in(0, W - w), sy = rnd_in(0, H - h), dx = rnd_in(0, W - w), dy = rnd_in(0, H - h);
    ULONG minterm = rnd() & 0xF0, mask = rnd() & 3 ? 0xFF : rnd() & 255;
    int s = rnd() & 1, d = rnd() & 3 ? s : !s;     /* mostly within one bitmap, so overlapping */
    LONG r0, r1, r2 = 0;
    if ((rnd() & 3) == 0) minterm = 0xC0;
    if (!(mask & ((1u << depth) - 1))) mask |= 1;   /* (no plane in the mask: graphics.library's, every way) */
    sprintf(args, "%s%ld,%ld -> %s%ld,%ld %ldx%ld minterm %02lx mask %02lx", s ? "b:" : "a:", (long)sx, (long)sy, d ? "b:" : "a:",
            (long)dx, (long)dy, (long)w, (long)h, (unsigned long)minterm, (unsigned long)mask);
    kind = 1;
    on(0); r0 = BltBitMap(ways[REF].bm[s], sx, sy, ways[REF].bm[d], dx, dy, w, h, minterm, mask, NULL);
    on(1); r1 = OGFX_BltBitMap(ways[LVO].bm[s], sx, sy, ways[LVO].bm[d], dx, dy, w, h, minterm, mask, NULL);
    if (patched) r2 = BltBitMap(ways[PAT].bm[s], sx, sy, ways[PAT].bm[d], dx, dy, w, h, minterm, mask, NULL);
    else r2 = r0;
    if (have_fast) {
        /* Fast RAM is OpenGfx's own: it answers as graphics.library's own
         * BltBitMap does, with the planes drawn (those in the mask). Under
         * Picasso96, graphics.library's answer above is Picasso96's: every
         * plane the two bitmaps share. Either is taken. */
        LONG rom = 0;
        int p;
        for (p = 0; p < depth; p++) rom += (mask >> p) & 1;
        fast_r = OGFX_BltBitMap(ways[FAST].bm[s], sx, sy, ways[FAST].bm[d], dx, dy, w, h, minterm, mask, NULL);
        if (fast_r == rom && r0 == depth && rom != depth) { p96_counts++; fast_r = r0; }
    } else sync_fast(r0);
    compare("BltBitMap", r0, r1, r2);
}

static void one_scroll(void)
{
    LONG x0 = rnd_in(0, W - 1), y0 = rnd_in(0, H - 1), x1 = rnd_in(x0, W - 1), y1 = rnd_in(y0, H - 1);
    LONG dx = rnd_in(-24, 24), dy = rnd_in(-24, 24);
    ULONG bg = rnd() & 255, mask = rnd() & 3 ? 0xFF : rnd() & 255;
    int k = rnd() & 1, w;
    if (rnd() & 1) dx = 0; else if (rnd() & 1) dy = 0;
    for (w = 0; w < WAYS; w++) set_rp(&ways[w].rp[k], 1, bg, JAM2, mask, 0);
    sprintf(args, "%ld,%ld in %ld,%ld,%ld,%ld bg %lu mask %02lx", (long)dx, (long)dy, (long)x0, (long)y0, (long)x1, (long)y1,
            (unsigned long)bg, (unsigned long)mask);
    kind = 2;
    on(0); ScrollRaster(&ways[REF].rp[k], dx, dy, x0, y0, x1, y1);
    on(1); OGFX_ScrollRaster(&ways[LVO].rp[k], dx, dy, x0, y0, x1, y1);
    if (patched) ScrollRaster(&ways[PAT].rp[k], dx, dy, x0, y0, x1, y1);
    if (have_fast && (dx < 0 ? -dx : dx) <= x1 - x0 && (dy < 0 ? -dy : dy) <= y1 - y0) { OGFX_ScrollRaster(&ways[FAST].rp[k], dx, dy, x0, y0, x1, y1); fast_r = 0; }
    else sync_fast(0);
    compare("ScrollRaster", 0, 0, 0);
}

static void one_template(void)
{
    LONG w = rnd_in(1, 32), h = rnd_in(1, 16), x = rnd_in(0, W - w), y = rnd_in(0, H - h), sx = rnd_in(0, 32 - w);
    ULONG fg = rnd() & 255, bg = rnd() & 255;
    static const ULONG modes[] = { JAM1, JAM2, COMPLEMENT };
    ULONG mode = modes[rnd() % 3];
    int k = rnd() & 1, i;
    for (i = 0; i < WAYS; i++) set_rp(&ways[i].rp[k], fg, bg, mode, 0xFF, 0);
    sprintf(args, "%ld at %ld,%ld %ldx%ld mode %lu", (long)sx, (long)x, (long)y, (long)w, (long)h, (unsigned long)mode);
    kind = 3;
    on(0); BltTemplate(tmpl, sx, 4, &ways[REF].rp[k], x, y, w, h);
    on(1); OGFX_BltTemplate(tmpl, sx, 4, &ways[LVO].rp[k], x, y, w, h);
    if (patched) BltTemplate(tmpl, sx, 4, &ways[PAT].rp[k], x, y, w, h);
    sync_fast(0);
    compare("BltTemplate", 0, 0, 0);
}

static void one_text(void)
{
    static const char words[] = "OpenGfx inside opengpu.library: one library for all the drawing";
    LONG n = rnd_in(1, 18), at = rnd_in(0, (LONG)sizeof words - 1 - n), x = rnd_in(2, W - 8 * n - 6 > 2 ? W - 8 * n - 6 : 2), y = rnd_in(8, H - 2);
    struct TextExtent te[WAYS], ce;
    ULONG fit[WAYS], len[WAYS], drawn[WAYS];
    ULONG fg = rnd() & 255, bg = rnd() & 255, mode = rnd() & 3 ? JAM2 : rnd() & 7, style = rnd() & 3 ? 0 : rnd() & 7;
    int k = rnd() & 1, i;
    for (i = 0; i < WAYS; i++) {
        set_rp(&ways[i].rp[k], fg, bg, mode, 0xFF, 0);
        SetSoftStyle(&ways[i].rp[k], style, 0xFF);
        Move(&ways[i].rp[k], x, y);
    }
    memset(te, 0, sizeof te); memset(&ce, 0, sizeof ce);
    sprintf(args, "%ld chars at %ld,%ld, mode %lu, style %lu", (long)n, (long)x, (long)y, (unsigned long)mode, (unsigned long)style);
    kind = 4;
    ce.te_Width = (WORD)rnd_in(0, 100); ce.te_Height = 20; ce.te_Extent.MinX = 0; ce.te_Extent.MinY = -8;
    ce.te_Extent.MaxX = ce.te_Width; ce.te_Extent.MaxY = 8;
    on(0);
    drawn[REF] = (ULONG)Text(&ways[REF].rp[k], (STRPTR)words + at, n);
    len[REF] = (ULONG)TextLength(&ways[REF].rp[k], (STRPTR)words + at, n);
    TextExtent(&ways[REF].rp[k], (STRPTR)words + at, n, &te[REF]);
    fit[REF] = TextFit(&ways[REF].rp[k], (STRPTR)words + at, n, &te[REF], &ce, 1, ce.te_Width, 20);
    on(1);
    drawn[LVO] = (ULONG)OGFX_Text(&ways[LVO].rp[k], (STRPTR)words + at, n);
    len[LVO] = (ULONG)OGFX_TextLength(&ways[LVO].rp[k], (STRPTR)words + at, n);
    OGFX_TextExtent(&ways[LVO].rp[k], (STRPTR)words + at, n, &te[LVO]);
    fit[LVO] = OGFX_TextFit(&ways[LVO].rp[k], (STRPTR)words + at, n, &te[LVO], &ce, 1, ce.te_Width, 20);
    if (patched) {
        drawn[PAT] = (ULONG)Text(&ways[PAT].rp[k], (STRPTR)words + at, n);
        len[PAT] = (ULONG)TextLength(&ways[PAT].rp[k], (STRPTR)words + at, n);
        TextExtent(&ways[PAT].rp[k], (STRPTR)words + at, n, &te[PAT]);
        fit[PAT] = TextFit(&ways[PAT].rp[k], (STRPTR)words + at, n, &te[PAT], &ce, 1, ce.te_Width, 20);
    } else { drawn[PAT] = drawn[REF]; len[PAT] = len[REF]; fit[PAT] = fit[REF]; te[PAT] = te[REF]; }
    /* the Fast RAM way is the one a real Amiga's core draws text on (the
     * default font is one the core takes, so nothing reaches the blitter) */
    if (have_fast) {
        drawn[FAST] = (ULONG)OGFX_Text(&ways[FAST].rp[k], (STRPTR)words + at, n);
        len[FAST] = (ULONG)OGFX_TextLength(&ways[FAST].rp[k], (STRPTR)words + at, n);
        OGFX_TextExtent(&ways[FAST].rp[k], (STRPTR)words + at, n, &te[FAST]);
        fit[FAST] = OGFX_TextFit(&ways[FAST].rp[k], (STRPTR)words + at, n, &te[FAST], &ce, 1, ce.te_Width, 20);
        fast_r = (LONG)drawn[FAST];
    } else sync_fast((LONG)drawn[REF]);
    compare("Text", (LONG)drawn[REF], (LONG)drawn[LVO], (LONG)drawn[PAT]);
    calls += 3;
    for (i = LVO; i < (have_fast ? WAYS : FAST); i++)
        if (len[i] != len[REF] || fit[i] != fit[REF] || memcmp(&te[i], &te[REF], sizeof te[0])) {
            fails++;
            if (shown++ < 12) printf("  DIFFERENT: TextLength/TextExtent/TextFit, %s\n", way_name[i]);
        }
    for (i = 0; i < WAYS; i++) SetSoftStyle(&ways[i].rp[k], 0, 0xFF);
}

/* ---- the core's calls: through graphics.library (the patches) ---------------- */

static UBYTE *core_mask;               /* BltPattern's mask, in Chip RAM: 2048 bytes */
static UBYTE *core_arr[WAYS];          /* the pixel arrays, one a way */
#define CORE_ARR ((W + 16) * H)
static struct RastPort core_tmp[WAYS]; /* WritePixelArray8's temporary RastPorts */
static struct BitMap *core_tmpbm[WAYS];
/* The area calls: a table and a TmpRas a way, and the shape, the same on
 * every way */
#define AREA_PTS 12
static struct AreaInfo core_ai[WAYS];
static WORD core_av[WAYS][(5 * (AREA_PTS + 6) + 1) / 2];
static struct TmpRas core_tr[WAYS];
static PLANEPTR core_trbuf[WAYS];
static WORD area_xy[2 * AREA_PTS], area_e[4];
static ULONG area_moves;
static int area_n, area_ell;

/* A window's RastPort on the Chip RAM ways' first bitmap: a smart refresh
 * layer with a simple refresh one in front of it (no backfill), so the
 * core's calls are checked cut to a layer's ClipRects too. Not on the Fast RAM way. */
struct Library *LayersBase;
static struct Layer_Info *lay_li[FAST];
static struct Layer *lay_back[FAST], *lay_front[FAST];
static struct RastPort lay_rp[FAST];
static int use_layer;

static void free_layers(void)
{
    int w;
    for (w = 0; w < FAST; w++) {
        if (lay_front[w]) DeleteLayer(0, lay_front[w]);
        if (lay_back[w]) DeleteLayer(0, lay_back[w]);
        if (lay_li[w]) DisposeLayerInfo(lay_li[w]);
        lay_front[w] = lay_back[w] = NULL;
        lay_li[w] = NULL;
    }
}

/* Whether way w's window keeps the same pixels as graphics.library's in
 * its covered part (a smart refresh layer's own bitmap), within the part's
 * bounds: the bits around them are never shown. The ClipRects pair up by
 * their rectangles (graphics.library may reorder a list). fix: way w takes
 * graphics.library's part. 1 for the Fast RAM way and with no window. */
static int parts_same(int w, int fix)
{
    struct ClipRect *a, *b;
    if (w >= FAST || !lay_back[REF] || !lay_back[w]) return 1;
    for (a = lay_back[REF]->ClipRect; a; a = a->Next) {
        LONG x0, x1, x, y, p;
        if (!a->obscured || !a->BitMap) continue;
        for (b = lay_back[w]->ClipRect; b; b = b->Next)
            if (b->bounds.MinX == a->bounds.MinX && b->bounds.MinY == a->bounds.MinY &&
                b->bounds.MaxX == a->bounds.MaxX && b->bounds.MaxY == a->bounds.MaxY) break;
        if (!b || !b->BitMap) { if (fix) continue; return 0; }
        if (fix) {
            for (p = 0; p < depth && p < a->BitMap->Depth; p++)
                CopyMem(a->BitMap->Planes[p], b->BitMap->Planes[p], (ULONG)a->BitMap->BytesPerRow * a->BitMap->Rows);
            continue;
        }
        x0 = a->bounds.MinX & 15; x1 = x0 + a->bounds.MaxX - a->bounds.MinX;
        for (p = 0; p < depth && p < a->BitMap->Depth; p++)
            for (y = 0; y <= a->bounds.MaxY - a->bounds.MinY && y < a->BitMap->Rows; y++) {
                const UBYTE *ra = at(a->BitMap, p, y), *rb = at(b->BitMap, p, y);
                for (x = x0; x <= x1; x++)
                    if ((ra[x >> 3] ^ rb[x >> 3]) & (0x80 >> (x & 7))) return 0;
            }
    }
    return 1;
}

static int make_layers(void)
{
    int w;
    if (!LayersBase) return 0;
    for (w = 0; w < FAST; w++) {
        if (!(lay_li[w] = NewLayerInfo())) return 0;
        lay_back[w] = CreateUpfrontHookLayer(lay_li[w], ways[w].bm[0], 13, 7, 150, 95, LAYERSMART, LAYERS_NOBACKFILL, NULL);
        lay_front[w] = lay_back[w] ? CreateUpfrontHookLayer(lay_li[w], ways[w].bm[0], 60, 30, 110, 70, LAYERSIMPLE,
                                                            LAYERS_NOBACKFILL, NULL) : NULL;
        if (!lay_front[w]) return 0;
        InitRastPort(&lay_rp[w]);
        lay_rp[w].Layer = lay_back[w];
        lay_rp[w].BitMap = ways[w].bm[0];
    }
    return 1;
}

/* One call of the core's on way w's RastPort k: c says which, a[] its
 * arguments. The result, or a sum of the array read. */
static LONG core_call(int w, int k, int c, const LONG *a, const WORD *poly)
{
    struct RastPort *rp = use_layer && w < FAST ? &lay_rp[w] : &ways[w].rp[k];
    LONG r = 0, i;
    switch (c) {
    case 0: Move(rp, a[0], a[1]); Draw(rp, a[2], a[3]); break;
    case 1: Move(rp, a[0], a[1]); PolyDraw(rp, a[2], (WORD *)poly); break;
    case 2: r = WritePixel(rp, a[0], a[1]); break;
    case 3: r = (LONG)ReadPixel(rp, a[0], a[1]); break;
    case 4: SetRast(rp, (UBYTE)a[0]); break;
    case 5: BltPattern(rp, a[4] ? core_mask : NULL, a[0], a[1], a[2], a[3], (ULONG)a[4]); break;
    case 6:
        for (i = 0; i < CORE_ARR; i++) core_arr[w][i] = (UBYTE)(i * 7 + a[4]);
        r = WritePixelArray8(rp, a[0], a[1], a[2], a[3], core_arr[w], &core_tmp[w]);
        break;
    case 7:
        memset(core_arr[w], 0xA5, CORE_ARR);
        r = ReadPixelArray8(rp, a[0], a[1], a[2], a[3], core_arr[w], &core_tmp[w]);
        {
            /* the pixels asked for: each row's padding to 16 is the temporary
             * RastPort's row on OS 3.2, whatever an earlier call left there */
            LONG x, y, bpr = (a[2] - a[0] + 16) & ~15;
            for (y = 0; y <= a[3] - a[1]; y++)
                for (x = 0; x <= a[2] - a[0]; x++) r = r * 31 + core_arr[w][y * bpr + x];
        }
        break;
    case 9:
        /* from the way's other bitmap: a[0..3] source x, y, width, height,
         * a[4] the minterm, a[5] a[6] where it lands */
        BltBitMapRastPort(ways[w].bm[1 - k], a[0], a[1], rp, a[5], a[6], a[2], a[3], (ULONG)a[4]);
        break;
    case 10:
        /* within the RastPort (a[4] & 0x100) or from the way's other one,
         * as case 9 */
        ClipBlit(a[4] & 0x100 ? rp : &ways[w].rp[1 - k], a[0], a[1], rp, a[5], a[6], a[2], a[3], (ULONG)a[4] & 0xF0);
        break;
    case 11: ScrollRaster(rp, a[5], a[6], a[0], a[1], a[2], a[3]); break;
    case 12:
        /* the shape's polygons (a vertex in area_moves starts one) and
         * maybe an ellipse; a[4] & 1: an outline in pen a[4] >> 8. The
         * result: whether a call to the table failed, then AreaEnd's */
        InitArea(&core_ai[w], core_av[w], AREA_PTS + 6);
        InitTmpRas(&core_tr[w], core_trbuf[w], RASSIZE(W, H));
        rp->AreaInfo = &core_ai[w]; rp->TmpRas = &core_tr[w];
        if (a[4] & 1) SetOutlinePen(rp, (ULONG)a[4] >> 8);
        for (i = 0; i < area_n; i++)
            r |= area_moves >> i & 1 ? AreaMove(rp, area_xy[2 * i], area_xy[2 * i + 1])
                                     : AreaDraw(rp, area_xy[2 * i], area_xy[2 * i + 1]);
        if (area_ell) r |= AreaEllipse(rp, area_e[0], area_e[1], area_e[2], area_e[3]);
        r = (r != 0) * 2 + AreaEnd(rp);
        rp->AreaInfo = NULL; rp->TmpRas = NULL;
        rp->Flags &= ~AREAOUTLINE;
        break;
    case 13: DrawEllipse(rp, a[0], a[1], a[2], a[3]); break;
    case 14: EraseRect(rp, a[0], a[1], a[2], a[3]); break;
    default:
        for (i = 0; i < CORE_ARR; i++) core_arr[w][i] = (UBYTE)(i * 5 + a[4]);
        WriteChunkyPixels(rp, a[0], a[1], a[2], a[3], core_arr[w], a[4] % 9 + a[2] - a[0] + 1);
    }
    if (c < 2) r = ((LONG)rp->cp_x << 16 | (UWORD)rp->cp_y) ^ ((LONG)rp->linpatcnt << 8);
    return r;
}

static void one_core(void)
{
    static const char *const name[] = { "Draw", "PolyDraw", "WritePixel", "ReadPixel", "SetRast", "BltPattern",
                                        "WritePixelArray8", "ReadPixelArray8", "WriteChunkyPixels", "BltBitMapRastPort",
                                        "ClipBlit", "ScrollRaster", "AreaEnd", "DrawEllipse", "EraseRect" };
    LONG a[7], r0, r1, r2 = 0, x0 = rnd_in(0, W - 1), y0 = rnd_in(0, H - 1), x1 = rnd_in(x0, W - 1), y1 = rnd_in(y0, H - 1);
    ULONG fg = rnd() & 255, bg = rnd() & 255, mode = rnd() & 7, mask = rnd() & 3 ? 0xFF : rnd() & 255;
    UWORD lp = rnd() & 1 ? 0xFFFF : (UWORD)rnd();
    WORD poly[12];
    int c = (int)(rnd() % 15), k = rnd() & 1, w, ptrn = (rnd() & 3) == 0, i;
    if (c == 4 && (rnd() & 3)) c = 2;               /* SetRast seldom */
    a[0] = x0; a[1] = y0; a[2] = x1; a[3] = y1; a[4] = 0;
    if (c == 0) { a[2] = rnd_in(0, W - 1); a[3] = rnd_in(0, H - 1); }
    if (c == 1) { a[2] = rnd_in(2, 6); for (i = 0; i < 2 * a[2]; i++) poly[i] = (WORD)(i & 1 ? rnd_in(0, H - 1) : rnd_in(0, W - 1)); }
    if (c == 4) a[0] = rnd() & 255;
    if (c == 5 && (rnd() & 1)) {                    /* a mask exactly as wide as the rectangle */
        a[4] = 2 * (((x1 - x0) >> 4) + 1);
        if (a[4] * (y1 - y0 + 1) > 2048) a[4] = 0;
        for (i = 0; i < 2048; i++) core_mask[i] = (UBYTE)rnd();
    }
    if (c >= 6 && c <= 8) { a[4] = rnd() & 255; if (c != 7) mode = JAM1; }
    use_layer = k == 0 && lay_back[0] && c != 5 && (rnd() % 3) == 0;  /* BltPattern's mask: not in a layer */
    if (c == 9 || c == 10) {
        /* inside the source always, and inside a bare bitmap: graphics.library
         * clips neither (past an edge it writes memory not the bitmap's); in
         * the layer it may hang over the layer's edges */
        a[2] = x1 - x0 + 1; a[3] = y1 - y0 + 1;
        a[4] = rnd() % 3 ? 0xC0 : rnd() & 0xF0;
        if (c == 10 && (rnd() & 1)) a[4] |= 0x100;
        a[5] = use_layer ? rnd_in(-8, 150 - 13 + 8) : rnd_in(0, W - 1);
        a[6] = use_layer ? rnd_in(-8, 95 - 7 + 8) : rnd_in(0, H - 1);
        if (!use_layer) {
            if (a[5] + a[2] > W) a[2] = W - a[5];
            if (a[6] + a[3] > H) a[3] = H - a[6];
        }
    }
    if (c == 11) {
        /* the rectangle inside a bare bitmap, and hanging over the layer's
         * edges in the layer */
        if (use_layer) { a[2] = rnd_in(x0, x0 + 150); a[3] = rnd_in(y0, y0 + 95); }
        a[5] = rnd_in(-12, 12); a[6] = rnd_in(-8, 8);
    }
    if (c == 12) {
        /* inside a bare bitmap (graphics.library clips nothing there); in
         * the layer hanging over its edges. The first polygon has two
         * points or more: on a table with no draw OS 3.2 answers junk */
        LONG lx = use_layer ? -8 : 0, ly = use_layer ? -8 : 0, hx = use_layer ? 145 : W - 1, hy = use_layer ? 95 : H - 1;
        int np = 1 + (int)(rnd() & 1), j, v;
        area_n = 0; area_moves = 0;
        for (j = 0; j < np; j++) {
            int n = (j ? 1 : 2) + (int)(rnd() % 5);
            LONG cx = rnd_in(lx, hx), cy = rnd_in(ly, hy);
            for (v = 0; v < n && area_n < AREA_PTS; v++, area_n++) {
                LONG px = cx + rnd_in(-30, 30), py = cy + rnd_in(-20, 20);
                area_xy[2 * area_n] = (WORD)(px < lx ? lx : px > hx ? hx : px);
                area_xy[2 * area_n + 1] = (WORD)(py < ly ? ly : py > hy ? hy : py);
                if (!v) area_moves |= 1UL << area_n;
            }
        }
        if ((area_ell = (rnd() % 3) == 0) != 0) {
            area_e[0] = (WORD)rnd_in(lx, hx); area_e[1] = (WORD)rnd_in(ly, hy);
            area_e[2] = (WORD)(rnd() % 30); area_e[3] = (WORD)(rnd() % 20);
            if (!use_layer) {
                if (area_e[2] > area_e[0]) area_e[2] = area_e[0];
                if (area_e[2] > W - 1 - area_e[0]) area_e[2] = W - 1 - area_e[0];
                if (area_e[3] > area_e[1]) area_e[3] = area_e[1];
                if (area_e[3] > H - 1 - area_e[1]) area_e[3] = H - 1 - area_e[1];
            }
        }
        a[0] = area_xy[0]; a[1] = area_xy[1]; a[2] = area_n; a[3] = area_ell;
        a[4] = (rnd() % 3 == 0) | (LONG)(rnd() & 255) << 8;
    }
    if (c == 13) {
        /* the centre and the radii; inside a bare bitmap */
        a[2] = (LONG)(rnd() % 40); a[3] = (LONG)(rnd() % 30);
        if (!use_layer) {
            if (a[2] > x0) a[2] = x0;
            if (a[2] > W - 1 - x0) a[2] = W - 1 - x0;
            if (a[3] > y0) a[3] = y0;
            if (a[3] > H - 1 - y0) a[3] = H - 1 - y0;
        }
    }
    for (w = 0; w < WAYS; w++) {
        struct RastPort *rp = use_layer && w < FAST ? &lay_rp[w] : &ways[w].rp[k];
        set_rp(rp, fg, bg, mode, mask, ptrn);
        SetDrPt(rp, lp);
    }
    sprintf(args, "%ld,%ld,%ld,%ld fg %lu bg %lu mode %lu mask %02lx%s ptrn %04x arg %ld%s", (long)a[0], (long)a[1], (long)a[2], (long)a[3],
            (unsigned long)fg, (unsigned long)bg, (unsigned long)mode, (unsigned long)mask, ptrn ? " pattern" : "", lp, (long)a[4],
            use_layer ? " in a layer" : "");
    if (c == 9 || c == 10) sprintf(args + strlen(args), " to %ld,%ld", (long)a[5], (long)a[6]);
    if (c == 11) sprintf(args + strlen(args), " by %ld,%ld", (long)a[5], (long)a[6]);
    kind = 5;
    on(0); r0 = core_call(REF, k, c, a, poly);
    on(1);
    if (patched) { r1 = core_call(LVO, k, c, a, poly); r2 = core_call(PAT, k, c, a, poly); }
    else { sync_way(LVO); r1 = r0; }
    /* a scroll of the rectangle's size or more is graphics.library's, whose
     * blitter can't reach the Fast RAM way (as in one_scroll) */
    if (have_fast && patched && !use_layer &&
        !(c == 11 && ((a[5] < 0 ? -a[5] : a[5]) > a[2] - a[0] || (a[6] < 0 ? -a[6] : a[6]) > a[3] - a[1])) &&
        !(c == 13 && ((mode & (COMPLEMENT | INVERSVID)) || lp != 0xFFFF)))
        fast_r = core_call(FAST, k, c, a, poly);
    else sync_fast(r0);
    compare(name[c], r0, r1, r2);
    use_layer = 0;
}

static int alloc_ways(int d)
{
    int w, k;
    struct TextFont *font = ((struct GfxBase *)GfxBase)->DefaultFont;
    for (w = 0; w < FAST; w++)
        for (k = 0; k < 2; k++) {
            if (!(ways[w].bm[k] = AllocBitMap(W, H, d, BMF_CLEAR, NULL))) return 0;
            if (!(GetBitMapAttr(ways[w].bm[k], BMA_FLAGS) & BMF_STANDARD)) return -1;
            InitRastPort(&ways[w].rp[k]);
            ways[w].rp[k].BitMap = ways[w].bm[k];
            if (font) SetFont(&ways[w].rp[k], font);
        }
    for (w = 0; w < WAYS; w++) {
        if (!(core_tmpbm[w] = AllocBitMap(((W + 15) & ~15) + 16, 1, d, BMF_CLEAR, NULL))) return 0;
        if (!core_trbuf[w] && !(core_trbuf[w] = AllocRaster(W, H))) return 0;
        InitRastPort(&core_tmp[w]);
        core_tmp[w].BitMap = core_tmpbm[w];
    }
    /* the Fast RAM way: planes of its own, if there is Fast RAM */
    have_fast = 0;
    if (AvailMem(MEMF_FAST) > 4UL << 20) {
        have_fast = 1;
        for (k = 0; k < 2; k++) {
            int p;
            InitBitMap(&fast_bm[k], d, W, H);
            for (p = 0; p < d; p++)
                if (!(fast_bm[k].Planes[p] = AllocVec(RASSIZE(W, H), MEMF_FAST | MEMF_CLEAR))) have_fast = 0;
            ways[FAST].bm[k] = &fast_bm[k];
            InitRastPort(&ways[FAST].rp[k]);
            ways[FAST].rp[k].BitMap = &fast_bm[k];
            if (font) SetFont(&ways[FAST].rp[k], font);
        }
    }
    if (!make_layers()) free_layers();
    return 1;
}

static void free_ways(void)
{
    int w, k;
    WaitBlit();
    free_layers();
    for (w = 0; w < FAST; w++)
        for (k = 0; k < 2; k++) { if (ways[w].bm[k]) FreeBitMap(ways[w].bm[k]); ways[w].bm[k] = NULL; }
    for (w = 0; w < WAYS; w++) { if (core_tmpbm[w]) FreeBitMap(core_tmpbm[w]); core_tmpbm[w] = NULL; }
    for (w = 0; w < WAYS; w++) { if (core_trbuf[w]) FreeRaster(core_trbuf[w], W, H); core_trbuf[w] = NULL; }
    for (k = 0; k < 2; k++) {
        int p;
        for (p = 0; p < 8; p++) { if (ways[FAST].bm[k] && fast_bm[k].Planes[p]) FreeVec(fast_bm[k].Planes[p]); fast_bm[k].Planes[p] = NULL; }
        ways[FAST].bm[k] = NULL;
    }
}

static int check(int n_per_depth)
{
    int di, i, ok;
    fails = calls = shown = 0;
    for (di = 0; di < N_DEPTHS; di++) {
        depth = depths[di];
        ok = alloc_ways(depth);
        if (ok > 0 && verbose)
            printf("  %d planes: bitmaps in %s RAM\n", depth, (TypeOfMem(ways[REF].bm[0]->Planes[0]) & MEMF_CHIP) ? "Chip" : "Fast");
        if (ok <= 0) {
            printf("  %d planes: %s\n", depth, ok < 0 ? "AllocBitMap gave no standard bitmap; skipped" : "no Chip RAM");
            free_ways();
            if (ok == 0) return -1;
            continue;
        }
        for (i = 0; i < n_per_depth; i++) {
            switch (rnd() % 11) {
            case 8: case 9: case 10: say("core"); one_core(); break;
            case 0: case 1: case 2: say("RectFill"); one_rectfill(); break;
            case 3: case 4: say("BltBitMap"); one_bltbitmap(); break;
            case 5: say("ScrollRaster"); one_scroll(); break;
            case 6: say("BltTemplate"); one_template(); break;
            default: say("Text"); one_text(); break;
            }
        }
        free_ways();
    }
    return fails;
}

/* ---- timing --------------------------------------------------------------------- */

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

static void bench(void)
{
    struct BitMap *a = AllocBitMap(640, 480, 8, BMF_CLEAR, NULL), *b = AllocBitMap(640, 480, 8, BMF_CLEAR, NULL);
    struct RastPort rp;
    double t[2][4], m;
    int s, i, k, round, n = 20;
    if (!a || !b || !(GetBitMapAttr(a, BMA_FLAGS) & BMF_STANDARD)) {
        printf("timing: no 640 x 480 x 8 standard bitmap\n");
        goto out;
    }
    InitRastPort(&rp);
    rp.BitMap = a;
    for (s = 0; s < 2; s++) for (k = 0; k < 4; k++) t[s][k] = 1e9;
    /* five rounds, off and on in turn, and the fastest of each: the host's
     * other work and the JIT's first translations stay out of it. The copies
     * come first in a round: after the leaves' fills the runtime's next blits
     * are slower, whichever way they go. */
    for (round = 0; round < 10; round++) {
        double t0;
        s = round & 1;
        on(s);
        t0 = now_ms();
        for (i = 0; i < n; i++) { if (patched) BltBitMap(a, 0, 0, b, 0, 0, 640, 480, 0xC0, 0xFF, NULL); else OGFX_BltBitMap(a, 0, 0, b, 0, 0, 640, 480, 0xC0, 0xFF, NULL); }
        WaitBlit();
        m = (now_ms() - t0) / n; if (m < t[s][1]) t[s][1] = m;
        t0 = now_ms();
        for (i = 0; i < 25 * n; i++) { if (patched) BltBitMap(a, (i * 37) % 576, (i * 53) % 416, b, (i * 61) % 576, (i * 29) % 416, 64, 64, 0xC0, 0xFF, NULL);
                                      else OGFX_BltBitMap(a, (i * 37) % 576, (i * 53) % 416, b, (i * 61) % 576, (i * 29) % 416, 64, 64, 0xC0, 0xFF, NULL); }
        WaitBlit();
        m = (now_ms() - t0) / (25 * n); if (m < t[s][3]) t[s][3] = m;
        t0 = now_ms();
        for (i = 0; i < n; i++) { SetAPen(&rp, i & 255); if (patched) RectFill(&rp, 0, 0, 639, 479); else OGFX_RectFill(&rp, 0, 0, 639, 479); }
        WaitBlit();
        m = (now_ms() - t0) / n; if (m < t[s][0]) t[s][0] = m;
        t0 = now_ms();
        for (i = 0; i < n; i++) { if (patched) ScrollRaster(&rp, 0, 1, 0, 0, 639, 479); else OGFX_ScrollRaster(&rp, 0, 1, 0, 0, 639, 479); }
        WaitBlit();
        m = (now_ms() - t0) / n; if (m < t[s][2]) t[s][2] = m;
    }
    printf("timing, 640 x 480 x 8 planes in %s RAM, ms a call, fastest of five (%s):\n",
           (TypeOfMem(a->Planes[0]) & MEMF_CHIP) ? "Chip" : "Fast", patched ? "through graphics.library" : "through OpenGfx's LVOs");
    printf("  RectFill      off %7.3f  on %7.3f  (%.1fx)\n", t[0][0], t[1][0], t[1][0] > 0 ? t[0][0] / t[1][0] : 0.0);
    printf("  BltBitMap     off %7.3f  on %7.3f  (%.1fx)\n", t[0][1], t[1][1], t[1][1] > 0 ? t[0][1] / t[1][1] : 0.0);
    printf("  ScrollRaster  off %7.3f  on %7.3f  (%.1fx)\n", t[0][2], t[1][2], t[1][2] > 0 ? t[0][2] / t[1][2] : 0.0);
    printf("  BltBitMap 64  off %7.3f  on %7.3f  (%.1fx)  (64 x 64 copies)\n", t[0][3], t[1][3], t[1][3] > 0 ? t[0][3] / t[1][3] : 0.0);
out:
    WaitBlit();
    if (a) FreeBitMap(a);
    if (b) FreeBitMap(b);
}

/* ---- the look hook (0.7) --------------------------------------------------- */

static struct RastPort look_rp;
static int look_claim, look_rects, look_texts;
static LONG look_x0, look_n;
static const char look_owner[] = "OpenGfxCheck";

static LONG look_rectfill(APTR userdata, struct OGFXRectFillRequest *r)
{
    if (r->rp != &look_rp || userdata != (APTR)look_owner) return 0;
    look_rects++;
    look_x0 = r->x0;
    return look_claim;
}

static LONG look_text(APTR userdata, struct OGFXTextRequest *r)
{
    if (r->rp != &look_rp || userdata != (APTR)look_owner) return 0;
    look_texts++;
    look_n = (LONG)r->length;
    r->result = 1234;
    return look_claim;
}

static int look_lit(void)
{
    struct BitMap *bm = look_rp.BitMap;
    int y, x;
    WaitBlit();
    for (y = 0; y < 16; y++)
        for (x = 0; x < bm->BytesPerRow; x++)
            if (at(bm, 0, y)[x]) return 1;
    return 0;
}

static void look_clear(void)
{
    struct BitMap *bm = look_rp.BitMap;
    int y;
    WaitBlit();
    for (y = 0; y < 16; y++) memset(at(bm, 0, y), 0, bm->BytesPerRow);
}

static int look_fail(const char *what) { printf("  look: %s\n", what); return 1; }

/* 0 when the look hook behaves as gfx.h says, else the number of faults. */
static int look_check(void)
{
    struct OGFXLookV1 look, other;
    struct BitMap *bm;
    int f = 0, way;
    LONG r;
    if (OGFX_Status() & OGFX_STATUS_LOOK) {
        printf("look: skipped (another look is in: OpenLook)\n");
        return 0;
    }
    if (!(bm = AllocBitMap(64, 16, 1, BMF_CLEAR, NULL))) { printf("look: no memory\n"); return 1; }
    InitRastPort(&look_rp);
    look_rp.BitMap = bm;
    SetAPen(&look_rp, 1);
    SetDrMd(&look_rp, JAM2);
    memset(&look, 0, sizeof look);
    look.size = sizeof look;
    look.abi = OGFX_LOOK_ABI_V1;
    look.owner = (APTR)look_owner;
    look.userdata = (APTR)look_owner;
    look.rectfill = look_rectfill;
    look.text = look_text;
    other = look;
    other.owner = (APTR)&other;
    if (!OGFX_RegisterLook(&look)) { FreeBitMap(bm); return look_fail("RegisterLook refused a free slot"); }
    if (OGFX_RegisterLook(&other)) { f += look_fail("a second look went in"); (void)OGFX_UnregisterLook(&other); }
    if (!(OGFX_Status() & OGFX_STATUS_LOOK)) f += look_fail("Status has no OGFX_STATUS_LOOK");
    for (way = 0; way < (patched ? 2 : 1); way++) {
        const char *how = way ? "through the patches" : "by the LVOs";
        look_rects = look_texts = 0;
        look_claim = 1;
        look_clear();
        if (way) RectFill(&look_rp, 3, 2, 20, 9); else OGFX_RectFill(&look_rp, 0x12340003, 2, 20, 9);
        if (look_rects != 1 || look_x0 != 3) { printf("  look %s: RectFill asked %d times, x0 %ld\n", how, look_rects, (long)look_x0); f++; }
        if (look_lit()) { printf("  look %s: a RectFill the look drew was drawn again\n", how); f++; }
        Move(&look_rp, 2, 10);
        r = way ? (Text(&look_rp, (STRPTR)"Look", 4), 1234) : OGFX_Text(&look_rp, (STRPTR)"Look", 4);
        if (look_texts != 1 || look_n != 4 || r != 1234) { printf("  look %s: Text asked %d times, length %ld, result %ld\n", how, look_texts, (long)look_n, (long)r); f++; }
        if (look_lit()) { printf("  look %s: a Text the look drew was drawn again\n", how); f++; }
        look_claim = 0;
        if (way) RectFill(&look_rp, 3, 2, 20, 9); else OGFX_RectFill(&look_rp, 3, 2, 20, 9);
        if (look_rects != 2 || !look_lit()) { printf("  look %s: a RectFill the look declined: asked %d times, %s\n", how, look_rects, look_lit() ? "drawn" : "NOT drawn"); f++; }
        look_clear();
        Move(&look_rp, 2, 10);
        if (way) Text(&look_rp, (STRPTR)"Look", 4); else (void)OGFX_Text(&look_rp, (STRPTR)"Look", 4);
        if (look_texts != 2 || !look_lit()) { printf("  look %s: a Text the look declined: asked %d times, %s\n", how, look_texts, look_lit() ? "drawn" : "NOT drawn"); f++; }
    }
    if (OGFX_UnregisterLook((APTR)&other)) f += look_fail("UnregisterLook took the look out for another owner");
    if (!OGFX_UnregisterLook((APTR)look_owner)) f += look_fail("UnregisterLook did not take the look out");
    if (OGFX_Status() & (OGFX_STATUS_LOOK | OGFX_STATUS_LOOK_BUSY)) f += look_fail("Status still shows a look");
    look_rects = 0;
    look_claim = 1;
    look_clear();
    OGFX_RectFill(&look_rp, 3, 2, 20, 9);
    if (look_rects || !look_lit()) f += look_fail("a look taken out was still asked");
    FreeBitMap(bm);
    printf("look: %s%s\n", f ? "WRONG" : "asked first, ends the call when it draws, taken out cleanly",
           f ? "" : patched ? " (LVOs and patches)" : " (LVOs; no patches in)");
    return f;
}

int main(int argc, char **argv)
{
    int i, install = 0, quick = 0, nocheck = 0, set = -1, rc = 0;
    LONG was;
    ULONG st, v;
    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "INSTALL")) install = 1;
        else if (!strcasecmp(argv[i], "ON")) set = 1;
        else if (!strcasecmp(argv[i], "OFF")) set = 0;
        else if (!strcasecmp(argv[i], "QUICK")) quick = 1;
        else if (!strcasecmp(argv[i], "NOCHECK")) nocheck = 1;
        else if (!strcasecmp(argv[i], "VERBOSE")) verbose = 1;
        else { printf("OpenGfxCheck [INSTALL] [ON|OFF] [QUICK] [NOCHECK] [VERBOSE]\n"); return 5; }
    }
    if (!(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) {
        printf("OpenGfxCheck: opengpu.library is not installed\n");
        return 20;
    }
    if (OpenGPUBase->lib_Version == 0 && OpenGPUBase->lib_Revision < OPENGPU_OGFX_REVISION) {
        printf("OpenGfxCheck: opengpu.library %d.%d has no OpenGfx (0.%d or later has)\n",
               OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision, OPENGPU_OGFX_REVISION);
        CloseLibrary(OpenGPUBase);
        return 20;
    }
    LayersBase = OpenLibrary((CONST_STRPTR)"layers.library", 39);
    v = OGFX_Version();
    if (install && !OGFX_InstallPatches()) { printf("OpenGfxCheck: the patches would not go in\n"); rc = 10; }
    if (set >= 0) printf("OpenGfx's own drawing: %s (was %s)\n", set ? "on" : "off", OGFX_SetEnabled(set) ? "on" : "off");
    st = OGFX_Status();
    patched = (st & OGFX_STATUS_PATCHED) != 0;
    printf("opengpu.library %d.%d, OpenGfx %lu.%lu: patches %s, own drawing %s, provider %s%s\n",
           OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision, (unsigned long)(v >> 16), (unsigned long)(v & 0xFFFF),
           patched ? "in" : "not in", st & OGFX_STATUS_ENABLED ? "on" : "off", st & OGFX_STATUS_PROVIDER ? "registered" : "none",
           st & OGFX_STATUS_AMIGACHROME ? ", AmigaChrome (the leaves take Chip RAM)" : "");
    if ((v >> 16) != OGFX_INTERFACE_VERSION) { printf("  OGFX_Version is WRONG\n"); rc = 10; }

    if (!nocheck && !(tmpl = AllocVec(4 * 16, MEMF_CHIP))) { printf("OpenGfxCheck: no Chip RAM\n"); nocheck = 1; rc = 20; }
    if (!nocheck) {
        core_mask = AllocVec(2048, MEMF_CHIP | MEMF_CLEAR);
        for (i = 0; i < WAYS; i++) core_arr[i] = AllocVec(CORE_ARR, MEMF_ANY);
        for (i = 0; i < WAYS; i++) if (!core_arr[i]) nocheck = 1;
        if (!core_mask || nocheck) {
            printf("OpenGfxCheck: no memory\n");
            FreeVec(core_mask);
            for (i = 0; i < WAYS; i++) FreeVec(core_arr[i]);
            FreeVec(tmpl);
            nocheck = 1; rc = 20;
        }
    }
    if (!nocheck) {
        int f;
        for (i = 0; i < 4 * 16; i++) tmpl[i] = (UBYTE)rnd();
        was = OGFX_SetEnabled(1);
        f = check(quick ? 60 : 400);
        if (f < 0) { printf("check: no Chip RAM\n"); rc = 20; }
        else {
            printf("check: %d calls on 1 to 8 planes, %s ways%s%s: %s\n", calls, patched ? "three" : "two", have_fast ? " and in Fast RAM" : "",
                   patched ? "" : " (no patches in)",
                   f ? "DIFFERENT" : "all the same");
            if (p96_counts)
                printf("  (%d Fast RAM BltBitMap answers were the planes drawn, where graphics.library here counted every shared plane, as Picasso96's BltBitMap does)\n",
                       p96_counts);
            if (f) {
                printf("  %d differences: RectFill %d, BltBitMap %d, ScrollRaster %d, BltTemplate %d, text %d, core calls %d\n", f,
                       fails_of[0], fails_of[1], fails_of[2], fails_of[3], fails_of[4], fails_of[5]);
                rc = 10;
            }
        }
        if (!quick && !OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) {
            TimerBase = treq.tr_node.io_Device;
            bench();
            CloseDevice(&treq.tr_node);
        }
        (void)OGFX_SetEnabled(was);
        FreeVec(tmpl);
        FreeVec(core_mask);
        for (i = 0; i < WAYS; i++) FreeVec(core_arr[i]);
        if ((OpenGPUBase->lib_Version > 0 || OpenGPUBase->lib_Revision >= OPENGPU_OGFX_LOOK_REVISION) && look_check())
            rc = 10;
    }
    if (LayersBase) CloseLibrary(LayersBase);
    CloseLibrary(OpenGPUBase);
    return rc;
}
