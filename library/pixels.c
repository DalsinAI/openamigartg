/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
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
#include "ortg_state.h"
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include "../include/oa_protos_begin.h"   /* the protos' unused varargs wrappers dropped */
#include <proto/exec.h>
#include <proto/graphics.h>
#include "../include/oa_protos_end.h"

#include <opengpu/stream.h>

#include "screens.h"
#include "pixels.h"

/* ---- colours to pens ------------------------------------------------------------- */

/* What the pixel arrays write (0.18): their part of the library's state (ortg_state.h). */
struct pix_g {
    struct SignalSemaphore lock;
    int lock_ready;
    struct ColorMap *pal_cm;
    ULONG pal_gen, pal_n;
    ULONG pal[256];                     /* 0x00RRGGBB */
    UBYTE *near;                        /* 32768 pens, by 5-bit R, G, B */
    UBYTE known[32768 / 8];
    ULONG rgb[3 * 256];                 /* use_palette's GetRGB32 */
};
ULONG ortg_pixels_size(void) { return sizeof(struct pix_g); }
#define PX ((struct pix_g *)ortg_S->pix)
#define lock (PX->lock)
#define lock_ready (PX->lock_ready)
#define pal_cm (PX->pal_cm)
#define pal_gen (PX->pal_gen)
#define pal_n (PX->pal_n)
#define pal (PX->pal)
#define near (PX->near)
#define known (PX->known)

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
    ULONG *rgb = PX->rgb;
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

/* The colour of pixel sx of a row in px's format (for the formats that
 * carry colours, and pens through the palette). */
static ULONG src_rgb(const struct OpenRTGPixels *px, const UBYTE *row, LONG sx)
{
    const UBYTE *p;
    switch (px->format) {
    case ORTG_PIX_PEN: case ORTG_PIX_RAW: return pal[row[sx]];
    case ORTG_PIX_GREY: return (ULONG)row[sx] * 0x010101UL;
    case ORTG_PIX_INDEX: return px->ctable ? px->ctable[row[sx]] & 0xFFFFFF : (ULONG)row[sx] * 0x010101UL;
    case ORTG_PIX_RGB: p = row + 3 * sx; return (ULONG)p[0] << 16 | p[1] << 8 | p[2];
    case ORTG_PIX_BGR: p = row + 3 * sx; return (ULONG)p[2] << 16 | p[1] << 8 | p[0];
    case ORTG_PIX_RGBA: case ORTG_PIX_RGB0: p = row + 4 * sx; return (ULONG)p[0] << 16 | p[1] << 8 | p[2];
    case ORTG_PIX_ARGB: case ORTG_PIX_0RGB: p = row + 4 * sx; return (ULONG)p[1] << 16 | p[2] << 8 | p[3];
    case ORTG_PIX_BGRA: case ORTG_PIX_BGR0: p = row + 4 * sx; return (ULONG)p[2] << 16 | p[1] << 8 | p[0];
    case ORTG_PIX_ABGR: case ORTG_PIX_0BGR: p = row + 4 * sx; return (ULONG)p[3] << 16 | p[2] << 8 | p[1];
    default: return 0;
    }
}

/* A colour stored as a pixel of px's format; pen is what the pen formats store. */
static void dst_store_rgb(const struct OpenRTGPixels *px, UBYTE *row, LONG x, ULONG c, UBYTE pen)
{
    UBYTE r = (UBYTE)(c >> 16), g = (UBYTE)(c >> 8), b = (UBYTE)c, *p;
    switch (px->format) {
    case ORTG_PIX_PEN: case ORTG_PIX_INDEX: row[x] = pen; break;
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
    ULONG argb;                 /* fill: the colour */
};

/* Which of n source pixels the target pixel r of d (another size) shows: the
 * one under the target pixel's centre, as OpenGPU's COMPOSITE takes it
 * (ogpu_core.c: a 16.16 step of n / d once, then r * step + step / 2), so a
 * scaled write gives the same pixels on the CPU and through OpenGPU (0.16;
 * before, r * n / d). */
static LONG scaled(LONG r, LONG n, LONG d)
{
    ULONG step = ((ULONG)n << 16) / (ULONG)d;
    LONG s = (LONG)(((ULONG)r * step + (step >> 1)) >> 16);
    return s >= n ? n - 1 : s;
}

static LONG src_x(const struct pix_ctx *c, LONG rx) { return c->px->x + (c->dw == c->px->width ? rx : scaled(rx, c->px->width, c->dw)); }
static LONG src_y(const struct pix_ctx *c, LONG ry) { return c->px->y + (c->dh == c->px->height ? ry : scaled(ry, c->px->height, c->dh)); }

/* The OpenGPU format a source format is, where PIXELS draws exactly what
 * write_piece does (0: it doesn't): 1:1 only; pens raw on 8-bit; the
 * bitmap's own pixels; pens and colour tables as INDEX8 on 16 and 32-bit
 * (*table: the pens' colours or the caller's, 0x00RRGGBB, so alpha 0 as
 * ortg_encode gives); ARGB colours on 16-bit (RGB565 keeps the top bits, as
 * ortg_encode does). 32-bit OpenRTG pixels carry alpha 0, and PIXELS into
 * ARGB32 would keep an ARGB source's alpha, so there ARGB colours, and
 * tables with alpha, go to the bitmap seen as XRGB32 (*xrgb 1), which
 * drops it; a back end without XRGB32 leaves them on the CPU. */
static int ogpu_source(const struct pix_ctx *c, const struct ortg_bitmap *bm, const void **table, int *xrgb)
{
    ULONG fmt = c->px->format;
    *table = NULL;
    *xrgb = 0;
    if (c->dw != c->px->width || c->dh != c->px->height) return 0;
    if (bm->bpp == 1) {
        if (c->mask != 0xFF) return 0;
        return fmt == ORTG_PIX_PEN || fmt == ORTG_PIX_RAW ? OGPU_FMT_CLUT8 : 0;
    }
    if (fmt == ORTG_PIX_RAW) return bm->bpp == 2 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    if (fmt == ORTG_PIX_PEN) { *table = ortg_pen_rgb[bm->pal_index]; return OGPU_FMT_INDEX8; }
    if (fmt == ORTG_PIX_INDEX && c->px->ctable) {
        /* the CPU code drops a colour's top byte; into ARGB32 PIXELS would keep it */
        if (bm->bpp == 4) for (int i = 0; i < 256; i++) if (c->px->ctable[i] >> 24) {
            if (!ortg_ogpu_xrgb(bm)) return 0;
            *xrgb = 1;
            break;
        }
        *table = c->px->ctable;
        return OGPU_FMT_INDEX8;
    }
    if (fmt == ORTG_PIX_ARGB || fmt == ORTG_PIX_0RGB) {
        if (bm->bpp == 2) return OGPU_FMT_ARGB32;
        if (bm->bpp == 4 && ortg_ogpu_xrgb(bm)) { *xrgb = 1; return OGPU_FMT_ARGB32; }
    }
    return 0;
}

/* The OpenGPU format of a source drawn at another size, where COMPOSITE
 * gives write_piece's pixels (0: it doesn't): ARGB and 0RGB colours, opaque,
 * on 16 and 32-bit (ortg_ogpu_scale keeps a 32-bit pixel's top byte 0); a 16-bit
 * bitmap's own pixels (RAW) on 16-bit, which RGB565 carries through
 * unchanged. A 32-bit RAW source keeps its top byte on the CPU, which
 * COMPOSITE doesn't, so it stays there; so do pens and colour tables. */
static int scaled_source(const struct pix_ctx *c, const struct ortg_bitmap *bm)
{
    ULONG fmt = c->px->format;
    if (c->dw == c->px->width && c->dh == c->px->height) return 0;
    if (bm->bpp == 1 || !c->mask) return 0;
    if (fmt == ORTG_PIX_ARGB || fmt == ORTG_PIX_0RGB) return OGPU_FMT_ARGB32;
    if (fmt == ORTG_PIX_RAW && bm->bpp == 2) return OGPU_FMT_RGB565;
    return 0;
}

static void write_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    ULONG fmt = c->px->format;
    int of, xrgb;
    const void *table;
    if (bm->bpp != 1 && !c->mask) return;
    if ((of = ogpu_source(c, bm, &table, &xrgb)) != 0) {
        LONG bpp = of == OGPU_FMT_ARGB32 ? 4 : of == OGPU_FMT_RGB565 ? 2 : 1;
        const UBYTE *src = (const UBYTE *)c->px->data + (c->px->y + y0 - dy - c->at_y) * c->px->modulo + (c->px->x + x0 - dx - c->at_x) * bpp;
        if (xrgb ? ortg_ogpu_pixels_xrgb(bm, x0, y0, x1, y1, src, (ULONG)c->px->modulo, of, table)
                 : ortg_ogpu_pixels(bm, x0, y0, x1, y1, src, (ULONG)c->px->modulo, of, table)) {
            ortg_stat(ORTG_STAT_PIXELS, 1, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
            return;
        }
    } else if ((of = scaled_source(c, bm)) != 0) {
        /* another size: OpenGPU's COMPOSITE from the source rectangle (0.16) */
        const UBYTE *src = (const UBYTE *)c->px->data + c->px->y * c->px->modulo + c->px->x * (of == OGPU_FMT_RGB565 ? 2 : 4);
        if (ortg_ogpu_scale(bm, x0, y0, x1, y1, src, (ULONG)c->px->modulo, c->px->width, c->px->height, of,
                            c->at_x + dx, c->at_y + dy, c->dw, c->dh)) {
            ortg_stat(ORTG_STAT_PIXELS, 1, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
            return;
        }
    }
    ortg_stat(ORTG_STAT_PIXELS, 0, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
    if (bm->bpp == 4 && (fmt == ORTG_PIX_ARGB || fmt == ORTG_PIX_0RGB)
        && c->dw == c->px->width && c->dh == c->px->height) {
        /* ARGB colours 1:1 onto a 32-bit bitmap, the case a GL or SDL frame
         * in a window takes: each pixel is the source's longword with its
         * alpha byte cleared, exactly what ortg_encode(src_rgb()) gives,
         * without a call and a format switch per pixel (11 Oct 2026:
         * AstroMenace's frames spent about a quarter of the 68k's time here). */
        LONG sx0 = c->px->x + x0 - dx - c->at_x, n0 = x1 - x0 + 1;
        for (LONG y = y0; y <= y1; y++) {
            const UBYTE *row = (const UBYTE *)c->px->data + (c->px->y + y - dy - c->at_y) * c->px->modulo + sx0 * 4;
            ULONG *d = (ULONG *)(bm->mem + y * bm->stride) + x0;
            LONG n = n0;
            if (!((ULONG)row & 1)) {
                const ULONG *s = (const ULONG *)row;
                while (n--) *d++ = *s++ & 0x00FFFFFFUL;
            } else
                for (; n--; row += 4) *d++ = (ULONG)row[1] << 16 | (ULONG)row[2] << 8 | row[3];
        }
        return;
    }
    for (LONG y = y0; y <= y1; y++) {
        const UBYTE *row = (const UBYTE *)c->px->data + src_y(c, y - dy - c->at_y) * c->px->modulo;
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) {
            LONG sx = src_x(c, x - dx - c->at_x);
            if (bm->bpp == 1) {
                UBYTE pen = src_pen(c->px, row, sx);
                d[x] = c->mask == 0xFF ? pen : (UBYTE)((d[x] & ~c->mask) | (pen & c->mask));
            } else if (fmt == ORTG_PIX_PEN)
                ortg_put(bm, x, y, ortg_pen_px(bm, row[sx]));
            else if (fmt == ORTG_PIX_RAW)          /* the bitmap's own pixels */
                ortg_put(bm, x, y, bm->bpp == 2 ? ((const UWORD *)row)[sx] : ((const ULONG *)row)[sx]);
            else
                ortg_put(bm, x, y, ortg_encode(bm, src_rgb(c->px, row, sx)));
        }
    }
}

static void read_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    ULONG fmt = c->px->format;
    if (fmt == ORTG_PIX_RAW) {      /* the bitmap's own pixels: a COPY into memory */
        UBYTE *dst = (UBYTE *)c->px->data + (c->px->y + y0 - dy - c->at_y) * c->px->modulo + (c->px->x + x0 - dx - c->at_x) * bm->bpp;
        if (ortg_ogpu_read(bm, x0, y0, x1, y1, dst, (ULONG)c->px->modulo)) {
            ortg_stat(ORTG_STAT_PIXELS_READ, 1, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
            return;
        }
    } else if ((fmt == ORTG_PIX_ARGB || fmt == ORTG_PIX_0RGB) && bm->bpp != 1) {
        /* colours with alpha 255, as dst_store_rgb writes them */
        UBYTE *dst = (UBYTE *)c->px->data + (c->px->y + y0 - dy - c->at_y) * c->px->modulo + (c->px->x + x0 - dx - c->at_x) * 4;
        if (ortg_ogpu_read_argb(bm, x0, y0, x1, y1, dst, (ULONG)c->px->modulo)) {
            ortg_stat(ORTG_STAT_PIXELS_READ, 1, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
            return;
        }
    }
    ortg_stat(ORTG_STAT_PIXELS_READ, 0, (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1));
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *row = (UBYTE *)c->px->data + (c->px->y + y - dy - c->at_y) * c->px->modulo;
        const UBYTE *s = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) {
            LONG tx = c->px->x + x - dx - c->at_x;
            if (bm->bpp == 1) dst_store(c->px, row, tx, s[x]);
            else {
                ULONG v = ortg_get(bm, x, y);
                if (fmt == ORTG_PIX_RAW) { if (bm->bpp == 2) ((UWORD *)row)[tx] = (UWORD)v; else ((ULONG *)row)[tx] = v; }
                else dst_store_rgb(c->px, row, tx, ortg_decode(bm, v),
                                   (fmt == ORTG_PIX_PEN || fmt == ORTG_PIX_INDEX) ? ortg_px_pen(bm, v) : 0);
            }
        }
    }
}

static void fill_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    ULONG n = (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1);
    if ((bm->bpp == 1 ? c->mask == 0xFF : c->mask != 0)
        && ortg_ogpu_fill(bm, x0, y0, x1, y1, bm->bpp == 1 ? c->value : ortg_encode(bm, c->argb))) {
        ortg_stat(ORTG_STAT_PIXELS_FILL, 1, n);
        return;
    }
    ortg_stat(ORTG_STAT_PIXELS_FILL, 0, n);
    if (bm->bpp != 1) {
        ULONG v = ortg_encode(bm, c->argb);
        if (!c->mask) return;
        for (LONG y = y0; y <= y1; y++)
            for (LONG x = x0; x <= x1; x++) ortg_put(bm, x, y, v);
        return;
    }
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) d[x] = (UBYTE)((d[x] & ~c->mask) | (c->value & c->mask));
    }
}

static void invert_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct pix_ctx *c = v;
    ULONG n = (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1);
    if (c->mask && ortg_ogpu_invert(bm, x0, y0, x1, y1, bm->bpp == 1 ? c->mask : bm->bpp == 2 ? 0xFFFF : 0xFFFFFF)) {
        ortg_stat(ORTG_STAT_PIXELS_INVERT, 1, n);
        return;
    }
    ortg_stat(ORTG_STAT_PIXELS_INVERT, 0, n);
    if (bm->bpp != 1) {
        ULONG m = bm->bpp == 2 ? 0xFFFF : 0xFFFFFF;
        if (!c->mask) return;
        for (LONG y = y0; y <= y1; y++)
            for (LONG x = x0; x <= x1; x++) ortg_put(bm, x, y, ortg_get(bm, x, y) ^ m);
        return;
    }
    for (LONG y = y0; y <= y1; y++) {
        UBYTE *d = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++) d[x] ^= c->mask;
    }
}

/* Every pixel call takes its RastPort's layer lock first and the palette lock second, and
 * lets go in the opposite order. Intuition, graphics and programs take a layer's lock and then
 * call in here (OpenLook paints the screen bar's layer that way), so waiting for a layer while
 * holding the palette lock closes a cycle. It did on 10 Oct 2026: an SDL window's present held the
 * palette lock and waited for its layer, which Intuition's window drag held; the drag waited for
 * the screen bar's layer, held by a task that waited for the palette lock. The whole machine stopped. */
static struct Layer *begin(struct RastPort *rp)
{
    struct Layer *l = rp->Layer;
    if (l) ObtainSemaphore(&l->Lock);
    obtain();
    if (!near) near = AllocVec(32768, MEMF_ANY);
    use_palette(ortg_colormap(rp));
    return l;
}

static void end(struct Layer *l)
{
    ReleaseSemaphore(&lock);
    if (l) ReleaseSemaphore(&l->Lock);
}

LONG ortg_write_pixels(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px)
{
    struct pix_ctx c;
    struct Layer *lay;
    LONG done;
    if (!ortg_S->gfx || !rp || !px || !px->data || px->width <= 0 || px->height <= 0) return 0;
    c.px = px; c.at_x = x; c.at_y = y;
    c.dw = px->dest_width > 0 ? px->dest_width : px->width;
    c.dh = px->dest_height > 0 ? px->dest_height : px->height;
    c.mask = rp->Mask; c.value = 0;
    done = c.dw * c.dh;
    lay = begin(rp);
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
    end(lay);
    return done;
}

LONG ortg_read_pixels(struct RastPort *rp, LONG x, LONG y, struct OpenRTGPixels *px)
{
    struct pix_ctx c;
    struct Layer *lay;
    if (!ortg_S->gfx || !rp || !px || !px->data || px->width <= 0 || px->height <= 0) return 0;
    c.px = px; c.at_x = x; c.at_y = y; c.dw = px->width; c.dh = px->height; c.mask = 0xFF; c.value = 0;
    lay = begin(rp);
    if (!ortg_pieces(rp, x, y, x + px->width - 1, y + px->height - 1, read_piece, &c)) {
        for (LONG j = 0; j < px->height; j++) {
            UBYTE *row = (UBYTE *)px->data + (px->y + j) * px->modulo;
            for (LONG i = 0; i < px->width; i++) {
                LONG pen = ReadPixel(rp, x + i, y + j);
                dst_store(px, row, px->x + i, (UBYTE)(pen < 0 ? 0 : pen));
            }
        }
    }
    end(lay);
    return px->width * px->height;
}

LONG ortg_fill_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, ULONG argb)
{
    struct pix_ctx c;
    struct Layer *lay;
    if (!ortg_S->gfx || !rp || w <= 0 || h <= 0) return 0;
    lay = begin(rp);
    c.px = NULL; c.mask = rp->Mask; c.value = pen_of(argb & 0xFFFFFF); c.argb = argb & 0xFFFFFF;
    if (!ortg_pieces(rp, x, y, x + w - 1, y + h - 1, fill_piece, &c)) {
        UBYTE old_pen = rp->FgPen, old_mode = rp->DrawMode;
        SetAPen(rp, c.value); SetDrMd(rp, JAM1);
        RectFill(rp, x, y, x + w - 1, y + h - 1);
        SetAPen(rp, old_pen); SetDrMd(rp, old_mode);
    }
    end(lay);
    return w * h;
}

LONG ortg_invert_pixels(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h)
{
    struct pix_ctx c;
    if (!ortg_S->gfx || !rp || w <= 0 || h <= 0) return 0;
    c.px = NULL; c.mask = rp->Mask; c.value = 0;
    if (!ortg_pieces(rp, x, y, x + w - 1, y + h - 1, invert_piece, &c)) {
        UBYTE old_mode = rp->DrawMode;
        SetDrMd(rp, COMPLEMENT);
        RectFill(rp, x, y, x + w - 1, y + h - 1);
        SetDrMd(rp, old_mode);
    }
    return w * h;
}

/* ---- alpha (0.11) ----------------------------------------------------------------------- */

/* x * y / 255, rounded: OpenGPU's own (ogpu_core.c), so both paths give the same bytes. */
static ULONG m255(ULONG x, ULONG y) { ULONG t = x * y + 128; return (t + (t >> 8)) >> 8; }

struct alpha_ctx { const struct OpenRTGPixels *px; LONG at_x, at_y; ULONG alpha; UBYTE mask; };

/* ARGB over the bitmap by its alpha times the global alpha: OpenGPU's
 * COMPOSITE (SRCALPHA, OVER) on 16 and 32-bit, or the same sums here. An
 * 8-bit bitmap has no blending: a pixel at least half opaque is drawn as
 * its nearest pen, as before 0.11. */
static void alpha_piece(void *v, struct ortg_bitmap *bm, LONG x0, LONG y0, LONG x1, LONG y1, LONG dx, LONG dy)
{
    struct alpha_ctx *c = v;
    const struct OpenRTGPixels *px = c->px;
    const UBYTE *src = (const UBYTE *)px->data + (px->y + y0 - dy - c->at_y) * px->modulo + (px->x + x0 - dx - c->at_x) * 4;
    ULONG n = (ULONG)(x1 - x0 + 1) * (ULONG)(y1 - y0 + 1);
    if (bm->bpp != 1 && !c->mask) return;
    if (bm->bpp != 1 && ortg_ogpu_alpha(bm, x0, y0, x1, y1, src, (ULONG)px->modulo, c->alpha)) {
        ortg_stat(ORTG_STAT_ALPHA, 1, n);
        return;
    }
    ortg_stat(ORTG_STAT_ALPHA, 0, n);
    for (LONG y = y0; y <= y1; y++, src += px->modulo) {
        const UBYTE *p = src;
        UBYTE *d8 = bm->mem + y * bm->stride;
        for (LONG x = x0; x <= x1; x++, p += 4) {
            ULONG ea, ia, r, g, b, a, dv, dc;
            if (bm->bpp == 1) {
                if (p[0] >= 0x80) {
                    UBYTE pen = pen_of((ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]);
                    d8[x] = c->mask == 0xFF ? pen : (UBYTE)((d8[x] & ~c->mask) | (pen & c->mask));
                }
                continue;
            }
            ea = m255(p[0], c->alpha);
            if (!ea) continue;
            if (ea == 255) { r = p[1]; g = p[2]; b = p[3]; a = 255; }
            else {
                ia = 255 - ea;
                dv = ortg_get(bm, x, y);
                dc = ortg_decode(bm, dv);
                r = m255(p[1], ea) + m255((dc >> 16) & 255, ia);
                g = m255(p[2], ea) + m255((dc >> 8) & 255, ia);
                b = m255(p[3], ea) + m255(dc & 255, ia);
                a = ea + m255(bm->bpp == 4 ? (dv >> 24) & 255 : 255, ia);
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                if (a > 255) a = 255;
            }
            if (bm->bpp == 2) ortg_put(bm, x, y, ortg_encode(bm, r << 16 | g << 8 | b));
            else ortg_put(bm, x, y, a << 24 | r << 16 | g << 8 | b);
        }
    }
}

LONG ortg_write_pixels_alpha(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px, ULONG alpha)
{
    struct alpha_ctx c;
    struct Layer *lay;
    if (!ortg_S->gfx || !rp || !px || !px->data || px->width <= 0 || px->height <= 0) return 0;
    if (px->format != ORTG_PIX_ARGB) return ortg_write_pixels(rp, x, y, px);
    c.px = px; c.at_x = x; c.at_y = y; c.alpha = alpha & 255; c.mask = rp->Mask;
    lay = begin(rp);
    if (!ortg_pieces(rp, x, y, x + px->width - 1, y + px->height - 1, alpha_piece, &c)) {
        /* a planar RastPort: the pixels at least half opaque, run by run */
        end(lay);
        for (LONG j = 0; j < px->height; j++) {
            const UBYTE *row = (const UBYTE *)px->data + (px->y + j) * px->modulo + px->x * 4;
            LONG i = 0;
            while (i < px->width) {
                LONG run;
                struct OpenRTGPixels one = *px;
                while (i < px->width && row[i * 4] < 0x80) i++;
                for (run = 0; i + run < px->width && row[(i + run) * 4] >= 0x80; run++) ;
                if (run) {
                    one.x = px->x + i; one.y = px->y + j; one.width = run; one.height = 1; one.dest_width = one.dest_height = 0;
                    ortg_write_pixels(rp, x + i, y + j, &one);
                }
                i += run;
            }
        }
        return px->width * px->height;
    }
    end(lay);
    return px->width * px->height;
}

BOOL ortg_bitmap_info(struct BitMap *bm, struct OpenRTGBitMapInfo *info)
{
    struct ortg_bitmap *o = ortg_find(bm);
    if (!o || !info) return FALSE;
    info->memory = o->mem;
    info->bytes_per_row = o->stride;
    info->width = o->width; info->height = o->height;
    info->depth = o->bpp == 4 ? 32 : o->bpp == 2 ? 16 : 8; info->format = o->format; info->monitor = o->monitor;
    info->flags = ortg_in_card(o) ? ORTG_BMI_CARD : 0;                    /* 0.20 */
    return TRUE;
}
