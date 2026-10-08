/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
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
 * text calls, on 1 to 8 planes. On AmigaChrome (OGFX_STATUS_AMIGACHROME)
 * OpenGfx's leaves take Chip RAM, so the leaves are what is checked; on a
 * real Amiga they leave Chip RAM to the blitter.
 *
 *   OpenGfxCheck [INSTALL] [ON|OFF] [QUICK] [NOCHECK] [VERBOSE]
 * INSTALL puts the patches in first; ON or OFF switches OpenGfx's own
 * drawing and leaves it so. Without NOCHECK the check runs (OpenGfx's
 * switch is put back afterwards); without QUICK it also times full-screen
 * fills, copies and scrolls with OpenGfx on and off. VERBOSE names each
 * call before it is made.
 * Returns 0 when everything matches, 10 when something doesn't, 20 when
 * opengpu.library has no OpenGfx. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
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
static char args[96];                  /* the call's arguments, for a report */
static int fails_of[5];                /* by call: RectFill, BltBitMap, ScrollRaster, BltTemplate, text */
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
}

static int same(int w)
{
    int k, p, y;
    for (k = 0; k < 2; k++)
        for (p = 0; p < depth; p++)
            for (y = 0; y < H; y++)
                if (memcmp(at(ways[REF].bm[k], p, y), at(ways[w].bm[k], p, y), W / 8)) return 0;
    return 1;
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
        }
    }
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
    if (have_fast && !ptrn && (mode == JAM1 || mode == JAM2 || mode == COMPLEMENT)) { OGFX_RectFill(&ways[FAST].rp[k], x0, y0, x1, y1); fast_r = 0; }
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
    LONG n = rnd_in(1, 20), at = rnd_in(0, (LONG)sizeof words - 1 - n), x = rnd_in(0, W - 8 * n > 0 ? W - 8 * n : 0), y = rnd_in(8, H - 2);
    struct TextExtent te[WAYS], ce;
    ULONG fit[WAYS], len[WAYS], drawn[WAYS];
    ULONG fg = rnd() & 255, bg = rnd() & 255;
    int k = rnd() & 1, i;
    for (i = 0; i < WAYS; i++) { set_rp(&ways[i].rp[k], fg, bg, JAM2, 0xFF, 0); Move(&ways[i].rp[k], x, y); }
    memset(te, 0, sizeof te); memset(&ce, 0, sizeof ce);
    sprintf(args, "%ld chars at %ld,%ld", (long)n, (long)x, (long)y);
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
    sync_fast((LONG)drawn[REF]);
    compare("Text", (LONG)drawn[REF], (LONG)drawn[LVO], (LONG)drawn[PAT]);
    calls += 3;
    for (i = LVO; i < FAST; i++)
        if (len[i] != len[REF] || fit[i] != fit[REF] || memcmp(&te[i], &te[REF], sizeof te[0])) {
            fails++;
            if (shown++ < 12) printf("  DIFFERENT: TextLength/TextExtent/TextFit, %s\n", way_name[i]);
        }
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
    return 1;
}

static void free_ways(void)
{
    int w, k;
    WaitBlit();
    for (w = 0; w < FAST; w++)
        for (k = 0; k < 2; k++) { if (ways[w].bm[k]) FreeBitMap(ways[w].bm[k]); ways[w].bm[k] = NULL; }
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
            switch (rnd() % 8) {
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
                printf("  %d differences: RectFill %d, BltBitMap %d, ScrollRaster %d, BltTemplate %d, text %d\n", f,
                       fails_of[0], fails_of[1], fails_of[2], fails_of[3], fails_of[4]);
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
    }
    CloseLibrary(OpenGPUBase);
    return rc;
}
