/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * How a finished GL frame reaches the display on OS 3.2. Mesa draws into a
 * buffer in Fast RAM (gla_core.c); this file shows it, through the Open
 * stack rather than the bare CPU:
 *
 *  - A window, or anything with layers: WritePixelArray (RECTFMT_ARGB) into
 *    the RastPort. That is OpenRTG's cybergraphics call, which clips to the
 *    layers and draws through OpenGPU (one PIXELS command per visible part).
 *  - A whole screen of its own (an SDL full-screen game): with
 *    opengpu.library open, one SURFACE + PIXELS + FENCE batch straight into
 *    the screen's locked bitmap, and the CPU is free until the fence. On a
 *    plain 68040 that runs on OpenGPU's CPU back end; on AmigaChrome on the
 *    host's GPU.
 *  - No RTG (AGA only): not in phase 1. GL there needs a chunky-to-planar
 *    step, which comes with the AGA back end (OpenGPU G5).
 *
 * Big-endian builds draw GLA_ARGB, which is RECTFMT_ARGB and OGPU_FMT_ARGB32,
 * so no frame is converted on the way. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include <opengpu/opengpu.h>
#include <opengpu/build.h>
#include <proto/opengpu.h>

#include "../gla_core.h"
#include "gla_present_os3.h"

extern struct Library *CyberGfxBase;
extern struct Library *OpenGPUBase;

/* SURFACE 6 + TARGET 2 + PIXELS 7 + FENCE 2 words, with room to spare. */
#define BATCH_WORDS 24

static int present_gpu(struct gla_os3_target *t, const struct gla_frame *f)
{
    ULONG words[BATCH_WORDS], fence = 0, base = 0, bpr = 0, fmt = 0;
    struct OGPUBatch b;
    APTR lock;
    int w = f->x1 - f->x0, h = f->y1 - f->y0, ofmt;
    LONG err;

    lock = LockBitMapTags(t->bitmap, LBMI_BASEADDRESS, (ULONG)&base,
                          LBMI_BYTESPERROW, (ULONG)&bpr, LBMI_PIXFMT, (ULONG)&fmt, TAG_DONE);
    if (!lock)
        return 0;
    if (fmt == PIXFMT_ARGB32)
        ofmt = OGPU_FMT_ARGB32;
    else if (fmt == PIXFMT_RGB16)
        ofmt = OGPU_FMT_RGB565;
    else {
        UnLockBitMap(lock);
        return 0;                       /* other layouts: WritePixelArray converts */
    }
    ogpu_batch_init(&b, words, BATCH_WORDS);
    ogpu_surface(&b, 0, base, bpr, GetCyberMapAttr(t->bitmap, CYBRMATTR_WIDTH),
                 GetCyberMapAttr(t->bitmap, CYBRMATTR_HEIGHT), ofmt);
    ogpu_target(&b, 0);
    ogpu_pixels(&b, (unsigned long)((const UBYTE *)f->pixels + f->y0 * f->stride + f->x0 * 4),
                f->stride, OGPU_FMT_ARGB32, 0, t->left + f->x0, t->top + f->y0, w, h);
    ogpu_fence(&b, 1);
    err = b.overflow ? -1 : OGPU_Submit(words, b.words, &fence);
    if (err == OGPU_OK)
        OGPU_Wait(fence);
    UnLockBitMap(lock);
    return err == OGPU_OK;
}

static void present(void *user, const struct gla_frame *f)
{
    struct gla_os3_target *t = user;
    if (f->x1 <= f->x0 || f->y1 <= f->y0 || f->format != GLA_ARGB)
        return;
    if (t->bitmap && OpenGPUBase && CyberGfxBase && present_gpu(t, f))
        return;
    if (CyberGfxBase && t->rp)
        WritePixelArray((APTR)f->pixels, f->x0, f->y0, f->stride, t->rp,
                        t->left + f->x0, t->top + f->y0, f->x1 - f->x0, f->y1 - f->y0,
                        RECTFMT_ARGB);
}

void gla_os3_present_init(struct gla_present *p, struct gla_os3_target *t)
{
    p->present = present;
    p->user = t;
}
