/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Pixel arrays (0.4): rectangles of pixels in memory written to a RastPort,
 * read from one, fills and inverts in a colour rather than a pen. These are
 * what cybergraphics.library and Picasso96API.library pass on to OpenRTG
 * (DESIGN.md section 1); Workbench needs them for its backdrop, since
 * picture.datatype draws through CyberGraphX when it finds it and pokes
 * planes when it doesn't.
 *
 * On an OpenRTG bitmap they work on the chunky pixels through the layer's
 * clip rectangles (screens.c's pieces); on any other bitmap through
 * graphics (WriteChunkyPixels, ReadPixel). OpenRTG's screens are 8-bit, so
 * a colour becomes the nearest pen of the RastPort's palette, from a cache
 * of 32x32x32 colours kept until the palette changes.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "screens.h"
#include "pixels.h"

extern struct GfxBase *GfxBase;

/* ---- colours to pens ------------------------------------------------------------- */

static struct SignalSemaphore lock;
static int lock_ready;
static struct ColorMap *pal_cm;
static ULONG pal_gen, pal_n;
static ULONG pal[256];                  /* 0x00RRGGBB */
static UBYTE *near;                     /* 32768 pens, by 5-bit R, G, B */
static UBYTE known[32768 / 8];

static void obtain(void)
{
    Forbid();
    if (!lock_ready) { InitSemaphore(&lock); lock_ready = 1; }
    Permit();
    ObtainSemaphore(&lock);
}

/* The palette in pal[], and the cache emptied, when it isn't the one it was. */
static void use_palette(struct ColorMap *cm)
{
    static ULONG rgb[3 * 256];
    if (cm == pal_cm && pal_gen == ortg_palette_gen && pal_n) return;
    pal_cm = cm; pal_gen = ortg_palette_gen;
    pal_n = cm ? (cm->Count < 256 ? cm->Count : 256) : 0;
    if (pal_n) {
        GetRGB32(cm, 0, pal_n, rgb);
        for (ULONG i = 0; i < pal_n; i++) pal[i] = (rgb[3 * i] >> 24) << 16 | (rgb[3 * i + 1] >> 24) << 8 | rgb[3 * i + 2] >> 24;
    } else {
        for (ULONG i = 0; i < 256; i++) pal[i] = i << 16 | i << 8 | i;   /* no palette: a grey ramp */
        pal_n = 256;
    }
    for (ULONG i = 0; i < sizeof known; i++) known[i] = 0;
}

static UBYTE pen_of(ULONG rgb)
{
    ULONG r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    ULONG k = (r >> 3) << 10 | (g >> 3) << 5 | b >> 3;
    if (near && (known[k >> 3] & (1 << (k & 7)))) return near[k];
    {
        ULONG best = 0, dist = ~0UL;
        for (ULONG i = 0; i < pal_n; i++) {
            LONG dr = (LONG)((pal[i] >> 16) & 255) - (LONG)r, dg = (LONG)((pal[i] >> 8) & 255) - (LONG)g, db = (LONG)(pal[i] & 255) - (LONG)b;
            ULONG d = (ULONG)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
            if (d < dist) { dist = d; best = i; if (!d) break; }
        }
        if (near) { near[k] = (UBYTE)best; known[k >> 3] |= (UBYTE)(1 << (k & 7)); }
        return (UBYTE)best;
    }
}

/* ---- reading a source pixel ------------------------------------------------------------ */

/* The pen for pixel sx of a row in px's format. */
static UBYTE src_pen(const struct OpenRTGPixels *px, const UBYTE *row, LONG sx)
{
    const UBYTE *p;
    switch (px->format) {
    case ORTG_PIX_PEN: case ORTG_PIX_RAW: return row[sx];
    case ORTG_PIX_GREY: return pen_of((ULONG)row[sx] * 0x010101UL);
    case ORTG_PIX_INDEX: return pen_of(px->ctable ? px->ctable[row[sx]] & 0xFFFFFF : (ULONG)row[sx] * 0x010101UL);
    case ORTG_PIX_RGB: p = row + 3 * sx; return pen_of((ULONG)p[0] << 16 | p[1] << 8 | p[2]);
    case ORTG_PIX_BGR: p = row + 3 * sx; return pen_of((ULONG)p[2] << 16 | p[1] << 8 | p[0]);
    case ORTG_PIX_RGBA: case ORTG_PIX_RGB0: p = row + 4 * sx; return pen_of((ULONG)p[0] << 16 | p[1] << 8 | p[2]);
    case ORTG_PIX_ARGB: case ORTG_PIX_0RGB: p = row + 4 * sx; return pen_of((ULONG)p[1] << 16 | p[2] << 8 | p[3]);
    case ORTG_PIX_BGRA: case ORTG_PIX_BGR0: p = row + 4 * sx; return pen_of((ULONG)p[2] << 16 | p[1] << 8 | p[0]);
    case ORTG_PIX_ABGR: case ORTG_PIX_0BGR: p = row + 4 * sx; return pen_of((ULONG)p[3] << 16 | p[2] << 8 | p[1]);
    default: return 0;
    }
}

/* A pen stored as a pixel of px's format. */
static void dst_store(const struct OpenRTGPixels *px, UBYTE *row, LONG x, UBYTE pen)
{
    ULONG c = pal[pen];
    UBYTE r = (UBYTE)(c >> 16), g = (UBYTE)(c >> 8), b = (UBYTE)c, *p;
    switch (px->format) {
    case ORTG_PIX_PEN: case ORTG_PIX_RAW: case ORTG_PIX_INDEX: row[x] = pen; break;
    case ORTG_PIX_GREY: row[x] = (UBYTE)((r * 77 + g * 151 + b * 28) >> 8); break;
    case ORTG_PIX_RGB: p = row + 3 * x; p[0] = r; p[1] = g; p[2] = b; break;
    case ORTG_PIX_BGR: p = row + 3 * x; p[0] = b; p[1] = g; p[2] = r; break;
    case ORTG_PIX_RGBA: case ORTG_PIX_RGB0: p = row + 4 * x; p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF; break;
    case ORTG_PIX_ARGB: case ORTG_PIX_0RGB: p = row + 4 * x; p[0] = 0xFF; p[1] = r; p[2] = g; p[3] = b; break;
    case ORTG_PIX_BGRA: case ORTG_PIX_BGR0: p = row + 4 * x; p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF; break;
    case ORTG_PIX_ABGR: case ORTG_PIX_0BGR: p = row + 4 * x; p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r; break;
    default: break;
    }
}

/* ---- the operations ------------------------------------------------------------------------ */

struct pix_ctx {
    const struct OpenRTGPixels *px;
    LONG at_x, at_y;            /* the RastPort position of the rectangle */
    LONG dw, dh;                /* its size there */
    UBYTE mask, value;
};

static LONG src_x(const struct pix_ctx *c, LONG rx) { return c->px->x + (c->dw == c->px->width ? rx : rx * c->px->width / c->dw); }
static LONG src_y(const struct pix_ctx *c, LONG ry) { return c->px->y + (c->dh == c->px->height ? ry : ry * c->px->height / c->dh); }

static void write_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    for (LONG y = y0; y <= y1; y++) {
        const UBYTE *row = (const UBYTE *)c->px->data + src_y(c, y - dy - c->at_y) * c->px->modulo;
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) {
            UBYTE pen = src_pen(c->px, row, src_x(c, x - dx - c->at_x));
            d[x] = c->mask == 0xFF ? pen : (UBYTE)((d[x] & ~c->mask) | (pen & c->mask));
        }
    }
}

static void read_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *row = (UBYTE *)c->px->data + (c->px->y + y - dy - c->at_y) * c->px->modulo;
        const UBYTE *s = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) dst_store(c->px, row, c->px->x + x - dx - c->at_x, s[x]);
    }
}

static void fill_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) d[x] = (UBYTE)((d[x] & ~c->mask) | (c->value & c->mask));
    }
}

static void invert_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) d[x] ^= c->mask;
    }
}

static void begin(struct RastPort *rp)
{
    obtain();
    if (!near) near = AllocVec(32768, MEMF_ANY);
    use_palette(ortg_colormap(rp));
}

LONG ortg_write_pixels(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px)
{
    struct pix_ctx c;
    LONG done;
    if (!GfxBase || !rp || !px || !px->data || px->width <= 0 || px->height <= 0) return 0;
    c.px = px; c.at_x = x; c.at_y = y;
    c.dw = px->dest_width > 0 ? px->dest_width : px->width;
    c.dh = px->dest_height > 0 ? px->dest_height : px->height;
    c.mask = rp->Mask; c.value = 0;
    done = c.dw * c.dh;
    begin(rp);
    if (!ortg_pieces(rp, x, y, x + c.dw - 1, y + c.dh - 1, write_piece, &c)) {
        /* a planar RastPort: a row of pens at a time through graphics */
        UBYTE *pens = AllocVec((ULONG)c.dw + 16, MEMF_ANY);
        if (pens) {
            for (LONG j = 0; j < c.dh; j++) {
                const UBYTE *row = (const UBYTE *)px->data + src_y(&c, j) * px->modulo;
                for (LONG i = 0; i < c.dw; i++) pens[i] = src_pen(px, row, src_x(&c, i));
                WriteChunkyPixels(rp, x, y + j, x + c.dw - 1, y + j, pens, c.dw);
            }
            FreeVec(pens);
        } else done = 0;
    }
    ReleaseSemaphore(&lock);
    return done;
}

LONG ortg_read_pixels(struct RastPort *rp, LONG x, LONG y, struct OpenRTGPixels *px)
{
    struct pix_ctx c;
    if (!GfxBase || !rp || !px || !px->data || px->width <= 0 || px->height <= 0) return 0;
    c.px = px; c.at_x = x; c.at_y = y; c.dw = px->width; c.dh = px->height; c.mask = 0xFF; c.value = 0;
    begin(rp);
    if (!ortg_pieces(rp, x, y, x + px->width - 1, y + px->height - 1, read_piece, &c)) {
        for (LONG j = 0; j < px->height; j++) {
            UBYTE *row = (UBYTE *)px->data + (px->y + j) * px->modulo;
            for (LONG i = 0; i < px->width; i++) {
                LONG pen = ReadPixel(rp, x + i, y + j);
                dst_store(px, row, px->x + i, (UBYTE)(pen < 0 ? 0 : pen));
            }
        }
    }
    ReleaseSemaphore(&lock);
    return px->width * px->height;
}

LONG ortg_fill_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, ULONG argb)
{
    struct pix_ctx c;
    if (!GfxBase || !rp || w <= 0 || h <= 0) return 0;
    begin(rp);
    c.px = NULL; c.mask = rp->Mask; c.value = pen_of(argb & 0xFFFFFF);
    if (!ortg_pieces(rp, x, y, x + w - 1, y + h - 1, fill_piece, &c)) {
        UBYTE old_pen = rp->FgPen, old_mode = rp->DrawMode;
        SetAPen(rp, c.value); SetDrMd(rp, JAM1);
        RectFill(rp, x, y, x + w - 1, y + h - 1);
        SetAPen(rp, old_pen); SetDrMd(rp, old_mode);
    }
    ReleaseSemaphore(&lock);
    return w * h;
}

LONG ortg_invert_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h)
{
    struct pix_ctx c;
    if (!GfxBase || !rp || w <= 0 || h <= 0) return 0;
    c.px = NULL; c.mask = rp->Mask; c.value = 0;
    if (!ortg_pieces(rp, x, y, x + w - 1, y + h - 1, invert_piece, &c)) {
        UBYTE old_mode = rp->DrawMode;
        SetDrMd(rp, COMPLEMENT);
        RectFill(rp, x, y, x + w - 1, y + h - 1);
        SetDrMd(rp, old_mode);
    }
    return w * h;
}

BOOL ortg_bitmap_info(struct BitMap *bm, struct OpenRTGBitMapInfo *info)
{
    struct ortg_bitmap *o = ortg_find(bm);
    if (!o || !info) return FALSE;
    info->memory = o->mem;
    info->bytes_per_row = o->stride;
    info->width = o->width; info->height = o->height;
    info->depth = 8; info->format = 0; info->monitor = o->monitor; info->pad = 0;
    return TRUE;
}
