/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTG's own screens (DESIGN.md, section 4: Screens, Bitmaps, Drawing).
 *
 * A screen opened on an OpenRTG ModeID gets a chunky bitmap in its board's
 * video RAM, and the board shows it: no Picasso96. graphics.library knows
 * only planar bitmaps, so the calls that draw (the busiest ones phase 0
 * measured) learn chunky bitmaps here: on an OpenRTG bitmap they draw with
 * the CPU, through the layer's clip rectangles as graphics does; on any
 * other bitmap they go to graphics untouched.
 *
 * Step 1 (5 October 2026): 8-bit (CLUT) screens. 16 and 32-bit screens,
 * the pointer and the board's blitter come next; OpenGPU after that.
 *
 * An OpenRTG bitmap is a struct BitMap with Depth 8, BytesPerRow the row's
 * bytes, every plane pointer at the chunky pixels, and pad and a head
 * before the pixels marking it, so a program that reads plane 0 sees
 * pixels rather than a crash. ortg_is() is the test every patch makes first.
 *
 * OS-friendly as displaydb.c is: SetFunction() under Forbid(); each patch
 * passes what isn't its own to the vector it replaced; patches stay.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <graphics/sprite.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/opengpu.h>
#include <opengpu/build.h>
#include <openrtg/openrtg.h>

#include "modes.h"
#include "screens.h"
#include "opengfx_bridge.h"

#define REG(r, decl) register decl __asm(#r)

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *UtilityBase;
struct Library *OpenGPUBase;
static struct Library *OpenGfxBase;
static int ogfx_handoff;
static int ogfx_all;     /* OpenGfx 1.4: it patches every drawing call, and OpenRTG provides all of them */

/* ---- tracing to the serial port (ORTG_TRACE) ---- */

#define ORTG_TRACE 0
#if ORTG_TRACE
static void ser(const char *t)
{
    while (*t) {
        register UBYTE c __asm("d0") = (UBYTE)*t++;
        __asm volatile ("move.l a6,-(sp)\n\tmove.l 4.w,a6\n\tjsr -516(a6)\n\tmove.l (sp)+,a6" : "+d"(c) : : "d1", "a0", "a1", "cc", "memory");
    }
}
static void serx(const char *t, ULONG v)
{
    char b[12];
    ser(t);
    for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[8] = '\r'; b[9] = '\n'; b[10] = 0;
    ser(b);
}
/* one line for a big drawing call: what, the six numbers, the first 120 only */
static void trace_big(const char *what, LONG a, LONG b, LONG c, LONG d, LONG w, LONG h, ULONG extra)
{
    static int n;
    LONG v[7] = { a, b, c, d, w, h, (LONG)extra };
    if (w * h < 64 || n >= 400) return;
    n++;
    ser(what);
    for (int k = 0; k < 7; k++) {
        char t[12], *q = t + 11;
        ULONG u = (ULONG)(v[k] < 0 ? -v[k] : v[k]);
        *q = 0;
        if (k == 6) { for (int i = 0; i < 8; i++) { *--q = "0123456789abcdef"[u & 15]; u >>= 4; } }
        else { do { *--q = (char)('0' + u % 10); u /= 10; } while (u); if (v[k] < 0) *--q = '-'; }
        ser(" "); ser(q);
    }
    ser("\r\n");
}
#else
#define ser(t) ((void)0)
#define serx(t, v) ((void)0)
#define trace_big(w, a, b, c, d, e, f, x) ((void)0)
#endif

/* ---- the boards ----------------------------------------------------------------------- */

#define VRAM_AT   0x00010000UL
#define VRAM_SIZE 0x00FE0000UL          /* the 16 MiB board's; a 64 MiB board's is in vram_size[] (the board's size less its two 64 KiB pages) */
#define PAGE_64K  0x00010000UL
#define R_WIDTH 0x10
#define R_HEIGHT 0x14
#define R_FORMAT 0x18
#define R_PAN_X 0x24
#define R_PAN_Y 0x28
#define R_CLOCK 0x2C
#define R_SWITCH 0x30   /* the board's SetSwitch: the viewer shows this head while it is 1 */
#define R_COMMIT 0x48
#define R_RESULT 0x4C
#define R_ARG_A 0x60
#define R_ARG_B 0x64
#define R_PAL_INDEX 0x80
#define R_PAL_RGB 0x84
#define R_SPR_CTRL 0x90
#define R_SPR_X 0x94
#define R_SPR_Y 0x98
#define R_SPR_HOT 0x9C
#define R_SPR_SIZE 0xA0
#define R_SPR_DATA 0xA4
#define R_SPR_COLOR 0xA8
#define R_VERSION 0x04
#define R_RING_BASE 0xB0
#define R_RING_SIZE 0xB4
#define R_GPU_INFO 0xD0
#define RING_RESERVE 0x00040000UL   /* as acrtg.card 2.3: the top 256 KiB of video RAM for OpenGPU's ring */
#define RING_BYTES   0x00010000UL
#define C_MODE 1
#define C_PAN 6
#define C_DISPLAY 7
#define FMT_CLUT 1
#define FMT_ARGB32 6
#define FMT_RGB16 11
static const ULONG board_fmt[3] = { FMT_CLUT, FMT_RGB16, FMT_ARGB32 };   /* by enum ortg_format */

static UBYTE *board[ORTG_MAX_MONITORS + 1];               /* each monitor's board, or NULL */
static struct ortg_mode_table **tables;

/* Each board's size sets where its video RAM ends and its register page lies (the page at its end). */
static ULONG board_size[ORTG_MAX_MONITORS + 1], regs_at[ORTG_MAX_MONITORS + 1], vram_size[ORTG_MAX_MONITORS + 1];

static void reg(int n, ULONG off, ULONG v) { *(volatile ULONG *)(board[n] + regs_at[n] + off) = v; }
static ULONG rreg(int n, ULONG off) { return *(volatile ULONG *)(board[n] + regs_at[n] + off); }

/* The video RAM bitmaps may use on each board. */
static ULONG vram_limit[ORTG_MAX_MONITORS + 1];

/* OpenGPU's command ring needs a place in video RAM nobody else uses. With
 * Picasso96, acrtg.card 2.3 keeps the top 256 KiB back and sets the ring
 * up there; OpenRTG, driving the board itself, does the same, so ACRTG.gpu
 * finds its ring either way. A ring already there is left alone. */
static void ring_reserve(int n)
{
    ULONG base, size;
    vram_limit[n] = vram_size[n];
    if (rreg(n, R_VERSION) < 3 || !(rreg(n, R_GPU_INFO) & 1)) return;     /* no ring before acrtg-v3 */
    base = rreg(n, R_RING_BASE); size = rreg(n, R_RING_SIZE);
    if (!size || base < vram_size[n] - RING_RESERVE || base + size > vram_size[n]) {
        reg(n, R_RING_BASE, vram_size[n] - RING_RESERVE);
        reg(n, R_RING_SIZE, RING_BYTES);
    }
    vram_limit[n] = vram_size[n] - RING_RESERVE;
}

/* What each monitor shows now. */
static struct { struct ortg_bitmap *bm; UWORD w, h; UBYTE fmt; } shown[ORTG_MAX_MONITORS + 1];

/* each monitor's pen colours, for its 16 and 32-bit bitmaps; [0] for the rest */
ULONG ortg_pen_rgb[ORTG_MAX_MONITORS + 1][256];

/* ---- video RAM: first fit in fixed blocks ------------------------------------------- */

#define BLOCKS 128
static struct { ULONG off, size; UBYTE used, monitor; } blk[BLOCKS];
static int nblk;

static LONG vram_alloc(int monitor, ULONG size)
{
    int i;
    ULONG off = 0;
    size = (size + 63) & ~63UL;
    /* the lowest gap on this monitor's board that fits */
    for (;;) {
        int clash = -1;
        for (i = 0; i < nblk; i++)
            if (blk[i].used && blk[i].monitor == monitor && off < blk[i].off + blk[i].size && blk[i].off < off + size) { clash = i; break; }
        if (clash < 0) break;
        off = blk[clash].off + blk[clash].size;
    }
    if (off + size > (vram_limit[monitor] ? vram_limit[monitor] : vram_size[monitor])) return -1;
    for (i = 0; i < BLOCKS; i++)
        if (!blk[i].used) {
            blk[i].off = off; blk[i].size = size; blk[i].used = 1; blk[i].monitor = (UBYTE)monitor;
            if (i >= nblk) nblk = i + 1;
            return (LONG)off;
        }
    return -1;
}

static void vram_free(int monitor, ULONG off)
{
    for (int i = 0; i < nblk; i++)
        if (blk[i].used && blk[i].monitor == monitor && blk[i].off == off) { blk[i].used = 0; return; }
}

/* ---- OpenRTG bitmaps ---------------------------------------------------------------- */

#define ORTG_PAD 0x4F52                                  /* "OR" */
#define ORTG_HEAD 64            /* before the pixels: the magic and the ortg_bitmap */

/* The OpenRTG bitmap a BitMap is, or is a copy of: intuition copies a
 * screen's custom bitmap into the Screen's own BitMap, so a copy (same
 * marks, same pixels) counts as the bitmap. The mark is pad and the two
 * longwords just before the pixels: every plane pointer is the pixels, since
 * code that doesn't know the bitmap writes it as planes, all eight of them
 * (5 Oct 2026: with the ortg_bitmap's address in Planes[7], an 8-plane write
 * landed on it and on whatever followed it: a reopened Workbench's Screen) */
static struct ortg_bitmap *ortg_of(struct BitMap *bm)
{
    struct ortg_bitmap *o;
    ULONG *t;
    if (!bm || bm->pad != ORTG_PAD || !bm->Planes[0]) return NULL;
    t = (ULONG *)bm->Planes[0];
    if (t[-2] != ORTG_BM_MAGIC) return NULL;
    o = (struct ortg_bitmap *)t[-1];
    return o && o->magic == ORTG_BM_MAGIC && o->mem == bm->Planes[0] ? o : NULL;
}

int ortg_is(struct BitMap *bm) { return ortg_of(bm) != NULL; }

/* 1 when any monitor shows an OpenRTG screen (BestModeIDA, displaydb.c). */
int ortg_any_shown(void)
{
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++) if (shown[n].bm) return 1;
    return 0;
}

/* A chunky bitmap of a format: in monitor's video RAM when monitor > 0 (and
 * it fits), else in fast RAM. Its pens use monitor's table (0: the first's). */
struct ortg_bitmap *ortg_alloc(int monitor, ULONG w, ULONG h, int clear, int format)
{
    struct ortg_bitmap *o = AllocVec(sizeof *o, MEMF_ANY | MEMF_CLEAR);
    ULONG bpp = format == ORTG_ARGB32 ? 4 : format == ORTG_RGB16 ? 2 : 1;
    ULONG stride = (w * bpp + 63) & ~63UL;
    if (!o) return NULL;
    o->magic = ORTG_BM_MAGIC;
    o->format = (UBYTE)format; o->bpp = (UBYTE)bpp;
    o->pal_index = (UBYTE)(monitor > 0 && monitor <= ORTG_MAX_MONITORS ? monitor : 1);
    o->width = (UWORD)w; o->height = (UWORD)h; o->stride = stride;
    if (monitor > 0 && monitor <= ORTG_MAX_MONITORS && board[monitor]) {
        LONG off = vram_alloc(monitor, ORTG_HEAD + stride * h);
        if (off >= 0) {
            o->monitor = (UBYTE)monitor; o->vram_block = (ULONG)off; o->vram_off = (ULONG)off + ORTG_HEAD;
            o->mem = board[monitor] + VRAM_AT + o->vram_off;
        }
    }
    if (!o->mem) {
        UBYTE *raw;
        o->monitor = 0;
        if (!(raw = AllocVec(ORTG_HEAD + stride * h, MEMF_ANY | (clear ? MEMF_CLEAR : 0)))) { FreeVec(o); return NULL; }
        o->mem = raw + ORTG_HEAD;
        clear = 0;
    }
    ((ULONG *)o->mem)[-2] = ORTG_BM_MAGIC;
    ((ULONG *)o->mem)[-1] = (ULONG)o;
    if (clear) for (ULONG i = 0; i < stride * h; i++) o->mem[i] = 0;
    o->bm.BytesPerRow = (UWORD)stride;
    o->bm.Rows = (UWORD)h;
    o->bm.Depth = 8;
    o->bm.pad = ORTG_PAD;
    for (int p = 0; p < 8; p++) o->bm.Planes[p] = o->mem;
    return o;
}

void ortg_free(struct ortg_bitmap *o)
{
    if (!o) return;
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
        if (shown[n].bm == o) shown[n].bm = NULL;
    ((ULONG *)o->mem)[-2] = 0;
    if (o->monitor) vram_free(o->monitor, o->vram_block);
    else FreeVec(o->mem - ORTG_HEAD);
    o->magic = 0;
    FreeVec(o);
}

/* ---- pixels: what a pen and a draw mode do to a byte ----------------------------------- */

struct pens { UBYTE a, b, mode, mask; };

static void pens_of(struct RastPort *rp, struct pens *p)
{
    p->a = rp->FgPen; p->b = rp->BgPen; p->mode = rp->DrawMode; p->mask = rp->Mask;
    if (p->mode & INVERSVID) { UBYTE t = p->a; p->a = p->b; p->b = t; }
}

/* The pens as one bitmap's pixels: on 8-bit the pens and the mask work on
 * the byte; on 16 and 32-bit a pen is its colour, the mask only switches
 * drawing off (0) or on, and COMPLEMENT inverts the colour. */
struct ink { ULONG a, b, x; UBYTE mode, mask; UBYTE bpp; };
struct fill_ctx { struct pens p; UWORD *ptrn; int ptsz; int ptoff_y; };
struct obatch;
struct tmpl_ctx { struct pens p; const UBYTE *src; LONG src_x, mod; LONG at_x, at_y; struct obatch *ob; };

static void ink_of(const struct pens *p, const struct ortg_bitmap *o, struct ink *k)
{
    k->mode = p->mode; k->mask = p->mask; k->bpp = o->bpp;
    k->a = ortg_pen_px(o, p->a); k->b = ortg_pen_px(o, p->b);
    k->x = o->bpp == 1 ? p->mask : o->bpp == 2 ? 0xFFFF : 0xFFFFFF;
}

/* One pixel where a source bit is `on`. */
static inline void plot(struct ortg_bitmap *o, LONG x, LONG y, const struct ink *k, int on)
{
    ULONG d;
    if (!k->mask) return;
    if (k->mode & COMPLEMENT) { if (on) ortg_put(o, x, y, ortg_get(o, x, y) ^ k->x); return; }
    if (!on && !(k->mode & JAM2)) return;
    if (k->bpp != 1 || k->mask == 0xFF) { ortg_put(o, x, y, on ? k->a : k->b); return; }
    d = ortg_get(o, x, y);
    ortg_put(o, x, y, (d & ~k->mask) | ((on ? k->a : k->b) & k->mask));
}

/* The pen nearest a pixel's colour. */
UBYTE ortg_px_pen(const struct ortg_bitmap *o, ULONG px)
{
    const ULONG *pal = ortg_pen_rgb[o->pal_index];
    ULONG rgb, best = 0, dist = ~0UL;
    if (o->bpp == 1) return (UBYTE)px;
    rgb = ortg_decode(o, px);
    for (ULONG i = 0; i < 256; i++) {
        ULONG c = o->bpp == 2 ? ortg_decode(o, ortg_encode(o, pal[i])) : pal[i];
        LONG dr = (LONG)((c >> 16) & 255) - (LONG)((rgb >> 16) & 255), dg = (LONG)((c >> 8) & 255) - (LONG)((rgb >> 8) & 255), db = (LONG)(c & 255) - (LONG)(rgb & 255);
        ULONG d2 = (ULONG)(dr * dr + dg * dg + db * db);
        if (d2 < dist) { dist = d2; best = i; if (!d2) break; }
    }
    return (UBYTE)best;
}

/* ---- OpenGPU: the drawing that goes through its stream ---------------------------------
 *
 * Each helper draws one piece (a rectangle of one bitmap, inclusive
 * coordinates) with OpenGPU and returns 1, or returns 0 having drawn
 * nothing, and the caller draws the piece with the CPU code below, which
 * defines what OpenRTG draws. A helper is used only where its command
 * gives the CPU code's pixels exactly. Each piece is one batch, done when
 * the helper returns (graphics.library's calls are synchronous); lines
 * gather their pieces into one batch. ortg_stats counts both ways. */

#define ORTG_OGPU_WORDS 64

ULONG ortg_stats[ORTG_STAT_COUNT][4];

void ortg_stat(int kind, int gpu, ULONG pixels)
{
    if (kind < 0 || kind >= ORTG_STAT_COUNT) return;
    ortg_stats[kind][gpu ? 0 : 1]++;
    ortg_stats[kind][gpu ? 2 : 3] += pixels;
}

static int ogpu_off;            /* ORTG_DRAW_CPU_ONLY: everything on the CPU (for comparing the two) */

ULONG ortg_draw_stats(ULONG *counts, ULONG kinds, ULONG flags)
{
    ULONG k, i;
    Forbid();
    for (k = 0; k < ORTG_STAT_COUNT; k++)
        for (i = 0; i < 4; i++) {
            if (counts && k < kinds) counts[k * 4 + i] = ortg_stats[k][i];
            if (flags & ORTG_DRAW_RESET) ortg_stats[k][i] = 0;
        }
    if (flags & ORTG_DRAW_CPU_ONLY) ogpu_off = 1;
    if (flags & ORTG_DRAW_OPENGPU) ogpu_off = 0;
    Permit();
    return ORTG_STAT_COUNT;
}

static ULONG area(LONG x0, LONG y0, LONG x1, LONG y1) { return (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1); }

static int ogpu_format_of(const struct ortg_bitmap *bm)
{
    if (!bm) return 0;
    if (bm->format == ORTG_CLUT8) return OGPU_FMT_CLUT8;
    if (bm->format == ORTG_RGB16) return OGPU_FMT_RGB565;
    if (bm->format == ORTG_ARGB32) return OGPU_FMT_ARGB32;
    return 0;
}

/* OGPU_Query's answers, kept: 0 not asked, 1 yes, 2 no (opcodes below 0x40). */
static UBYTE ogpu_known[0x40][4];

static int ogpu_can(int op, const struct ortg_bitmap *bm)
{
    int fmt = ogpu_format_of(bm), yes;
    if (!OpenGPUBase || !fmt || ogpu_off) return 0;
    if (op < 0x40 && ogpu_known[op][fmt]) return ogpu_known[op][fmt] == 1;
    yes = OGPU_ANSWER(OGPU_Query((ULONG)op, (ULONG)fmt)) != OGPU_NONE;
    /* Ask again until a process has asked: opengpu.library looks for its
     * drivers then, and the answer may change once. */
    if (op < 0x40 && ((struct Task *)FindTask(NULL))->tc_Node.ln_Type == NT_PROCESS) ogpu_known[op][fmt] = yes ? 1 : 2;
    return yes;
}

static int ogpu_submit_batch(struct OGPUBatch *b)
{
    ULONG fence = 0;
    LONG rc;
    if (!OpenGPUBase || !b || b->overflow || !b->words) return 0;
    rc = OGPU_Submit((APTR)b->buf, (ULONG)b->words, &fence);
    if (rc != OGPU_OK) return 0;
    return OGPU_Wait(fence) == OGPU_OK;
}

static void ogpu_target_bitmap(struct OGPUBatch *b, int slot, const struct ortg_bitmap *bm)
{
    ogpu_surface(b, slot, (ULONG)bm->mem, bm->stride, bm->width, bm->height, ogpu_format_of(bm));
}

/* A batch drawing on bm, slot 0 the target. */
static void ogpu_begin(struct OGPUBatch *b, UBYTE *stream, const struct ortg_bitmap *bm)
{
    ogpu_batch_init(b, stream, ORTG_OGPU_WORDS);
    ogpu_target_bitmap(b, 0, bm);
    ogpu_target(b, 0);
}

static int ogpu_fill_piece(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                           const struct ink *k)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    int op;

    if (!k || !k->mask || (k->bpp == 1 && k->mask != 0xFF)) return 0;
    op = (k->mode & COMPLEMENT) ? OGPU_OP_INVERT : OGPU_OP_FILL;
    if (!ogpu_can(op, bm)) return 0;
    ogpu_begin(&b, stream, bm);
    if (op == OGPU_OP_INVERT)
        ogpu_invert(&b, x0, y0, x1 - x0 + 1, y1 - y0 + 1, k->x);
    else
        ogpu_fill(&b, x0, y0, x1 - x0 + 1, y1 - y0 + 1, k->a);
    return ogpu_submit_batch(&b);
}

/* A raw pixel value over a rectangle (SetRast, ScrollRaster's uncovered
 * area, ORTG_FillPixels): every pixel, whatever the RastPort's mask. */
int ortg_ogpu_fill(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, ULONG value)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    if (x0 > x1 || y0 > y1) return 1;
    if (!ogpu_can(OGPU_OP_FILL, bm)) return 0;
    ogpu_begin(&b, stream, bm);
    ogpu_fill(&b, x0, y0, x1 - x0 + 1, y1 - y0 + 1, value);
    return ogpu_submit_batch(&b);
}

/* Pixel ^= mask over a rectangle (ORTG_InvertPixels). */
int ortg_ogpu_invert(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, ULONG mask)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    if (!ogpu_can(OGPU_OP_INVERT, bm)) return 0;
    ogpu_begin(&b, stream, bm);
    ogpu_invert(&b, x0, y0, x1 - x0 + 1, y1 - y0 + 1, mask);
    return ogpu_submit_batch(&b);
}

/* Pixels in memory, 1:1, into a rectangle: OGPU's PIXELS (format an
 * OGPU_FMT_ source format; table an INDEX8 source's 256 ARGB colours). */
int ortg_ogpu_pixels(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                     const void *src, ULONG src_bpr, int format, const void *table)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    if (!ogpu_can(OGPU_OP_PIXELS, bm)) return 0;
    ogpu_begin(&b, stream, bm);
    ogpu_pixels(&b, (ULONG)src, src_bpr, format, (ULONG)table, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    return ogpu_submit_batch(&b);
}

/* A rectangle of the bitmap copied, raw, into memory (ORTG_ReadPixels' RAW
 * format): a surface of the same format there, and a COPY. */
int ortg_ogpu_read(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, void *dst, ULONG dst_bpr)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    LONG w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (!ogpu_can(OGPU_OP_COPY, bm)) return 0;
    ogpu_batch_init(&b, stream, ORTG_OGPU_WORDS);
    ogpu_target_bitmap(&b, 0, bm);
    ogpu_surface(&b, 1, (ULONG)dst, dst_bpr, w, h, ogpu_format_of(bm));
    ogpu_target(&b, 1);
    ogpu_copy(&b, 0, x0, y0, 0, 0, w, h);
    return ogpu_submit_batch(&b);
}

/* A rectangle of a 16 or 32-bit bitmap into memory as ARGB with alpha 255
 * (ORTG_ReadPixels' ARGB and 0RGB): COMPOSITE, opaque, onto a surface
 * there, which writes each pixel's colour with alpha 255. */
int ortg_ogpu_read_argb(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, void *dst, ULONG dst_bpr)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    LONG w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (bm->bpp == 1 || !ogpu_can(OGPU_OP_COMPOSITE, bm)) return 0;
    ogpu_batch_init(&b, stream, ORTG_OGPU_WORDS);
    ogpu_target_bitmap(&b, 0, bm);
    ogpu_surface(&b, 1, (ULONG)dst, dst_bpr, w, h, OGPU_FMT_ARGB32);
    ogpu_target(&b, 1);
    ogpu_composite(&b, 0, x0, y0, w, h, 0, 0, w, h, 255, 0);
    return ogpu_submit_batch(&b);
}

/* ARGB pixels in memory blended over a rectangle by their alpha times
 * alpha (0-255): COMPOSITE with SRCALPHA, 1:1 (16 and 32-bit bitmaps). */
int ortg_ogpu_alpha(struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                    const void *src, ULONG src_bpr, ULONG alpha)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    LONG w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (bm->bpp == 1 || !ogpu_can(OGPU_OP_COMPOSITE, bm)) return 0;
    ogpu_batch_init(&b, stream, ORTG_OGPU_WORDS);
    ogpu_target_bitmap(&b, 0, bm);
    ogpu_surface(&b, 1, (ULONG)src, src_bpr, w, h, OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    ogpu_composite(&b, 1, 0, 0, w, h, x0, y0, w, h, (int)(alpha & 255), OGPU_COMP_SRCALPHA);
    return ogpu_submit_batch(&b);
}

static int ogpu_pattern_piece(struct ortg_bitmap *bm,
                              LONG x0, LONG y0, LONG x1, LONG y1,
                              LONG dx, LONG dy, const struct fill_ctx *f,
                              const struct ink *k)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    UWORD shifted[64];
    const UWORD *ptrn;
    if (!f || !f->ptrn || !k || !k->mask || f->ptsz < 1 || f->ptsz > 64 ||
        (k->bpp == 1 && k->mask != 0xFF))
        return 0;
    if (!ogpu_can(OGPU_OP_PATTERN, bm)) return 0;
    /* OpenGPU's PATTERN is anchored at the bitmap's 0,0 and OpenRTG's at the
     * RastPort's (dx, dy in the bitmap): the rows taken from dy on, each
     * turned right by dx, are the same pattern anchored at 0,0. */
    ptrn = f->ptrn;
    if (dx || dy) {
        int r, sh = (int)(dx & 15);
        for (r = 0; r < f->ptsz; r++) {
            UWORD v = f->ptrn[(UWORD)(r - dy) & (f->ptsz - 1)];
            shifted[r] = sh ? (UWORD)((v >> sh) | (v << (16 - sh))) : v;
        }
        ptrn = shifted;
    }
    ogpu_begin(&b, stream, bm);
    ogpu_pattern(&b, (ULONG)ptrn, (ULONG)f->ptsz,
                 x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                 k->a, k->b, k->mode & ~INVERSVID);
    return ogpu_submit_batch(&b);
}

/* One batch for many pieces of one call (Text's glyphs): what a piece adds
 * waits for ob_flush, which every CPU drawing in the call comes after. */
#define OB_WORDS 128
struct obatch {
    struct OGPUBatch b;
    const struct ortg_bitmap *tgt;
    int failed;
    UBYTE buf[OB_WORDS * 4];
};

static void ob_init(struct obatch *o) { ogpu_batch_init(&o->b, o->buf, OB_WORDS); o->tgt = NULL; o->failed = 0; }

static void ob_flush(struct obatch *o)
{
    if (o && o->b.words) {
        if (!ogpu_submit_batch(&o->b)) o->failed = 1;
        ogpu_batch_init(&o->b, o->buf, OB_WORDS);
        o->tgt = NULL;
    }
}

/* Room for `words` more, drawing on bm. */
static void ob_room(struct obatch *o, const struct ortg_bitmap *bm, int words)
{
    if (o->b.words + words + 8 > OB_WORDS) ob_flush(o);
    if (o->tgt != bm) { ogpu_target_bitmap(&o->b, 0, bm); ogpu_target(&o->b, 0); o->tgt = bm; }
}

static int ogpu_template_piece(struct ortg_bitmap *bm,
                               LONG x0, LONG y0, LONG x1, LONG y1,
                               LONG dx, LONG dy, const struct tmpl_ctx *t,
                               const struct ink *k)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;
    LONG row, first, mod;
    const UBYTE *src;
    UBYTE rom_rows[256];

    if (!t || !k || !k->mask || (k->bpp == 1 && k->mask != 0xFF) || t->mod <= 0)
        return 0;
    if (!ogpu_can(OGPU_OP_TEMPLATE, bm)) return 0;

    row = y0 - dy - t->at_y;
    first = t->src_x + (x0 - dx - t->at_x);
    if (row < 0 || first < 0) return 0;
    src = t->src + row * t->mod;
    mod = t->mod;
    if (t->ob) {
        /* gathered into the call's batch (the source stays where it is) */
        if (!TypeOfMem((APTR)src)) return 0;
        ob_room(t->ob, bm, 9);
        ogpu_template(&t->ob->b, (ULONG)src, (ULONG)mod, (ULONG)first, x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                      k->a, k->b, k->mode & ~INVERSVID);
        return 1;
    }
    /* A template in ROM (topaz's glyphs) is out of a host back end's reach:
     * the rows this piece needs, copied to the stack. */
    if (!TypeOfMem((APTR)src)) {
        LONG h = y1 - y0 + 1, bytes = ((first & 7) + (x1 - x0 + 1) + 7) >> 3, r, i;
        if (h * bytes > (LONG)sizeof rom_rows) return 0;
        for (r = 0; r < h; r++)
            for (i = 0; i < bytes; i++) rom_rows[r * bytes + i] = src[r * t->mod + (first >> 3) + i];
        src = rom_rows; mod = bytes; first &= 7;
    }

    ogpu_begin(&b, stream, bm);
    ogpu_template(&b, (ULONG)src, (ULONG)mod, (ULONG)first,
                  x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                  k->a, k->b, k->mode & ~INVERSVID);
    return ogpu_submit_batch(&b);
}

static int ogpu_copy_rect(struct ortg_bitmap *src, LONG sx, LONG sy,
                          struct ortg_bitmap *dst, LONG dx, LONG dy,
                          LONG w, LONG h)
{
    UBYTE stream[ORTG_OGPU_WORDS * 4];
    struct OGPUBatch b;

    if (!src || !dst || src->format != dst->format || w <= 0 || h <= 0)
        return 0;
    if (!ogpu_can(OGPU_OP_COPY, dst)) return 0;

    ogpu_batch_init(&b, stream, ORTG_OGPU_WORDS);
    ogpu_target_bitmap(&b, 0, src);
    ogpu_target_bitmap(&b, 1, dst);
    ogpu_target(&b, 1);
    ogpu_copy(&b, 0, sx, sy, dx, dy, w, h);
    return ogpu_submit_batch(&b);
}

/* ---- clipping: an operation on every visible piece of a RastPort's box -------------- */

/* fn gets the target bitmap, the box in it (inclusive), and dx, dy: the bitmap
 * position of RastPort coordinate (0, 0). */
typedef void (*piece_fn)(void *ctx, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy);

/* 0 when the RastPort isn't on an OpenRTG bitmap (the caller goes to graphics). */
static int pieces(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, piece_fn fn, void *ctx)
{
    struct Layer *l = rp->Layer;
    struct ortg_bitmap *scr = ortg_of(rp->BitMap);
    if (!scr) return 0;
    if (x0 > x1 || y0 > y1) return 1;
    if (!l) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= scr->width) x1 = scr->width - 1;
        if (y1 >= scr->height) y1 = scr->height - 1;
        if (x0 <= x1 && y0 <= y1) fn(ctx, scr, x0, y0, x1, y1, 0, 0);
        return 1;
    }
    ObtainSemaphore(&l->Lock);                    /* LockLayerRom: its argument is in a5 */
    {
        LONG ox = l->bounds.MinX - l->Scroll_X, oy = l->bounds.MinY - l->Scroll_Y;
        LONG sx0 = x0 + ox, sy0 = y0 + oy, sx1 = x1 + ox, sy1 = y1 + oy;
        struct ClipRect *cr;
        for (cr = l->ClipRect; cr; cr = cr->Next) {
            LONG a0 = sx0 > cr->bounds.MinX ? sx0 : cr->bounds.MinX, b0 = sy0 > cr->bounds.MinY ? sy0 : cr->bounds.MinY;
            LONG a1 = sx1 < cr->bounds.MaxX ? sx1 : cr->bounds.MaxX, b1 = sy1 < cr->bounds.MaxY ? sy1 : cr->bounds.MaxY;
            if (a0 > a1 || b0 > b1) continue;
            if (!cr->obscured) {
                if (a0 < 0) a0 = 0;
                if (b0 < 0) b0 = 0;
                if (a1 >= scr->width) a1 = scr->width - 1;
                if (b1 >= scr->height) b1 = scr->height - 1;
                if (a0 <= a1 && b0 <= b1) fn(ctx, scr, a0, b0, a1, b1, ox, oy);
            } else if (cr->BitMap) {
                /* smart refresh: the hidden part lives in a bitmap of its own,
                 * its x origin aligned to 16 pixels as layers allocates it */
                struct ortg_bitmap *bb = ortg_of(cr->BitMap);
                LONG bx = cr->bounds.MinX & 15, by = 0;
                LONG tx = bx - cr->bounds.MinX, ty = by - cr->bounds.MinY;
                if (bb) fn(ctx, bb, a0 + tx, b0 + ty, a1 + tx, b1 + ty, ox + tx, oy + ty);
            }
        }
    }
    ReleaseSemaphore(&l->Lock);
    return 1;
}

struct ortg_bitmap *ortg_find(struct BitMap *bm) { return ortg_of(bm); }

int ortg_pieces(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, ortg_piece_fn fn, void *ctx)
{
    return pieces(rp, x0, y0, x1, y1, (piece_fn)fn, ctx);
}

/* The palette a RastPort draws with: its layer's screen's, else the screen
 * whose bitmap it draws on, else the front screen's. */
struct ColorMap *ortg_colormap(struct RastPort *rp)
{
    struct Screen *s;
    struct ortg_bitmap *o = ortg_of(rp->BitMap);
    if (!IntuitionBase) return NULL;
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        if ((rp->Layer && rp->Layer->LayerInfo == &s->LayerInfo) || rp->BitMap == s->RastPort.BitMap ||
            (o && ortg_of(s->RastPort.BitMap) == o)) return s->ViewPort.ColorMap;
    return IntuitionBase->FirstScreen ? IntuitionBase->FirstScreen->ViewPort.ColorMap : NULL;
}

/* ---- the operations ------------------------------------------------------------------ */

/* A fill with the RastPort's pen, its area pattern and its mode. */
static void fill_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct fill_ctx *f = c;
    struct ink k;
    int kind;
    ink_of(&f->p, bm, &k);
    kind = f->ptrn ? ORTG_STAT_PATTERN : (k.mode & COMPLEMENT) ? ORTG_STAT_INVERT : ORTG_STAT_FILL;
    if (!f->ptrn && ogpu_fill_piece(bm, x0, y0, x1, y1, &k)) { ortg_stat(kind, 1, area(x0, y0, x1, y1)); return; }
    if (f->ptrn && ogpu_pattern_piece(bm, x0, y0, x1, y1, dx, dy, f, &k)) { ortg_stat(kind, 1, area(x0, y0, x1, y1)); return; }
    ortg_stat(kind, 0, area(x0, y0, x1, y1));
    for (LONG y = y0; y <= y1; y++) {
        if (!f->ptrn) {
            if (!(k.mode & COMPLEMENT) && (k.mask == 0xFF || (k.bpp != 1 && k.mask))) {
                UBYTE *row = bm->mem + y * bm->stride;
                if (k.bpp == 1) { UBYTE v = (UBYTE)k.a; for (LONG x = x0; x <= x1; x++) row[x] = v; }
                else if (k.bpp == 2) { UWORD v = (UWORD)k.a, *r = (UWORD *)row; for (LONG x = x0; x <= x1; x++) r[x] = v; }
                else { ULONG v = k.a, *r = (ULONG *)row; for (LONG x = x0; x <= x1; x++) r[x] = v; }
            } else
                for (LONG x = x0; x <= x1; x++) plot(bm, x, y, &k, 1);
        } else {
            /* the pattern's rows repeat every ptsz rows of the RastPort */
            UWORD bits = f->ptrn[(UWORD)(y - dy) & (f->ptsz - 1)];
            for (LONG x = x0; x <= x1; x++)
                plot(bm, x, y, &k, (bits >> (15 - ((x - dx) & 15))) & 1);
        }
    }
}

/* A one-bit template: bit set, the A pen; clear, the B pen in JAM2. */
static void tmpl_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct tmpl_ctx *t = c;
    struct ink k;
    ink_of(&t->p, bm, &k);
    if (ogpu_template_piece(bm, x0, y0, x1, y1, dx, dy, t, &k)) { ortg_stat(ORTG_STAT_TEMPLATE, 1, area(x0, y0, x1, y1)); return; }
    ortg_stat(ORTG_STAT_TEMPLATE, 0, area(x0, y0, x1, y1));
    ob_flush(t->ob);                                /* what the batch holds first */
    for (LONG y = y0; y <= y1; y++) {
        const UBYTE *s = t->src + (y - dy - t->at_y) * t->mod;
        for (LONG x = x0; x <= x1; x++) {
            LONG bit = t->src_x + (x - dx - t->at_x);
            plot(bm, x, y, &k, (s[bit >> 3] >> (7 - (bit & 7))) & 1);
        }
    }
}

/* ---- pixels of a planar bitmap, and the minterm ------------------------------------ */

static UBYTE planar_pen(struct BitMap *bm, LONG x, LONG y)
{
    UBYTE v = 0;
    ULONG off = y * bm->BytesPerRow + (x >> 3);
    UBYTE bit = 0x80 >> (x & 7);
    for (int p = 0; p < bm->Depth && p < 8; p++) {
        PLANEPTR pl = bm->Planes[p];
        if (pl == (PLANEPTR)-1) v |= 1 << p;
        else if (pl && (pl[off] & bit)) v |= 1 << p;
    }
    return v;
}

static void planar_set(struct BitMap *bm, LONG x, LONG y, UBYTE v, UBYTE mask)
{
    ULONG off = y * bm->BytesPerRow + (x >> 3);
    UBYTE bit = 0x80 >> (x & 7);
    for (int p = 0; p < bm->Depth && p < 8; p++) {
        PLANEPTR pl = bm->Planes[p];
        if (!(mask & (1 << p)) || !pl || pl == (PLANEPTR)-1) continue;
        if (v & (1 << p)) pl[off] |= bit; else pl[off] &= ~bit;
    }
}

/* BltBitMap's minterm, bit by bit: B the source, C the destination. */
static inline ULONG minterm(UBYTE m, ULONG s, ULONG d)
{
    ULONG r = 0;
    if (m & 0x80) r |= s & d;
    if (m & 0x40) r |= s & ~d;
    if (m & 0x20) r |= ~s & d;
    if (m & 0x10) r |= ~s & ~d;
    return r;
}

/* A rectangle from any bitmap to an OpenRTG one, or from an OpenRTG one to
 * a planar one; overlapping copies within one bitmap are ordered so the
 * source is read before it is written. */
/* n bytes from s to d, either way round (the rows of one bitmap may overlap),
 * a longword at a time where both are longword aligned. */
static void move_bytes(UBYTE *d, const UBYTE *s, ULONG n)
{
    if (d == s || !n) return;
    if (d < s || d >= s + n) {
        if (!(((ULONG)d | (ULONG)s) & 3)) {
            ULONG *dl = (ULONG *)d; const ULONG *sl = (const ULONG *)s;
            for (; n >= 16; n -= 16) { dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3]; dl += 4; sl += 4; }
            for (; n >= 4; n -= 4) *dl++ = *sl++;
            d = (UBYTE *)dl; s = (const UBYTE *)sl;
        }
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        if (!(((ULONG)d | (ULONG)s) & 3)) {
            ULONG *dl = (ULONG *)d; const ULONG *sl = (const ULONG *)s;
            for (; n >= 4; n -= 4) *--dl = *--sl;
            d = (UBYTE *)dl; s = (const UBYTE *)sl;
        }
        while (n--) *--d = *--s;
    }
}

static void blit(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG w, LONG h, UBYTE m, UBYTE mask,
                 const UBYTE *cookie, LONG cookie_x, LONG cookie_y, LONG cookie_mod)
{
    struct ortg_bitmap *os = ortg_of(src), *od = ortg_of(dst);
    int same = os && os == od;
    int down = !(same && sy < dy), right = !(same && sy == dy && sx < dx);
    if (os && od && os->bpp == od->bpp && !cookie && (m & 0xF0) == 0xC0 && (od->bpp == 1 ? mask == 0xFF : mask != 0)) {
        /* v1.0's COPY is the preferred path for the common RTG move/copy.
         * It is synchronous at the graphics.library boundary: future queued
         * drivers are waited here before the call returns. */
        if (ogpu_copy_rect(os, sx, sy, od, dx, dy, w, h)) { ortg_stat(ORTG_STAT_COPY, 1, (ULONG)w * (ULONG)h); return; }
        ortg_stat(ORTG_STAT_COPY, 0, (ULONG)w * (ULONG)h);
        /* No OpenGPU, unsupported memory or a refused batch: keep the
         * proven OpenRTG CPU copy as the exact fallback. */
        ULONG bytes = (ULONG)w * od->bpp;
        for (LONG j = 0; j < h; j++) {
            LONG yy = down ? j : h - 1 - j;
            move_bytes(od->mem + (dy + yy) * od->stride + dx * od->bpp, os->mem + (sy + yy) * os->stride + sx * os->bpp, bytes);
        }
        return;
    }
    if (od && !cookie && ((m >> 7) & 1) == ((m >> 5) & 1) && ((m >> 6) & 1) == ((m >> 4) & 1)) {
        /* A minterm that ignores the source (layers clears a window with
         * 0x00): a fill or an invert, as the loop below does it. */
        int ok = 0, op = m & 0xF0;
        if (op == 0xA0) ok = 1;                                     /* the destination as it is */
        else if (od->bpp != 1 ? mask != 0 : (op == 0x50 || mask == 0xFF)) {
            if (op == 0x50) ok = ortg_ogpu_invert(od, dx, dy, dx + w - 1, dy + h - 1, od->bpp == 1 ? mask : od->bpp == 2 ? 0xFFFF : 0xFFFFFF);
            else ok = ortg_ogpu_fill(od, dx, dy, dx + w - 1, dy + h - 1, od->bpp == 1 ? (op ? 0xFF : 0) : ortg_pen_px(od, op ? 255 : 0));
        } else if (od->bpp != 1) ok = 1;                            /* mask 0 on 16 and 32-bit: nothing changes */
        ortg_stat(ORTG_STAT_BLIT, ok, (ULONG)w * (ULONG)h);
        if (ok) return;
    } else if (os && od && os->bpp != od->bpp && !cookie && (m & 0xF0) == 0xC0 && (od->bpp == 1 ? mask == 0xFF : mask != 0)
        && (os->bpp == 1 || od->bpp == 2)) {
        /* A copy between formats, as the loop below converts: pens to
         * their colours (through the pen table), or colours cut to RGB565. */
        const UBYTE *sp = os->mem + sy * os->stride + sx * os->bpp;
        int ok = os->bpp == 1 ? ortg_ogpu_pixels(od, dx, dy, dx + w - 1, dy + h - 1, sp, os->stride, OGPU_FMT_INDEX8, ortg_pen_rgb[od->pal_index])
                              : ortg_ogpu_pixels(od, dx, dy, dx + w - 1, dy + h - 1, sp, os->stride, ogpu_format_of(os), NULL);
        ortg_stat(ORTG_STAT_BLIT_CONVERT, ok, (ULONG)w * (ULONG)h);
        if (ok) return;
    } else if (od)
        ortg_stat(cookie ? ORTG_STAT_BLIT_MASKED : !os ? ORTG_STAT_BLIT_PLANAR : os->bpp != od->bpp ? ORTG_STAT_BLIT_CONVERT : ORTG_STAT_BLIT,
                  0, (ULONG)w * (ULONG)h);
    else if (os)
        ortg_stat(ORTG_STAT_BLIT_PLANAR, 0, (ULONG)w * (ULONG)h);
    for (LONG j = 0; j < h; j++) {
        LONG yy = down ? j : h - 1 - j;
        for (LONG i = 0; i < w; i++) {
            LONG xx = right ? i : w - 1 - i;
            ULONG s, d, r;
            if (cookie) {
                LONG bit = cookie_x + xx;
                if (!((cookie[(cookie_y + yy) * cookie_mod + (bit >> 3)] >> (7 - (bit & 7))) & 1)) continue;
            }
            s = os ? ortg_get(os, sx + xx, sy + yy) : planar_pen(src, sx + xx, sy + yy);
            if (od) {
                /* the source as the destination's pixel: a pen becomes its
                 * colour on 16 and 32-bit, a colour its nearest pen on 8-bit */
                if (!os || os->bpp == 1) { if (od->bpp != 1) s = ortg_pen_px(od, s); }
                else if (od->bpp == 1) s = ortg_px_pen(os, s);
                else if (os->bpp != od->bpp) s = ortg_encode(od, ortg_decode(os, s));
                d = ortg_get(od, dx + xx, dy + yy);
                if (cookie) r = s;
                else if (od->bpp != 1 && ((m >> 7) & 1) == ((m >> 5) & 1) && ((m >> 6) & 1) == ((m >> 4) & 1)) {
                    /* a minterm that ignores the source works on pens: a clear
                     * is pen 0 (layers clears a window that way), a set pen 255 */
                    switch (m & 0xF0) {
                    case 0x00: r = ortg_pen_px(od, 0); break;
                    case 0xF0: r = ortg_pen_px(od, 255); break;
                    case 0x50: r = d ^ (od->bpp == 2 ? 0xFFFF : 0xFFFFFF); break;
                    default: r = d; break;
                    }
                } else r = minterm(m, s, d);
                if (od->bpp == 1) r = (d & ~(ULONG)mask) | (r & mask);
                else if (!mask) r = d;
                else if (od->bpp == 2) r &= 0xFFFF;
                ortg_put(od, dx + xx, dy + yy, r);
            } else {
                if (os && os->bpp != 1) s = ortg_px_pen(os, s);
                d = planar_pen(dst, dx + xx, dy + yy);
                r = cookie ? s : minterm(m, s, d);
                planar_set(dst, dx + xx, dy + yy, (UBYTE)r, mask);
            }
        }
    }
}

struct blit_ctx { struct BitMap *src; LONG sx, sy, at_x, at_y; UBYTE m, mask; const UBYTE *cookie; LONG cmod; };

static void blit_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct blit_ctx *b = c;
    LONG rx = x0 - dx - b->at_x, ry = y0 - dy - b->at_y;    /* offset into the source box */
    blit(b->src, b->sx + rx, b->sy + ry, &bm->bm, x0, y0, x1 - x0 + 1, y1 - y0 + 1, b->m, b->mask,
         b->cookie, b->cookie ? rx + b->sx : 0, b->cookie ? ry + b->sy : 0, b->cmod);
}

/* ---- the patches ----------------------------------------------------------------------- */

typedef void (*rectfill_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(d2, LONG), REG(d3, LONG), REG(a6, struct GfxBase *));
typedef void (*bltpattern_fn)(REG(a1, struct RastPort *), REG(a0, PLANEPTR), REG(d0, LONG), REG(d1, LONG), REG(d2, LONG), REG(d3, LONG), REG(d4, ULONG), REG(a6, struct GfxBase *));
typedef void (*setrast_fn)(REG(a1, struct RastPort *), REG(d0, ULONG), REG(a6, struct GfxBase *));
typedef void (*draw_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef LONG (*writepixel_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef ULONG (*readpixel_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef void (*polydraw_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(a0, WORD *), REG(a6, struct GfxBase *));
typedef LONG (*text_fn)(REG(a1, struct RastPort *), REG(a0, STRPTR), REG(d0, ULONG), REG(a6, struct GfxBase *));
typedef void (*blttemplate_fn)(REG(a0, PLANEPTR), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(a6, struct GfxBase *));
typedef LONG (*bltbitmap_fn)(REG(a0, struct BitMap *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct BitMap *), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG), REG(d7, ULONG), REG(a2, PLANEPTR), REG(a6, struct GfxBase *));
typedef void (*bltbmrp_fn)(REG(a0, struct BitMap *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG), REG(a6, struct GfxBase *));
typedef void (*bltmaskbmrp_fn)(REG(a0, struct BitMap *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG), REG(a2, PLANEPTR), REG(a6, struct GfxBase *));
typedef void (*clipblit_fn)(REG(a0, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG), REG(a6, struct GfxBase *));
typedef void (*scroll_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(a6, struct GfxBase *));
typedef struct BitMap *(*allocbm_fn)(REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG), REG(a0, struct BitMap *), REG(a6, struct GfxBase *));
typedef void (*freebm_fn)(REG(a0, struct BitMap *), REG(a6, struct GfxBase *));
typedef ULONG (*bmattr_fn)(REG(a0, struct BitMap *), REG(d1, ULONG), REG(a6, struct GfxBase *));
typedef ULONG (*makevp_fn)(REG(a0, struct View *), REG(a1, struct ViewPort *), REG(a6, struct GfxBase *));
typedef ULONG (*mrgcop_fn)(REG(a1, struct View *), REG(a6, struct GfxBase *));
typedef void (*loadview_fn)(REG(a1, struct View *), REG(a6, struct GfxBase *));
typedef void (*loadrgb32_fn)(REG(a0, struct ViewPort *), REG(a1, ULONG *), REG(a6, struct GfxBase *));
typedef void (*setrgb32_fn)(REG(a0, struct ViewPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG), REG(a6, struct GfxBase *));
typedef void (*loadrgb4_fn)(REG(a0, struct ViewPort *), REG(a1, UWORD *), REG(d0, LONG), REG(a6, struct GfxBase *));
typedef void (*setrgb4_fn)(REG(a0, struct ViewPort *), REG(d0, LONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG), REG(a6, struct GfxBase *));
typedef void (*wcp_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG), REG(a2, UBYTE *), REG(d4, LONG), REG(a6, struct GfxBase *));
typedef LONG (*wpa8_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG), REG(a2, UBYTE *), REG(a1, struct RastPort *), REG(a6, struct GfxBase *));
typedef void (*movesprite_fn)(REG(a0, struct ViewPort *), REG(a1, struct SimpleSprite *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef LONG (*changeext_fn)(REG(a0, struct ViewPort *), REG(a1, struct ExtSprite *), REG(a2, struct ExtSprite *), REG(a3, struct TagItem *), REG(a6, struct GfxBase *));
typedef void (*remake_fn)(REG(a6, struct IntuitionBase *));
typedef LONG (*makescreen_fn)(REG(a0, struct Screen *), REG(a6, struct IntuitionBase *));
typedef struct Screen *(*openscreen_fn)(REG(a0, struct NewScreen *), REG(a1, struct TagItem *), REG(a6, struct IntuitionBase *));
typedef BOOL (*closescreen_fn)(REG(a0, struct Screen *), REG(a6, struct IntuitionBase *));

static rectfill_fn old_rectfill;
static bltpattern_fn old_bltpattern;
static setrast_fn old_setrast;
static draw_fn old_draw;
static writepixel_fn old_writepixel;
static readpixel_fn old_readpixel;
static polydraw_fn old_polydraw;
static text_fn old_text;
static blttemplate_fn old_blttemplate;
static bltbitmap_fn old_bltbitmap;
static bltbmrp_fn old_bltbmrp;
static bltmaskbmrp_fn old_bltmaskbmrp;
static clipblit_fn old_clipblit;
static scroll_fn old_scroll;
static allocbm_fn old_allocbm;
static freebm_fn old_freebm;
static bmattr_fn old_bmattr;
static makevp_fn old_makevp;
static mrgcop_fn old_mrgcop;
static loadview_fn old_loadview;
static loadrgb32_fn old_loadrgb32;
static setrgb32_fn old_setrgb32;
static loadrgb4_fn old_loadrgb4;
static setrgb4_fn old_setrgb4;
static wcp_fn old_wcp;
static wpa8_fn old_wpa8;
typedef LONG (*line8_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(a2, UBYTE *), REG(a1, struct RastPort *), REG(a6, struct GfxBase *));
static line8_fn old_wpl8, old_rpl8;
static wpa8_fn old_rpa8;
static movesprite_fn old_movesprite;
static changeext_fn old_changeext;
static remake_fn old_rethink, old_remake;
static makescreen_fn old_makescreen;
static openscreen_fn old_openscreen;
static closescreen_fn old_closescreen;

/* -- fills -- */

static void do_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, const UBYTE *mask, ULONG mask_bpr)
{
    if (mask) {
        struct tmpl_ctx t;
        pens_of(rp, &t.p);
        t.p.mode &= ~JAM2;                              /* a mask draws only where it is set */
        t.src = mask; t.src_x = 0; t.mod = (LONG)mask_bpr; t.at_x = x0; t.at_y = y0; t.ob = NULL;
        pieces(rp, x0, y0, x1, y1, tmpl_piece, &t);
        return;
    }
    {
        struct fill_ctx f;
        pens_of(rp, &f.p);
        f.ptrn = rp->AreaPtrn;
        f.ptsz = 1 << (rp->AreaPtSz < 0 ? 0 : rp->AreaPtSz);
        if (!f.ptrn) f.p.mode &= ~JAM2;
        pieces(rp, x0, y0, x1, y1, fill_piece, &f);
    }
}

static void rectfill_patch(REG(a1, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, WORD x1), REG(d3, WORD y1), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) { trace_big("ortg: RectFill", x0, y0, x1, y1, x1 - x0 + 1, y1 - y0 + 1, rp->FgPen); do_fill(rp, x0, y0, x1, y1, NULL, 0); return; }
    old_rectfill(rp, x0, y0, x1, y1, g);
}

static void bltpattern_patch(REG(a1, struct RastPort *rp), REG(a0, PLANEPTR mask), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, WORD x1), REG(d3, WORD y1), REG(d4, ULONG bpr), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) { trace_big("ortg: BltPattern", x0, y0, x1, y1, x1 - x0 + 1, y1 - y0 + 1, (ULONG)mask); do_fill(rp, x0, y0, x1, y1, mask, bpr); return; }
    old_bltpattern(rp, mask, x0, y0, x1, y1, bpr, g);
}

static void setrast_patch(REG(a1, struct RastPort *rp), REG(d0, ULONG pen), REG(a6, struct GfxBase *g))
{
    struct ortg_bitmap *o = ortg_of(rp->BitMap);
    if (o && rp->Layer) {
        /* A layer's RastPort: SetRast clears the layer, not the bitmap, and only where its clip rectangles are,
         * so an installed clip region (Clock's date does that) keeps it to the region. Filling the whole
         * bitmap, as the bare path below does, wiped the screen: the Workbench desktop went grey (10 Oct 2026). */
        struct Layer *l = rp->Layer;
        struct fill_ctx f;
        LONG w = l->bounds.MaxX - l->bounds.MinX, h = l->bounds.MaxY - l->bounds.MinY;
        f.p.a = (UBYTE)pen; f.p.b = (UBYTE)pen; f.p.mode = JAM1; f.p.mask = rp->Mask;
        f.ptrn = NULL; f.ptsz = 1; f.ptoff_y = 0;
        pieces(rp, l->Scroll_X, l->Scroll_Y, l->Scroll_X + w, l->Scroll_Y + h, fill_piece, &f);
        return;
    }
    if (o) {
        ULONG v = ortg_pen_px(o, pen);
        ULONG n = (ULONG)o->width * o->height;
        if (ortg_ogpu_fill(o, 0, 0, (LONG)o->width - 1, (LONG)o->height - 1, v)) { ortg_stat(ORTG_STAT_SETRAST, 1, n); return; }
        ortg_stat(ORTG_STAT_SETRAST, 0, n);
        for (ULONG y = 0; y < o->height; y++)
            for (ULONG x = 0; x < o->width; x++) ortg_put(o, (LONG)x, (LONG)y, v);
        return;
    }
    old_setrast(rp, pen, g);
}

/* -- lines and pixels -- */

static void pixel_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct ink k;
    ink_of((struct pens *)c, bm, &k);
    plot(bm, x0, y0, &k, 1);
}

static void line(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct pens p;
    LONG ddx = x1 > x0 ? x1 - x0 : x0 - x1, ddy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    LONG sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = ddx + ddy;
    UWORD pat = rp->LinePtrn;
    int k = 0;
    pens_of(rp, &p);
    p.mode &= ~JAM2;
    for (;;) {
        if (pat == 0xFFFF || ((pat >> (15 - (k & 15))) & 1)) pieces(rp, x0, y0, x0, y0, pixel_piece, &p);
        k++;
        if (x0 == x1 && y0 == y1) break;
        /* both tests on the error before this step: testing the second on the
         * updated one steps y too often and walks past the end for ever */
        LONG e2 = 2 * err;
        if (e2 >= ddy) { err += ddy; x0 += sx; }
        if (e2 <= ddx) { err += ddx; y0 += sy; }
    }
}

/* Lines through OpenGPU: one batch of CLIP and LINE for each visible piece.
 * OpenGPU's LINE and line() above break ties between their two steps
 * differently, so only the lines without ties go there: horizontal,
 * vertical and 45 degree ones, solid, through the whole mask. That is
 * almost every line a window's borders and gadgets draw. */
#define LINE_WORDS 96
struct line_ctx {
    struct OGPUBatch b;
    const struct ortg_bitmap *tgt;
    const struct pens *p;
    LONG x0, y0, x1, y1;
    int ok;
    UBYTE buf[LINE_WORDS * 4];
};

static void line_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct line_ctx *l = c;
    struct ink k;
    if (!l->ok) return;
    if (l->b.words + 6 + 2 + 3 + 5 > LINE_WORDS) {
        if (!ogpu_submit_batch(&l->b)) { l->ok = 0; return; }
        ogpu_batch_init(&l->b, l->buf, LINE_WORDS);
        l->tgt = NULL;
    }
    ink_of(l->p, bm, &k);
    if (l->tgt != bm) { ogpu_target_bitmap(&l->b, 0, bm); ogpu_target(&l->b, 0); l->tgt = bm; }
    ogpu_clip(&l->b, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    ogpu_line(&l->b, l->x0 + dx, l->y0 + dy, l->x1 + dx, l->y1 + dy, k.a, k.mode & COMPLEMENT);
}

/* One line, through OpenGPU when it draws exactly the same, else line(). */
static void any_line(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG adx = x1 > x0 ? x1 - x0 : x0 - x1, ady = y1 > y0 ? y1 - y0 : y0 - y1;
    ULONG n = (ULONG)(adx > ady ? adx : ady) + 1;
    struct ortg_bitmap *o = ortg_of(rp->BitMap);
    struct pens p;
    pens_of(rp, &p);
    p.mode &= ~JAM2;
    if (o && OpenGPUBase && !ogpu_off && rp->LinePtrn == 0xFFFF && (adx == 0 || ady == 0 || adx == ady)
        && (o->bpp == 1 ? p.mask == 0xFF : p.mask != 0) && ogpu_can(OGPU_OP_LINE, o)) {
        struct line_ctx l;
        LONG bx0 = x0 < x1 ? x0 : x1, by0 = y0 < y1 ? y0 : y1;
        ogpu_batch_init(&l.b, l.buf, LINE_WORDS);
        l.tgt = NULL; l.p = &p; l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1; l.ok = 1;
        pieces(rp, bx0, by0, bx0 + adx, by0 + ady, line_piece, &l);
        if (l.ok && (!l.b.words || ogpu_submit_batch(&l.b))) { ortg_stat(ORTG_STAT_LINE, 1, n); return; }
        /* A refused batch: what it drew, line() draws again the same (in
         * COMPLEMENT, it can't be undone, so that one is counted wrong). */
    }
    ortg_stat(ORTG_STAT_LINE, 0, n);
    line(rp, x0, y0, x1, y1);
}

static void draw_patch(REG(a1, struct RastPort *rp), REG(d0, WORD x), REG(d1, WORD y), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        any_line(rp, rp->cp_x, rp->cp_y, x, y);
        rp->cp_x = (WORD)x; rp->cp_y = (WORD)y;
        return;
    }
    old_draw(rp, x, y, g);
}

static void polydraw_patch(REG(a1, struct RastPort *rp), REG(d0, WORD n), REG(a0, WORD *xy), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        for (LONG i = 0; i < n; i++) {
            any_line(rp, rp->cp_x, rp->cp_y, xy[2 * i], xy[2 * i + 1]);
            rp->cp_x = xy[2 * i]; rp->cp_y = xy[2 * i + 1];
        }
        return;
    }
    old_polydraw(rp, n, xy, g);
}

static LONG writepixel_patch(REG(a1, struct RastPort *rp), REG(d0, WORD x), REG(d1, WORD y), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct pens p;
        pens_of(rp, &p);
        p.mode &= ~JAM2;
        ortg_stat(ORTG_STAT_PIXEL, 0, 1);
        pieces(rp, x, y, x, y, pixel_piece, &p);
        return 0;
    }
    return old_writepixel(rp, x, y, g);
}

struct read_ctx { LONG v; };
static void read_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    ((struct read_ctx *)c)->v = ortg_px_pen(bm, ortg_get(bm, x0, y0));
}

static ULONG readpixel_patch(REG(a1, struct RastPort *rp), REG(d0, WORD x), REG(d1, WORD y), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct read_ctx r = { -1 };
        ortg_stat(ORTG_STAT_PIXEL, 0, 1);
        pieces(rp, x, y, x, y, read_piece, &r);
        return (ULONG)r.v;
    }
    return old_readpixel(rp, x, y, g);
}

/* -- templates and text -- */

static void blttemplate_patch(REG(a0, PLANEPTR src), REG(d0, WORD sx), REG(d1, LONG mod), REG(a1, struct RastPort *rp), REG(d2, WORD x), REG(d3, WORD y),
                              REG(d4, WORD w), REG(d5, WORD h), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct tmpl_ctx t;
        pens_of(rp, &t.p);
        t.src = src; t.src_x = sx; t.mod = mod; t.at_x = x; t.at_y = y; t.ob = NULL;
        pieces(rp, x, y, x + w - 1, y + h - 1, tmpl_piece, &t);
        return;
    }
    old_blttemplate(src, sx, mod, rp, x, y, w, h, g);
}

/* A ROM font's glyphs, copied to RAM once (fonts in ROM stay for good), so
 * a host back end reaches them; a font in RAM as it is. */
#define FONT_CACHE 8
static struct { const UBYTE *rom; UBYTE *ram; } font_cache[FONT_CACHE];

static const UBYTE *font_data(struct TextFont *f)
{
    const UBYTE *src = f->tf_CharData;
    ULONG bytes = (ULONG)f->tf_Modulo * f->tf_YSize;
    UBYTE *copy;
    int i;
    if (!src || TypeOfMem((APTR)src) || !bytes || bytes > 65536) return src;
    for (i = 0; i < FONT_CACHE; i++) if (font_cache[i].rom == src) return font_cache[i].ram;
    if (!(copy = AllocVec(bytes, MEMF_ANY))) return src;
    CopyMem((APTR)src, copy, bytes);
    Forbid();
    for (i = 0; i < FONT_CACHE; i++) {
        if (font_cache[i].rom == src) { Permit(); FreeVec(copy); return font_cache[i].ram; }
        if (!font_cache[i].rom) { font_cache[i].rom = src; font_cache[i].ram = copy; Permit(); return copy; }
    }
    Permit();
    FreeVec(copy);
    return src;
}

static LONG text_patch(REG(a1, struct RastPort *rp), REG(a0, STRPTR s), REG(d0, WORD n), REG(a6, struct GfxBase *g))
{
    struct TextFont *f = rp->Font;
    if (!ortg_is(rp->BitMap) || !f) return old_text(rp, s, n, g);
    {
        UWORD *loc = f->tf_CharLoc;
        WORD *space = f->tf_CharSpace, *kern = f->tf_CharKern;
        LONG x = rp->cp_x, top = rp->cp_y - f->tf_Baseline, total = 0, i;
        int prop = (f->tf_Flags & FPF_PROPORTIONAL) && space;
        struct tmpl_ctx t;
        struct obatch ob;
        UBYTE style = rp->AlgoStyle;
        pens_of(rp, &t.p);
        ob_init(&ob);
        /* the advance of the whole string, for JAM2's background */
        for (i = 0; i < (LONG)n; i++) {
            UBYTE c = (UBYTE)s[i];
            int k = (c >= f->tf_LoChar && c <= f->tf_HiChar) ? c - f->tf_LoChar : f->tf_HiChar - f->tf_LoChar + 1;
            total += (prop ? space[k] : f->tf_XSize) + rp->TxSpacing;
            if (style & FSF_BOLD) total += f->tf_BoldSmear;
        }
        if (t.p.mode & JAM2 && !(t.p.mode & COMPLEMENT)) {
            struct fill_ctx bg;
            bg.p = t.p; bg.p.a = t.p.b; bg.p.mode = JAM1; bg.ptrn = NULL; bg.ptsz = 1;
            pieces(rp, x, top, x + total - 1, top + f->tf_YSize - 1, fill_piece, &bg);
        }
        t.p.mode &= ~JAM2;
        t.src = font_data(f); t.mod = f->tf_Modulo; t.ob = OpenGPUBase && !ogpu_off ? &ob : NULL;
        for (i = 0; i < (LONG)n; i++) {
            UBYTE c = (UBYTE)s[i];
            int k = (c >= f->tf_LoChar && c <= f->tf_HiChar) ? c - f->tf_LoChar : f->tf_HiChar - f->tf_LoChar + 1;
            LONG gx = x + (kern ? kern[k] : 0), w = loc[2 * k + 1];
            if (w > 0) {
                t.src_x = loc[2 * k]; t.at_x = gx; t.at_y = top;
                pieces(rp, gx, top, gx + w - 1, top + f->tf_YSize - 1, tmpl_piece, &t);
                if (style & FSF_BOLD) {
                    t.at_x = gx + f->tf_BoldSmear;
                    pieces(rp, t.at_x, top, t.at_x + w - 1, top + f->tf_YSize - 1, tmpl_piece, &t);
                }
            }
            x += (prop ? space[k] : f->tf_XSize) + rp->TxSpacing + ((style & FSF_BOLD) ? f->tf_BoldSmear : 0);
        }
        ob_flush(t.ob);
        if (style & FSF_UNDERLINED) {
            struct fill_ctx u;
            u.p = t.p; u.ptrn = NULL; u.ptsz = 1;
            pieces(rp, rp->cp_x, rp->cp_y + 1, x - 1, rp->cp_y + 1, fill_piece, &u);
        }
        rp->cp_x = (WORD)x;
    }
    return 0;
}

/* -- blits -- */

static LONG bltbitmap_patch(REG(a0, struct BitMap *src), REG(d0, WORD sx), REG(d1, WORD sy), REG(a1, struct BitMap *dst), REG(d2, WORD dx), REG(d3, WORD dy),
                            REG(d4, WORD w), REG(d5, WORD h), REG(d6, ULONG m), REG(d7, ULONG mask), REG(a2, PLANEPTR tmp), REG(a6, struct GfxBase *g))
{
    if (ortg_is(src) || ortg_is(dst)) {
        trace_big(ortg_is(dst) ? "ortg: BltBitMap to ours" : "ortg: BltBitMap from ours", sx, sy, dx, dy, w, h, (ULONG)src);
        if (w * h >= 100000 && src) {
            serx("  src bpr/rows ", (ULONG)src->BytesPerRow << 16 | src->Rows); serx("  flags/depth ", (ULONG)src->Flags << 8 | src->Depth);
            serx("  pad ", src->pad); serx("  plane0 ", (ULONG)src->Planes[0]); serx("  plane1 ", (ULONG)src->Planes[1]); serx("  plane7 ", (ULONG)src->Planes[7]);
            serx("  ours ", (ULONG)ortg_of(src));
        }
        if (w > 0 && h > 0) blit(src, sx, sy, dst, dx, dy, w, h, (UBYTE)m, (UBYTE)mask, NULL, 0, 0, 0);
        return 8;
    }
    return old_bltbitmap(src, sx, sy, dst, dx, dy, w, h, m, mask, tmp, g);
}

static void bltbmrp_patch(REG(a0, struct BitMap *src), REG(d0, WORD sx), REG(d1, WORD sy), REG(a1, struct RastPort *rp), REG(d2, WORD x), REG(d3, WORD y),
                          REG(d4, WORD w), REG(d5, WORD h), REG(d6, ULONG m), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct blit_ctx b = { src, sx, sy, x, y, (UBYTE)m, rp->Mask, NULL, 0 };
        trace_big("ortg: BltBitMapRastPort", sx, sy, x, y, w, h, (ULONG)src);
        pieces(rp, x, y, x + w - 1, y + h - 1, blit_piece, &b);
        return;
    }
    old_bltbmrp(src, sx, sy, rp, x, y, w, h, m, g);
}

static void bltmaskbmrp_patch(REG(a0, struct BitMap *src), REG(d0, WORD sx), REG(d1, WORD sy), REG(a1, struct RastPort *rp), REG(d2, WORD x), REG(d3, WORD y),
                              REG(d4, WORD w), REG(d5, WORD h), REG(d6, ULONG m), REG(a2, PLANEPTR mask), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        /* the mask is one plane of the source's size: where it is set, the source */
        LONG mod = ortg_of(src) ? (ortg_of(src)->width + 15) / 16 * 2 : src->BytesPerRow;
        struct blit_ctx b = { src, sx, sy, x, y, (UBYTE)m, rp->Mask, mask, mod };
        pieces(rp, x, y, x + w - 1, y + h - 1, blit_piece, &b);
        return;
    }
    old_bltmaskbmrp(src, sx, sy, rp, x, y, w, h, m, mask, g);
}

static void clipblit_patch(REG(a0, struct RastPort *srp), REG(d0, WORD sx), REG(d1, WORD sy), REG(a1, struct RastPort *rp), REG(d2, WORD x), REG(d3, WORD y),
                           REG(d4, WORD w), REG(d5, WORD h), REG(d6, ULONG m), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap) || ortg_is(srp->BitMap)) {
        LONG ox = 0, oy = 0;
        trace_big("ortg: ClipBlit", sx, sy, x, y, w, h, (ULONG)srp->BitMap);
        if (srp->Layer) { ox = srp->Layer->bounds.MinX - srp->Layer->Scroll_X; oy = srp->Layer->bounds.MinY - srp->Layer->Scroll_Y; }
        if (ortg_is(rp->BitMap)) {
            struct blit_ctx b = { srp->BitMap, sx + ox, sy + oy, x, y, (UBYTE)m, rp->Mask, NULL, 0 };
            pieces(rp, x, y, x + w - 1, y + h - 1, blit_piece, &b);
        } else {
            LONG dx = 0, dy = 0;
            if (rp->Layer) { dx = rp->Layer->bounds.MinX - rp->Layer->Scroll_X; dy = rp->Layer->bounds.MinY - rp->Layer->Scroll_Y; }
            blit(srp->BitMap, sx + ox, sy + oy, rp->BitMap, x + dx, y + dy, w, h, (UBYTE)m, rp->Mask, NULL, 0, 0, 0);
        }
        return;
    }
    old_clipblit(srp, sx, sy, rp, x, y, w, h, m, g);
}

/* ScrollRaster: the box's contents move by -dx, -dy; what is uncovered gets
 * the B pen. Within each visible piece of the box. */
struct scroll_ctx { LONG dx, dy; UBYTE pen; };

static void scroll_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG ox, LONG oy)
{
    struct scroll_ctx *s = c;
    LONG w = x1 - x0 + 1, h = y1 - y0 + 1;
    LONG mw = w - (s->dx < 0 ? -s->dx : s->dx), mh = h - (s->dy < 0 ? -s->dy : s->dy);
    if (mw > 0 && mh > 0) {
        LONG sx = x0 + (s->dx > 0 ? s->dx : 0), sy = y0 + (s->dy > 0 ? s->dy : 0);
        LONG tx = x0 + (s->dx < 0 ? -s->dx : 0), ty = y0 + (s->dy < 0 ? -s->dy : 0);
        blit(&bm->bm, sx, sy, &bm->bm, tx, ty, mw, mh, 0xC0, 0xFF, NULL, 0, 0, 0);
    }
    ULONG v = ortg_pen_px(bm, s->pen);
    {
        /* What the move uncovered: whole rows, then the side band of the rest. */
        LONG ry0 = y0, ry1 = y1, cy0 = y0, cy1 = y1, cx0 = 0, cx1 = -1;
        int ok;
        if (mw <= 0 || mh <= 0) { cy0 = 1; cy1 = 0; }                 /* all of it: one fill */
        else {
            if (s->dy > 0) { ry0 = y1 - s->dy + 1; cy1 = ry0 - 1; }
            else if (s->dy < 0) { ry1 = y0 - s->dy - 1; cy0 = ry1 + 1; }
            else { ry0 = 1; ry1 = 0; }
            if (s->dx > 0) { cx0 = x1 - s->dx + 1; cx1 = x1; }
            else if (s->dx < 0) { cx0 = x0; cx1 = x0 - s->dx - 1; }
        }
        ok = ortg_ogpu_fill(bm, x0, ry0, x1, ry1, v) && (cx1 < cx0 || ortg_ogpu_fill(bm, cx0, cy0, cx1, cy1, v));
        if (ok) {
            ortg_stat(ORTG_STAT_SCROLL, 1, (ry1 >= ry0 ? area(x0, ry0, x1, ry1) : 0) + (cx1 >= cx0 && cy1 >= cy0 ? area(cx0, cy0, cx1, cy1) : 0));
            return;
        }
        ortg_stat(ORTG_STAT_SCROLL, 0, 0);
    }
    for (LONG y = y0; y <= y1; y++) {
        int ybar = (s->dy > 0 && y > y1 - s->dy) || (s->dy < 0 && y < y0 - s->dy) || mh <= 0;
        for (LONG x = x0; x <= x1; x++)
            if (ybar || mw <= 0 || (s->dx > 0 && x > x1 - s->dx) || (s->dx < 0 && x < x0 - s->dx)) ortg_put(bm, x, y, v);
    }
}

static void scroll_patch(REG(a1, struct RastPort *rp), REG(d0, WORD dx), REG(d1, WORD dy), REG(d2, WORD x0), REG(d3, WORD y0), REG(d4, WORD x1), REG(d5, WORD y1), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct scroll_ctx s = { dx, dy, rp->BgPen };
        pieces(rp, x0, y0, x1, y1, scroll_piece, &s);
        return;
    }
    old_scroll(rp, dx, dy, x0, y0, x1, y1, g);
}

/* -- chunky arrays -- */

struct array_ctx { UBYTE *a; LONG bpr, at_x, at_y; };
static void array_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct array_ctx *a = c;
    {
        const UBYTE *src = a->a + (y0 - dy - a->at_y) * a->bpr + (x0 - dx - a->at_x);
        int ok = a->bpr > 0 && (bm->bpp == 1 ? ortg_ogpu_pixels(bm, x0, y0, x1, y1, src, (ULONG)a->bpr, OGPU_FMT_CLUT8, NULL)
                                             : ortg_ogpu_pixels(bm, x0, y0, x1, y1, src, (ULONG)a->bpr, OGPU_FMT_INDEX8, ortg_pen_rgb[bm->pal_index]));
        ortg_stat(ORTG_STAT_CHUNKY, ok, area(x0, y0, x1, y1));
        if (ok) return;
    }
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *s = a->a + (y - dy - a->at_y) * a->bpr + (x0 - dx - a->at_x);
        if (bm->bpp == 1) { UBYTE *d = bm->mem + y * bm->stride + x0; for (LONG x = x0; x <= x1; x++) *d++ = *s++; }
        else for (LONG x = x0; x <= x1; x++) ortg_put(bm, x, y, ortg_pen_px(bm, *s++));
    }
}

static void wcp_patch(REG(a0, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, WORD x1), REG(d3, WORD y1), REG(a2, UBYTE *a), REG(d4, LONG bpr), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct array_ctx c = { a, bpr, (LONG)x0, (LONG)y0 };
        pieces(rp, x0, y0, x1, y1, array_piece, &c);
        return;
    }
    old_wcp(rp, x0, y0, x1, y1, a, bpr, g);
}

static LONG wpa8_patch(REG(a0, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, WORD x1), REG(d3, WORD y1), REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        /* WritePixelArray8's rows are rounded up to 16 pixels */
        struct array_ctx c = { a, (LONG)((x1 - x0 + 16) & ~15UL), (LONG)x0, (LONG)y0 };
        pieces(rp, x0, y0, x1, y1, array_piece, &c);
        return (LONG)((x1 - x0 + 1) * (y1 - y0 + 1));
    }
    return old_wpa8(rp, x0, y0, x1, y1, a, tmp, g);
}

/* Reading pixels into an array: what isn't visible (and kept nowhere) stays as it was. */
static void array_read_piece(void *c, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct array_ctx *a = c;
    ortg_stat(ORTG_STAT_CHUNKY_READ, 0, area(x0, y0, x1, y1));
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *d = a->a + (y - dy - a->at_y) * a->bpr + (x0 - dx - a->at_x);
        if (bm->bpp == 1) { UBYTE *s = bm->mem + y * bm->stride + x0; for (LONG x = x0; x <= x1; x++) *d++ = *s++; }
        else for (LONG x = x0; x <= x1; x++) *d++ = ortg_px_pen(bm, ortg_get(bm, x, y));
    }
}

/* The line calls: graphics' own write the line as planes, through the
 * temporary RastPort (5 Oct 2026: picture.datatype draws Workbench's
 * backdrop with WritePixelLine8, and on a chunky bitmap that left a stripe
 * of planes 100 bytes wide). */
static LONG wpl8_patch(REG(a0, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, UWORD w), REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct array_ctx c = { a, (LONG)w, (LONG)x0, (LONG)y0 };
        if (w) pieces(rp, x0, y0, x0 + w - 1, y0, array_piece, &c);
        return (LONG)w;
    }
    return old_wpl8(rp, x0, y0, w, a, tmp, g);
}

static LONG rpl8_patch(REG(a0, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, UWORD w), REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        struct array_ctx c = { a, (LONG)w, (LONG)x0, (LONG)y0 };
        if (w) pieces(rp, x0, y0, x0 + w - 1, y0, array_read_piece, &c);
        return (LONG)w;
    }
    return old_rpl8(rp, x0, y0, w, a, tmp, g);
}

static LONG rpa8_patch(REG(a0, struct RastPort *rp), REG(d0, WORD x0), REG(d1, WORD y0), REG(d2, WORD x1), REG(d3, WORD y1), REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *g))
{
    if (ortg_is(rp->BitMap)) {
        /* ReadPixelArray8's rows are rounded up to 16 pixels, as WritePixelArray8's */
        struct array_ctx c = { a, (LONG)((x1 - x0 + 16) & ~15UL), (LONG)x0, (LONG)y0 };
        pieces(rp, x0, y0, x1, y1, array_read_piece, &c);
        return (LONG)((x1 - x0 + 1) * (y1 - y0 + 1));
    }
    return old_rpa8(rp, x0, y0, x1, y1, a, tmp, g);
}

/* -- bitmaps -- */

/* The mode an OpenRTG screen bitmap is for, from AllocBitMap's tags. */
static const struct ortg_mode *mode_of(ULONG id, int *monitor)
{
    int n = (int)((id >> 24) - 0x60);
    if (!(id & 0x1000) || (id >> 28) != 6 || n < 1 || n > ORTG_MAX_MONITORS || !tables || !tables[n] || !board[n]) return NULL;
    *monitor = n;
    return ortg_find_mode(tables[n], id);
}

static struct BitMap *allocbm_patch(REG(d0, WORD w), REG(d1, WORD h), REG(d2, ULONG depth), REG(d3, ULONG flags), REG(a0, struct BitMap *friend), REG(a6, struct GfxBase *g))
{
    struct BitMap *fr = friend;
    if (BITMAPFLAGS_ARE_EXTENDED(flags)) {
        /* OS 3.2's intuition asks for a screen's bitmap with tags: its
         * ModeID says whether it is an OpenRTG screen */
        struct TagItem *tags = (struct TagItem *)friend;
        int n = 0;
        const struct ortg_mode *m = mode_of(GetTagData(BMATags_DisplayID, INVALID_ID, tags), &n);
        if (m) {
            /* the mode's own format, whatever depth intuition asks for */
            struct ortg_bitmap *o = ortg_alloc(n, (UWORD)w, (UWORD)h, 1, m->format);
            serx("ortg: screen bitmap for mode ", m->mode_id);
            if (o && o->monitor) return &o->bm;
            if (o) ortg_free(o);
        }
        fr = (struct BitMap *)GetTagData(BMATags_Friend, 0, tags);
    }
    /* a bitmap like an OpenRTG one (layers' backing store, double buffers,
     * a program's off-screen picture) is one, in fast RAM */
    if (ortg_is(fr)) {
        const struct ortg_bitmap *f = ortg_of(fr);
        /* a friend of a 16 or 32-bit bitmap is one too, whatever the depth asked
         * (as Picasso96 does); of an 8-bit one, when it asks for at most 8 */
        if (f->bpp != 1 || depth <= 8) {
            struct ortg_bitmap *o = ortg_alloc(0, (UWORD)w, (UWORD)h, (flags & BMF_CLEAR) != 0, f->format);
            if (o) { o->pal_index = f->pal_index; return &o->bm; }
        }
    }
    return old_allocbm(w, h, depth, flags, friend, g);
}

static void freebm_patch(REG(a0, struct BitMap *bm), REG(a6, struct GfxBase *g))
{
    if (ortg_is(bm)) { struct ortg_bitmap *o = ortg_of(bm); if (&o->bm == bm) ortg_free(o); return; }
    old_freebm(bm, g);
}

static ULONG bmattr_patch(REG(a0, struct BitMap *bm), REG(d1, ULONG a), REG(a6, struct GfxBase *g))
{
    struct ortg_bitmap *o = ortg_of(bm);
    if (o) {
        switch (a) {
        case BMA_HEIGHT: return o->height;
        case BMA_DEPTH: return o->bpp == 4 ? 24 : o->bpp == 2 ? 16 : 8;
        case BMA_WIDTH: return o->stride / o->bpp;
        case BMA_FLAGS: return 0;               /* not standard: no planar planes */
        default: return 0;
        }
    }
    return old_bmattr(bm, a, g);
}

static struct ExtSprite *pointer;
static void pointer_update(LONG x, LONG y, int image);

/* ---- the display ---------------------------------------------------------------------- */

static struct ortg_bitmap *vp_bitmap(struct ViewPort *vp)
{
    return vp && vp->RasInfo ? ortg_of(vp->RasInfo->BitMap) : NULL;
}

/* A 16 or 32-bit screen's pens, from its palette, into its monitor's table. */
static void pens_from(int index, struct ViewPort *vp)
{
    ULONG rgb[3 * 16], n = vp->ColorMap ? vp->ColorMap->Count : 0;
    if (n > 256) n = 256;
    for (ULONG first = 0; first < n; first += 16) {
        ULONG k = n - first > 16 ? 16 : n - first;
        GetRGB32(vp->ColorMap, first, k, rgb);
        for (ULONG i = 0; i < k; i++)
            ortg_pen_rgb[index][first + i] = (rgb[3 * i] >> 24) << 16 | (rgb[3 * i + 1] >> 24) << 8 | (rgb[3 * i + 2] >> 24);
    }
}

static void palette_to_board(int n, struct ViewPort *vp, ULONG first, ULONG count)
{
    ULONG rgb[3 * 16];
    if (!vp->ColorMap) return;
    while (count) {
        ULONG k = count > 16 ? 16 : count;
        GetRGB32(vp->ColorMap, first, k, rgb);
        for (ULONG i = 0; i < k; i++) {
            reg(n, R_PAL_INDEX, first + i);
            reg(n, R_PAL_RGB, (rgb[3 * i] >> 24) << 16 | (rgb[3 * i + 1] >> 24) << 8 | (rgb[3 * i + 2] >> 24));
        }
        first += k; count -= k;
    }
}

/* The front OpenRTG screen of each monitor is what the board shows. */
static UBYTE switched[ORTG_MAX_MONITORS + 1];

static void show_front(void)
{
    struct ortg_bitmap *want[ORTG_MAX_MONITORS + 1] = { 0 };
    struct ViewPort *wvp[ORTG_MAX_MONITORS + 1] = { 0 };
    struct Screen *s;
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        struct ortg_bitmap *o = vp_bitmap(&s->ViewPort);
        if (o && o->monitor && !want[o->monitor]) { want[o->monitor] = o; wvp[o->monitor] = &s->ViewPort; }
    }
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++) {
        struct ortg_bitmap *o = want[n];
        if (!board[n] || !o || shown[n].bm == o) continue;
        serx("ortg: showing ", (ULONG)o->vram_off);
        if (shown[n].w != o->width || shown[n].h != o->height || shown[n].fmt != o->format) {
            reg(n, R_WIDTH, o->width); reg(n, R_HEIGHT, o->height); reg(n, R_FORMAT, board_fmt[o->format]); reg(n, R_CLOCK, 0);
            reg(n, R_COMMIT, C_MODE);
            shown[n].w = o->width; shown[n].h = o->height; shown[n].fmt = o->format;
        }
        reg(n, R_ARG_A, o->vram_off); reg(n, R_ARG_B, o->stride); reg(n, R_PAN_X, 0); reg(n, R_PAN_Y, 0);
        reg(n, R_COMMIT, C_PAN);
        if (o->bpp == 1) palette_to_board(n, wvp[n], 0, 256);
        else pens_from(o->pal_index, wvp[n]);
        reg(n, R_ARG_A, 1);
        reg(n, R_COMMIT, C_DISPLAY);
        shown[n].bm = o;
    }
    /* the switch, as Picasso96 sets it: on while an OpenRTG screen is in
     * front, so the instance window shows the board's picture, and off when
     * a chipset screen (a game, say) comes to the front (6 Oct 2026: without
     * it the window kept showing the chipset's empty picture) */
    {
        struct Screen *f = IntuitionBase->FirstScreen;
        struct ortg_bitmap *fo = f ? vp_bitmap(&f->ViewPort) : NULL;
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++) {
            UBYTE sw = fo && fo->monitor == n && shown[n].bm;
            if (board[n] && sw != switched[n]) { reg(n, R_SWITCH, sw); switched[n] = sw; }
        }
    }
    if (pointer) pointer_update(pointer->es_SimpleSprite.x, pointer->es_SimpleSprite.y, 0);
}

/* graphics has no copper list to make for an OpenRTG viewport: the board shows it */
static ULONG makevp_patch(REG(a0, struct View *v), REG(a1, struct ViewPort *vp), REG(a6, struct GfxBase *g))
{
    struct ortg_bitmap *o = vp_bitmap(vp);
    if (o) {
        /* the whole picture is shown: intuition's clip arithmetic, made for
         * the chipset's beam, can leave the height at 0 on a board's mode */
        if (!vp->DWidth || vp->DWidth > o->width) vp->DWidth = o->width;
        if (!vp->DHeight || vp->DHeight > o->height) vp->DHeight = o->height;
        serx("ortg: MakeVPort, height ", vp->DHeight);
        return MVP_OK;
    }
    return old_makevp(v, vp, g);
}

static ULONG mrgcop_patch(REG(a1, struct View *v), REG(a6, struct GfxBase *g))
{
    /* OpenRTG's viewports have no copper lists (MakeVPort made none), so the
     * chipset's merge passes them by. With only OpenRTG viewports there is
     * nothing to merge, but the view is good and intuition must still load
     * it (it loads none for MCOP_NOP, and its mouse then has no view). */
    struct ViewPort *vp;
    int ours = 0;
    ULONG r;
    for (vp = v ? v->ViewPort : NULL; vp; vp = vp->Next)
        if (vp_bitmap(vp)) ours = 1;
    r = old_mrgcop(v, g);
    { static int t; if (t < 10) { t++; serx("ortg: MrgCop view ", (ULONG)v); serx("  result ", r); } }
    if (r == MCOP_NOP && ours) r = MCOP_OK;
    return r;
}

static void loadview_patch(REG(a1, struct View *v), REG(a6, struct GfxBase *g))
{
    { static int t; if (t < 40) { t++; serx("ortg: LoadView ", (ULONG)v); serx("  lof ", v ? (ULONG)v->LOFCprList : 0); serx("  first vp ", v ? (ULONG)v->ViewPort : 0); } }
    if (v && !v->LOFCprList && !v->SHFCprList) {
        /* a view with only OpenRTG viewports: the board shows them; the
         * chipset shows nothing, and the view is the active one */
        old_loadview(NULL, g);
        GfxBase->ActiView = v;
    } else
        old_loadview(v, g);
    show_front();
}

volatile ULONG ortg_palette_gen;

static void palette_changed(struct ViewPort *vp, ULONG first, ULONG count)
{
    struct ortg_bitmap *o = vp_bitmap(vp);
    ortg_palette_gen++;
    if (!o) return;
    if (o->bpp != 1) {
        /* the pens of a 16 or 32-bit screen: what is drawn from now on;
         * what is drawn already keeps its colours, as on any true-colour screen */
        if (!o->monitor || shown[o->monitor].bm == o || !shown[o->monitor].bm) pens_from(o->pal_index, vp);
    } else if (o->monitor && shown[o->monitor].bm == o) palette_to_board(o->monitor, vp, first, count);
}

static void loadrgb32_patch(REG(a0, struct ViewPort *vp), REG(a1, ULONG *t), REG(a6, struct GfxBase *g))
{
    old_loadrgb32(vp, t, g);
    if (vp_bitmap(vp)) palette_changed(vp, 0, 256);
}

static void setrgb32_patch(REG(a0, struct ViewPort *vp), REG(d0, ULONG n), REG(d1, ULONG r), REG(d2, ULONG gg), REG(d3, ULONG b), REG(a6, struct GfxBase *g))
{
    old_setrgb32(vp, n, r, gg, b, g);
    if (vp_bitmap(vp)) palette_changed(vp, n, 1);
}

static void loadrgb4_patch(REG(a0, struct ViewPort *vp), REG(a1, UWORD *t), REG(d0, LONG n), REG(a6, struct GfxBase *g))
{
    old_loadrgb4(vp, t, n, g);
    if (vp_bitmap(vp)) palette_changed(vp, 0, (ULONG)n);
}

static void setrgb4_patch(REG(a0, struct ViewPort *vp), REG(d0, LONG n), REG(d1, ULONG r), REG(d2, ULONG gg), REG(d3, ULONG b), REG(a6, struct GfxBase *g))
{
    old_setrgb4(vp, n, r, gg, b, g);
    if (vp_bitmap(vp)) palette_changed(vp, (ULONG)n, 1);
}

/* ---- the pointer: the board's sprite while an OpenRTG screen is in front ------------- */

/* pointer: intuition's pointer sprite, the last it set */
static int pointer_on;                  /* the monitor whose sprite shows it, or 0 */

/* The OpenRTG monitor of the front screen, or 0 when the front screen is the chipset's. */
static int front_monitor(struct Screen **sp)
{
    struct Screen *s = IntuitionBase->FirstScreen;
    struct ortg_bitmap *o = s ? vp_bitmap(&s->ViewPort) : NULL;
    if (sp) *sp = s;
    return o && o->monitor && shown[o->monitor].bm == o ? o->monitor : 0;
}

/* The sprite's picture: posctldata as the chipset reads it (control words,
 * then each row's plane 0 and plane 1 words), es_wordwidth words a plane. */
static void pointer_to_board(int n, struct Screen *s)
{
    struct SimpleSprite *ss = &pointer->es_SimpleSprite;
    UWORD ww = pointer->es_wordwidth ? pointer->es_wordwidth : 1, h = ss->height;
    UWORD *d = ss->posctldata;
    ULONG rgb[9];
    if (!d || !h) return;
    if (ww > 4) ww = 1;
    if (h > 64) h = 64;
    reg(n, R_SPR_SIZE, (ULONG)(ww * 16) << 16 | h);
    for (UWORD y = 0; y < h; y++) {
        UWORD *row = d + 2 * ww + y * 2 * ww;
        for (UWORD k = 0; k < ww; k++) reg(n, R_SPR_DATA, (ULONG)row[k] << 16 | row[ww + k]);
    }
    if (s->ViewPort.ColorMap) {
        GetRGB32(s->ViewPort.ColorMap, 17, 3, rgb);
        for (int i = 0; i < 3; i++) reg(n, R_SPR_COLOR, (ULONG)i << 24 | (rgb[3 * i] >> 24) << 16 | (rgb[3 * i + 1] >> 24) << 8 | (rgb[3 * i + 2] >> 24));
    }
    reg(n, R_SPR_HOT, 0);
}

static void pointer_update(LONG x, LONG y, int image)
{
    struct Screen *s;
    int n = front_monitor(&s);
    if (pointer_on && pointer_on != n) { reg(pointer_on, R_SPR_CTRL, 0); pointer_on = 0; }
    if (!n || !pointer) return;
    if (image || pointer_on != n) pointer_to_board(n, s);
    reg(n, R_SPR_X, (ULONG)x);
    reg(n, R_SPR_Y, (ULONG)y);
    if (pointer_on != n) { reg(n, R_SPR_CTRL, 1); pointer_on = n; }
}

static void movesprite_patch(REG(a0, struct ViewPort *vp), REG(a1, struct SimpleSprite *sp), REG(d0, WORD x), REG(d1, WORD y), REG(a6, struct GfxBase *g))
{
    old_movesprite(vp, sp, x, y, g);
    {
        static int traced;
        static WORD lx = -999, ly = -999;
        if (traced < 60 && pointer && sp == &pointer->es_SimpleSprite && IntuitionBase->FirstScreen && (x != lx || y != ly)) {
            traced++; lx = x; ly = y;
            serx("ortg: MoveSprite x ", (ULONG)x); serx("  y ", (ULONG)y); serx("  vp ", (ULONG)vp);
            serx("  mouse x ", (ULONG)IntuitionBase->FirstScreen->MouseX); serx("  mouse y ", (ULONG)IntuitionBase->FirstScreen->MouseY);
        }
    }
    if (pointer && sp == &pointer->es_SimpleSprite) pointer_update(x, y, 0);
}

static LONG changeext_patch(REG(a0, struct ViewPort *vp), REG(a1, struct ExtSprite *olds), REG(a2, struct ExtSprite *news), REG(a3, struct TagItem *tags), REG(a6, struct GfxBase *g))
{
    LONG r = old_changeext(vp, olds, news, tags, g);
    if (news && news->es_SimpleSprite.num == 0) {          /* sprite 0: intuition's pointer */
        pointer = news;
        pointer_update(news->es_SimpleSprite.x, news->es_SimpleSprite.y, 1);
    }
    return r;
}

/* ---- the viewports' size ----------------------------------------------------------------

   Intuition sizes a screen's viewport from the chipset's display window;
   for a board's mode it leaves the height 0, and the mouse then has no room
   on the screen. OpenRTG's screens are shown whole, so their viewports are
   their bitmaps' size, put right after each of intuition's remakes. */

static void fix_viewports(void)
{
    struct Screen *s;
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        struct ortg_bitmap *o = vp_bitmap(&s->ViewPort);
        if (!o) continue;
        if (s->ViewPort.DWidth != (WORD)s->Width) s->ViewPort.DWidth = s->Width;
        if (s->ViewPort.DHeight != (WORD)s->Height) s->ViewPort.DHeight = s->Height;
        if (s == IntuitionBase->FirstScreen) s->ViewPort.Modes &= ~VP_HIDE;   /* hidden only for the 0 height */
        if (s->ViewPort.ColorMap && s->ViewPort.ColorMap->cm_vpe) {
            /* the display clip intuition worked out from the chipset's beam: 0 */
            struct Rectangle *dc = &s->ViewPort.ColorMap->cm_vpe->DisplayClip;
            if (dc->MaxX <= dc->MinX || dc->MaxY <= dc->MinY) {
                dc->MinX = 0; dc->MinY = 0; dc->MaxX = o->width - 1; dc->MaxY = o->height - 1;
            }
        }
    }
}

static void rethink_patch(REG(a6, struct IntuitionBase *ib))
{
    fix_viewports();
    old_rethink(ib);
    fix_viewports();
}

static void remake_patch(REG(a6, struct IntuitionBase *ib))
{
    fix_viewports();
    old_remake(ib);
    fix_viewports();
}

static LONG makescreen_patch(REG(a0, struct Screen *s), REG(a6, struct IntuitionBase *ib))
{
    LONG r;
    fix_viewports();
    r = old_makescreen(s, ib);
    fix_viewports();
    return r;
}

/* ---- the mouse on an OpenRTG screen ---------------------------------------------------

   Intuition scales the mouse's counts by the chipset's beam, which a board's
   mode doesn't have: on an OpenRTG screen a count would jump the pointer to
   its limits. So while an OpenRTG screen is in front, an input handler ahead
   of intuition's keeps the pointer's position itself, in the screen's pixels,
   and hands intuition that position (IECLASS_NEWPOINTERPOS, IESUBCLASS_PIXEL),
   which needs no scaling. Buttons pass through as they are. */

static struct Interrupt mouse_irq;
static struct IEPointerPixel mouse_pp;
static WORD mouse_x, mouse_y;
static struct Screen *mouse_screen;

static struct InputEvent *__attribute__((used)) mouse_events(struct InputEvent *ev)
{
    struct Screen *s;
    int n = front_monitor(&s);
    struct InputEvent *e;
    {
        static int traced;
        if (traced < 6) { traced++; serx("ortg: input, monitor ", (ULONG)n); }
    }
    {
        static int tr3;
        struct InputEvent *t;
        for (t = ev; t && tr3 < 30; t = t->ie_NextEvent)
            if (t->ie_Class != IECLASS_TIMER) { tr3++; serx("ortg: event class/code ", (ULONG)t->ie_Class << 16 | t->ie_Code); serx("  qual/xy ", (ULONG)t->ie_Qualifier << 16 | (UWORD)t->ie_X); serx("  n ", (ULONG)n); }
    }
    if (!n || !s) { mouse_screen = NULL; return ev; }
    if (s != mouse_screen) { mouse_screen = s; mouse_x = s->Width / 2; mouse_y = s->Height / 2; }   /* TEMP: from the centre */
    for (e = ev; e; e = e->ie_NextEvent) {
        if (e->ie_Class != IECLASS_RAWMOUSE || (e->ie_Qualifier & IEQUALIFIER_RELATIVEMOUSE) == 0) continue;
        if (!e->ie_X && !e->ie_Y) continue;
        mouse_x += e->ie_X; mouse_y += e->ie_Y;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= s->Width) mouse_x = s->Width - 1;
        if (mouse_y >= s->Height) mouse_y = s->Height - 1;
        e->ie_X = e->ie_Y = 0;
        {
            static int tr2;
            if (tr2 < 8) { tr2++; serx("ortg: mouse to ", (ULONG)mouse_x << 16 | (UWORD)mouse_y); serx("  screen mouse was ", (ULONG)s->MouseX << 16 | (UWORD)s->MouseY); }
        }
        if (e->ie_Code == IECODE_NOBUTTON) {
            /* this event becomes the new position */
            mouse_pp.iepp_Screen = s;
            mouse_pp.iepp_Position.X = mouse_x;
            mouse_pp.iepp_Position.Y = mouse_y;
            e->ie_Class = IECLASS_NEWPOINTERPOS;
            e->ie_SubClass = IESUBCLASS_PIXEL;
            e->ie_EventAddress = &mouse_pp;
        }
        if (pointer && n) { reg(n, R_SPR_X, (ULONG)mouse_x); reg(n, R_SPR_Y, (ULONG)mouse_y); }
    }
    return ev;
}

__asm(
"_mouse_entry:\n"
"   move.l a0,-(sp)\n"
"   jsr _mouse_events\n"
"   addq.l #4,sp\n"
"   rts\n");
void mouse_entry(void);

static void __attribute__((unused)) mouse_on(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof *io) : NULL;
    if (io && !OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)io, 0)) {
        mouse_irq.is_Node.ln_Type = NT_INTERRUPT;
        mouse_irq.is_Node.ln_Pri = 127;   /* TEMP: see every event */
        mouse_irq.is_Node.ln_Name = (char *)"OpenRTG mouse";
        mouse_irq.is_Code = (void (*)())mouse_entry;
        io->io_Command = IND_ADDHANDLER;
        io->io_Data = &mouse_irq;
        DoIO((struct IORequest *)io);
        CloseDevice((struct IORequest *)io);
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
}

/* ---- screens ---------------------------------------------------------------------------- */

static ULONG tag_or(struct TagItem *tags, ULONG tag, ULONG def)
{
    return tags ? GetTagData(tag, def, tags) : def;
}

static struct Screen *openscreen_patch(REG(a0, struct NewScreen *ns), REG(a1, struct TagItem *tags), REG(a6, struct IntuitionBase *ib))
{
    struct TagItem *ext = (ns && (ns->Type & NS_EXTENDED)) ? ((struct ExtNewScreen *)ns)->Extension : NULL;
    ULONG id = tag_or(tags, SA_DisplayID, tag_or(ext, SA_DisplayID, ns ? ns->ViewModes : 0));
    const struct ortg_mode *m = NULL;
    int n = (int)((id >> 24) - 0x60);
    if ((id & 0x1000) && (id >> 28) == 6 && n >= 1 && n <= ORTG_MAX_MONITORS && tables && tables[n]) m = ortg_find_mode(tables[n], id);
    serx("ortg: OpenScreen, mode ", id);
    if (m && m->format == ORTG_CLUT8 && board[n] && !tag_or(tags, SA_BitMap, tag_or(ext, SA_BitMap, 0))) {
        struct ortg_bitmap *o = ortg_alloc(n, m->width, m->height, 1, ORTG_CLUT8);
        serx("ortg: bitmap at ", (ULONG)(o ? o->mem : 0));
        if (o && o->monitor) {
            struct TagItem more[4];
            struct Screen *s;
            more[0].ti_Tag = SA_BitMap; more[0].ti_Data = (ULONG)&o->bm;
            more[1].ti_Tag = SA_Width; more[1].ti_Data = m->width;
            more[2].ti_Tag = SA_Height; more[2].ti_Data = m->height;
            more[3].ti_Tag = tags ? TAG_MORE : TAG_DONE; more[3].ti_Data = (ULONG)tags;
            s = old_openscreen(ns, more, ib);
            serx("ortg: screen ", (ULONG)s);
            if (s) { o->screen = s; fix_viewports(); RethinkDisplay(); show_front(); return s; }
        }
        ortg_free(o);
        return NULL;
    }
    {
        struct Screen *sc = old_openscreen(ns, tags, ib);
        if (sc && vp_bitmap(&sc->ViewPort)) {           /* intuition's own screen bitmap, from the tags */
            serx("ortg: screen view height was ", sc->ViewPort.DHeight);
            fix_viewports();
            RethinkDisplay();
            show_front();
        }
        return sc;
    }
}

static BOOL closescreen_patch(REG(a0, struct Screen *s), REG(a6, struct IntuitionBase *ib))
{
    struct ortg_bitmap *o = s ? vp_bitmap(&s->ViewPort) : NULL;
    int n = o ? o->monitor : 0, mine = o && o->screen == s;
    BOOL ok = old_closescreen(s, ib);
    if (ok && o) {
        if (mine) ortg_free(o);                         /* intuition frees the others itself */
        if (n) {
            shown[n].bm = NULL;
            show_front();
            if (!shown[n].bm) { reg(n, R_ARG_A, 0); reg(n, R_COMMIT, C_DISPLAY); reg(n, R_SWITCH, 0); switched[n] = 0; }
        }
    }
    return ok;
}

/* ---- OpenGfx handoff ---------------------------------------------------------------------- */

/* These callbacks preserve OpenRTG's current chunky-bitmap semantics while
 * OpenGfx owns the eight drawing/text graphics.library vectors. A callback
 * returns 0 for a bitmap/call that is not ours, so OpenGfx can use its own
 * native path or chain to the original OS vector. TextLength/TextExtent/
 * TextFit are deliberately left NULL below: OpenRTG does not change font
 * metrics, so graphics.library remains the correct provider until OpenFont
 * supplies shaped metrics through OpenGfx. */
static LONG ogfx_rectfill_provider(APTR userdata, struct ortg_ogfx_rectfill *r)
{
    (void)userdata;
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    rectfill_patch(r->rp, (WORD)r->x0, (WORD)r->y0,
                   (WORD)r->x1, (WORD)r->y1, GfxBase);
    return 1;
}

static LONG ogfx_text_provider(APTR userdata, struct ortg_ogfx_text *r)
{
    (void)userdata;
    if (!r || !r->rp || !r->rp->Font || !ortg_is(r->rp->BitMap)) return 0;
    r->result = text_patch(r->rp, r->text, (WORD)r->length, GfxBase);
    return 1;
}

static LONG ogfx_bltbitmap_provider(APTR userdata, struct ortg_ogfx_bltbitmap *r)
{
    (void)userdata;
    if (!r || (!ortg_is(r->src) && !ortg_is(r->dst))) return 0;
    r->result = bltbitmap_patch(r->src, (WORD)r->sx, (WORD)r->sy,
                                r->dst, (WORD)r->dx, (WORD)r->dy,
                                (WORD)r->width, (WORD)r->height,
                                r->minterm, r->mask, r->temp, GfxBase);
    return 1;
}

static LONG ogfx_scroll_provider(APTR userdata, struct ortg_ogfx_scroll *r)
{
    (void)userdata;
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    scroll_patch(r->rp, (WORD)r->dx, (WORD)r->dy,
                 (WORD)r->x0, (WORD)r->y0,
                 (WORD)r->x1, (WORD)r->y1, GfxBase);
    return 1;
}

static LONG ogfx_blttemplate_provider(APTR userdata, struct ortg_ogfx_blttemplate *r)
{
    (void)userdata;
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    blttemplate_patch(r->source, (WORD)r->sx, r->source_modulo,
                      r->rp, (WORD)r->dx, (WORD)r->dy,
                      (WORD)r->width, (WORD)r->height, GfxBase);
    return 1;
}

/* OpenGfx 1.4 (opengpu.library 0.8): the fourteen other drawing calls.
 * Each takes the call for an OpenRTG bitmap, the same code as the
 * standalone patch above, and says "not mine" (0) for any other, so OpenGfx
 * goes on to graphics.library. */
static LONG ogfx_bltpattern_provider(APTR u, struct ortg_ogfx_bltpattern *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    bltpattern_patch(r->rp, r->mask, (WORD)r->x0, (WORD)r->y0, (WORD)r->x1, (WORD)r->y1, (ULONG)r->mask_bpr, GfxBase);
    return 1;
}

static LONG ogfx_setrast_provider(APTR u, struct ortg_ogfx_setrast *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    setrast_patch(r->rp, r->pen, GfxBase);
    return 1;
}

static LONG ogfx_draw_provider(APTR u, struct ortg_ogfx_draw *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    draw_patch(r->rp, (WORD)r->x, (WORD)r->y, GfxBase);
    return 1;
}

static LONG ogfx_polydraw_provider(APTR u, struct ortg_ogfx_polydraw *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    polydraw_patch(r->rp, (WORD)r->count, r->array, GfxBase);
    return 1;
}

static LONG ogfx_writepixel_provider(APTR u, struct ortg_ogfx_pixel *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = writepixel_patch(r->rp, (WORD)r->x, (WORD)r->y, GfxBase);
    return 1;
}

static LONG ogfx_readpixel_provider(APTR u, struct ortg_ogfx_pixel *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = (LONG)readpixel_patch(r->rp, (WORD)r->x, (WORD)r->y, GfxBase);
    return 1;
}

static LONG ogfx_bltbmrp_provider(APTR u, struct ortg_ogfx_bltbmrp *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    bltbmrp_patch(r->src, (WORD)r->sx, (WORD)r->sy, r->rp, (WORD)r->dx, (WORD)r->dy, (WORD)r->width, (WORD)r->height,
                  r->minterm, GfxBase);
    return 1;
}

static LONG ogfx_bltmaskbmrp_provider(APTR u, struct ortg_ogfx_bltbmrp *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    bltmaskbmrp_patch(r->src, (WORD)r->sx, (WORD)r->sy, r->rp, (WORD)r->dx, (WORD)r->dy, (WORD)r->width, (WORD)r->height,
                      r->minterm, r->mask, GfxBase);
    return 1;
}

static LONG ogfx_clipblit_provider(APTR u, struct ortg_ogfx_clipblit *r)
{
    if (!r || !r->rp || !r->src_rp || (!ortg_is(r->rp->BitMap) && !ortg_is(r->src_rp->BitMap))) return 0;
    clipblit_patch(r->src_rp, (WORD)r->sx, (WORD)r->sy, r->rp, (WORD)r->dx, (WORD)r->dy, (WORD)r->width, (WORD)r->height,
                   r->minterm, GfxBase);
    return 1;
}

static LONG ogfx_wcp_provider(APTR u, struct ortg_ogfx_array *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    wcp_patch(r->rp, (WORD)r->x0, (WORD)r->y0, (WORD)r->x1, (WORD)r->y1, r->array, r->bytes_per_row, GfxBase);
    return 1;
}

static LONG ogfx_wpa8_provider(APTR u, struct ortg_ogfx_array *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = wpa8_patch(r->rp, (WORD)r->x0, (WORD)r->y0, (WORD)r->x1, (WORD)r->y1, r->array, r->temp_rp, GfxBase);
    return 1;
}

static LONG ogfx_rpa8_provider(APTR u, struct ortg_ogfx_array *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = rpa8_patch(r->rp, (WORD)r->x0, (WORD)r->y0, (WORD)r->x1, (WORD)r->y1, r->array, r->temp_rp, GfxBase);
    return 1;
}

static LONG ogfx_wpl8_provider(APTR u, struct ortg_ogfx_array *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = wpl8_patch(r->rp, (WORD)r->x0, (WORD)r->y0, (UWORD)r->width, r->array, r->temp_rp, GfxBase);
    return 1;
}

static LONG ogfx_rpl8_provider(APTR u, struct ortg_ogfx_array *r)
{
    if (!r || !r->rp || !ortg_is(r->rp->BitMap)) return 0;
    r->result = rpl8_patch(r->rp, (WORD)r->x0, (WORD)r->y0, (UWORD)r->width, r->array, r->temp_rp, GfxBase);
    return 1;
}

static struct ortg_ogfx_provider_all ogfx_all_provider;
#define ogfx_provider (ogfx_all_provider.v1)

static int try_opengfx_handoff(void)
{
    int registered = 0;

    ogfx_provider.size = sizeof ogfx_provider;
    ogfx_provider.abi = ORTG_OGFX_PROVIDER_ABI_V1;
    ogfx_provider.owner = (APTR)&ogfx_provider;
    ogfx_provider.userdata = NULL;
    ogfx_provider.rectfill = ogfx_rectfill_provider;
    ogfx_provider.text = ogfx_text_provider;
    ogfx_provider.bltbitmap = ogfx_bltbitmap_provider;
    ogfx_provider.scrollraster = ogfx_scroll_provider;
    ogfx_provider.textlength = NULL;
    ogfx_provider.textextent = NULL;
    ogfx_provider.textfit = NULL;
    ogfx_provider.blttemplate = ogfx_blttemplate_provider;

    /* opengpu.library 0.6 and later carries OpenGfx itself: the one library
     * that does all the drawing. OpenRTG registers there, and doesn't look
     * for opengfx.library (by now a stub that forwards to it). From 0.8
     * (OpenGfx 1.4) OpenGfx patches every drawing call, so OpenRTG registers
     * the whole record and patches none of them itself. */
    if (OpenGPUBase && (OpenGPUBase->lib_Version > 0 || OpenGPUBase->lib_Revision >= ORTG_OPENGPU_OGFX_REVISION)) {
        if (ORTG_OGPU_OGFX_Version(OpenGPUBase) >= ORTG_OGFX_ALL_INTERFACE) {
            struct ortg_ogfx_provider_all *a = &ogfx_all_provider;
            a->bltpattern = ogfx_bltpattern_provider;
            a->setrast = ogfx_setrast_provider;
            a->draw = ogfx_draw_provider;
            a->polydraw = ogfx_polydraw_provider;
            a->writepixel = ogfx_writepixel_provider;
            a->readpixel = ogfx_readpixel_provider;
            a->bltbitmaprastport = ogfx_bltbmrp_provider;
            a->bltmaskbitmaprastport = ogfx_bltmaskbmrp_provider;
            a->clipblit = ogfx_clipblit_provider;
            a->writechunkypixels = ogfx_wcp_provider;
            a->writepixelarray8 = ogfx_wpa8_provider;
            a->writepixelline8 = ogfx_wpl8_provider;
            a->readpixelline8 = ogfx_rpl8_provider;
            a->readpixelarray8 = ogfx_rpa8_provider;
            ogfx_provider.size = sizeof *a;
            ogfx_all = 1;
        }
        if (!ORTG_OGPU_OGFX_RegisterProvider(OpenGPUBase, &ogfx_provider)) { ogfx_all = 0; return 0; }
        if (ORTG_OGPU_OGFX_InstallPatches(OpenGPUBase)) return 1;
        (void)ORTG_OGPU_OGFX_UnregisterProvider(OpenGPUBase, (APTR)&ogfx_provider);
        ogfx_all = 0;
        return 0;
    }

    OpenGfxBase = OpenLibrary((CONST_STRPTR)ORTG_OPENGFXLIB_NAME,
                              ORTG_OPENGFXLIB_VERSION);
    if (!OpenGfxBase) return 0;

    if (!ORTG_OGFX_RegisterProvider(OpenGfxBase, &ogfx_provider))
        goto fail;
    registered = 1;

    if (!ORTG_OGFX_InstallPatches(OpenGfxBase))
        goto fail;

    return 1;

fail:
    if (registered)
        (void)ORTG_OGFX_UnregisterProvider(OpenGfxBase, (APTR)&ogfx_provider);
    CloseLibrary(OpenGfxBase);
    OpenGfxBase = NULL;
    return 0;
}

/* ---- switching on ------------------------------------------------------------------------ */

static int on;

int ortg_screens_on(struct Library *gfx, struct ortg_mode_table **t, APTR *boards, const ULONG *sizes)
{
    if (on) return 1;
    GfxBase = (struct GfxBase *)gfx;
    if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39))) return 0;
    if (!(UtilityBase = OpenLibrary("utility.library", 39))) return 0;
    tables = t;
    for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
        if ((board[n] = boards[n]) != NULL) {
            board_size[n] = sizes && sizes[n] ? sizes[n] : VRAM_SIZE + 2 * PAGE_64K;
            regs_at[n] = board_size[n] - PAGE_64K;
            vram_size[n] = board_size[n] - 2 * PAGE_64K;
            ring_reserve(n);
        }

    /* OpenGPU v1.0/G1 is optional at boot. When present, the common RTG
     * fill/template/copy paths above submit v1.0 batches; every refusal
     * falls back to the existing OpenRTG CPU implementation. */
    OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
    /* Ask once now, from C:OpenRTG's process with the rings in place:
     * opengpu.library loads its drivers (LIBS:OpenGPU/) on the first
     * question a process asks, and better here than inside a drawing call
     * that holds a layer's lock. */
    if (OpenGPUBase) (void)OGPU_Query(OGPU_OP_FILL, OGPU_FMT_CLUT8);

    /* New stack: OpenGfx (inside opengpu.library from 0.6, before that
     * opengfx.library 1.1) owns Text, TextLength, TextExtent, TextFit,
     * RectFill, BltBitMap, BltTemplate and ScrollRaster. OpenRTG provides
     * its RTG drawing implementations through the provider bridge. If
     * OpenGfx is absent, retain the old standalone OpenRTG patch path.
     * OpenGfx 1.4 (opengpu.library 0.8) owns the fourteen other drawing
     * calls too (ogfx_all); OpenRTG keeps the bitmap, display, palette,
     * sprite and screen calls, which aren't drawing. */
    ogfx_handoff = try_opengfx_handoff();

    Forbid();
    if (!ogfx_handoff) old_rectfill = (rectfill_fn)SetFunction(gfx, -306, (APTR)rectfill_patch);
    if (!ogfx_all) old_bltpattern = (bltpattern_fn)SetFunction(gfx, -312, (APTR)bltpattern_patch);
    if (!ogfx_all) old_setrast = (setrast_fn)SetFunction(gfx, -234, (APTR)setrast_patch);
    if (!ogfx_all) old_draw = (draw_fn)SetFunction(gfx, -246, (APTR)draw_patch);
    if (!ogfx_all) old_polydraw = (polydraw_fn)SetFunction(gfx, -336, (APTR)polydraw_patch);
    if (!ogfx_all) old_writepixel = (writepixel_fn)SetFunction(gfx, -324, (APTR)writepixel_patch);
    if (!ogfx_all) old_readpixel = (readpixel_fn)SetFunction(gfx, -318, (APTR)readpixel_patch);
    if (!ogfx_handoff) old_text = (text_fn)SetFunction(gfx, -60, (APTR)text_patch);
    if (!ogfx_handoff) old_blttemplate = (blttemplate_fn)SetFunction(gfx, -36, (APTR)blttemplate_patch);
    if (!ogfx_handoff) old_bltbitmap = (bltbitmap_fn)SetFunction(gfx, -30, (APTR)bltbitmap_patch);
    if (!ogfx_all) old_bltbmrp = (bltbmrp_fn)SetFunction(gfx, -606, (APTR)bltbmrp_patch);
    if (!ogfx_all) old_bltmaskbmrp = (bltmaskbmrp_fn)SetFunction(gfx, -636, (APTR)bltmaskbmrp_patch);
    if (!ogfx_all) old_clipblit = (clipblit_fn)SetFunction(gfx, -552, (APTR)clipblit_patch);
    if (!ogfx_handoff) old_scroll = (scroll_fn)SetFunction(gfx, -396, (APTR)scroll_patch);
    old_allocbm = (allocbm_fn)SetFunction(gfx, -918, (APTR)allocbm_patch);
    old_freebm = (freebm_fn)SetFunction(gfx, -924, (APTR)freebm_patch);
    old_bmattr = (bmattr_fn)SetFunction(gfx, -960, (APTR)bmattr_patch);
    old_makevp = (makevp_fn)SetFunction(gfx, -216, (APTR)makevp_patch);
    old_mrgcop = (mrgcop_fn)SetFunction(gfx, -210, (APTR)mrgcop_patch);
    old_loadview = (loadview_fn)SetFunction(gfx, -222, (APTR)loadview_patch);
    old_loadrgb32 = (loadrgb32_fn)SetFunction(gfx, -882, (APTR)loadrgb32_patch);
    old_setrgb32 = (setrgb32_fn)SetFunction(gfx, -852, (APTR)setrgb32_patch);
    old_loadrgb4 = (loadrgb4_fn)SetFunction(gfx, -192, (APTR)loadrgb4_patch);
    old_setrgb4 = (setrgb4_fn)SetFunction(gfx, -288, (APTR)setrgb4_patch);
    if (!ogfx_all) old_wcp = (wcp_fn)SetFunction(gfx, -1056, (APTR)wcp_patch);
    if (!ogfx_all) old_wpa8 = (wpa8_fn)SetFunction(gfx, -786, (APTR)wpa8_patch);
    if (!ogfx_all) old_rpl8 = (line8_fn)SetFunction(gfx, -768, (APTR)rpl8_patch);
    if (!ogfx_all) old_wpl8 = (line8_fn)SetFunction(gfx, -774, (APTR)wpl8_patch);
    if (!ogfx_all) old_rpa8 = (wpa8_fn)SetFunction(gfx, -780, (APTR)rpa8_patch);
    old_movesprite = (movesprite_fn)SetFunction(gfx, -426, (APTR)movesprite_patch);
    old_changeext = (changeext_fn)SetFunction(gfx, -1026, (APTR)changeext_patch);
    old_rethink = (remake_fn)SetFunction((struct Library *)IntuitionBase, -390, (APTR)rethink_patch);
    old_remake = (remake_fn)SetFunction((struct Library *)IntuitionBase, -384, (APTR)remake_patch);
    old_makescreen = (makescreen_fn)SetFunction((struct Library *)IntuitionBase, -378, (APTR)makescreen_patch);
    old_openscreen = (openscreen_fn)SetFunction((struct Library *)IntuitionBase, -612, (APTR)openscreen_patch);
    old_closescreen = (closescreen_fn)SetFunction((struct Library *)IntuitionBase, -66, (APTR)closescreen_patch);
    CacheClearU();
    Permit();
    /* mouse_on(): not needed once the display handles are records */
    on = 1;
    return 1;
}
