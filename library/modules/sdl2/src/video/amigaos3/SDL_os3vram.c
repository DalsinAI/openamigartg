/*
  SDL 2 on AmigaOS 3.x: SDL's window surface and the opengpu renderer's
  textures in an OpenRTG board's video RAM (SDL_os3vram.h).
  Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).

  A bitmap is asked for as Picasso96 programs ask for one on the card:
  AllocBitMap(w, h, depth, BMF_DISPLAYABLE | BMF_MINPLANES, the screen's
  bitmap). OpenRTG may still put it in Fast RAM (past the share of video RAM
  displayable friends may take, or with the board full), so it is kept only
  when OpenRTG says it is in the screen's board's video RAM; otherwise it is
  freed at once and SDL makes its surface in Fast RAM, as before. The pixels
  and the pitch are LockBitMapTags', never worked out here.

  OpenRTG never moves a bitmap once it is made (its LockBitMap has nothing
  to hold), so the address stays good until the bitmap is freed.

  openrtg.library is opened for each bitmap and closed after: only its word
  on where a bitmap is (ORTG_BitMapInfo's monitor) is needed, once.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

#include "SDL_hints.h"
#include "SDL_atomic.h"

#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

/* openrtg.library's calls go through the local `ortg` of the function that
   makes them: SDL keeps no base of its own, and a program's OpenRTGBase is
   its own. */
#include <openrtg/openrtg.h>
#define OPENRTG_BASE_NAME ortg
#include <inline/openrtg.h>

#include "SDL_os3gpu.h"
#include "SDL_os3vram.h"

#define OS3_VRAM_ORTG_REVISION 19  /* openrtg.library 0.19: displayable friends in video RAM */
#define OS3_VRAM_ORTG_ARGB32   2   /* OpenRTGBitMapInfo's format: 32-bit ARGB */

/* At most this many bitmaps a program in video RAM (its window surfaces and
   textures). OpenRTG keeps every board's video RAM in 128 blocks, and the
   share it gives displayable friends counts bytes, not blocks: a program
   with hundreds of small textures must not leave no block for a screen. */
#define OS3_VRAM_MOST 32

typedef struct OS3_VRAMSurface {
    SDL_Surface *surface;
    struct BitMap *bm;
    struct OS3_VRAMSurface *next;
} OS3_VRAMSurface;

static SDL_atomic_t vram_bitmaps;           /* held now */
static OS3_VRAMSurface *vram_surfaces;      /* the textures' (SDL_OS3_VRAM_CreateSurface) */
static SDL_SpinLock vram_lock;

/* Only for OpenGPU, which is what draws in video RAM: with SDL_OPENGPU=0,
   SDL_OPENGPU_VRAM=0 or no opengpu.library, Fast RAM as before. */
static SDL_bool OS3_VRAM_Wanted(void)
{
    if (!SDL_GetHintBoolean("SDL_OPENGPU_VRAM", SDL_TRUE) || !SDL_OS3_GPU_Open()) {
        return SDL_FALSE;
    }
    SDL_OS3_GPU_Close();
    return SDL_TRUE;
}

struct BitMap *SDL_OS3_VRAM_AllocBitMap(struct Window *win, int w, int h, void **pixels, int *pitch)
{
    struct Library *ortg;
    struct BitMap *sbm, *bm = NULL;
    struct OpenRTGBitMapInfo si, bi;
    ULONG depth, base = 0, bpr = 0, pixfmt = 0;
    APTR lock;
    SDL_bool kept = SDL_FALSE;

    SDL_zero(si);
    SDL_zero(bi);
    if (!win || !win->WScreen || w <= 0 || h <= 0 || w > 32767 || h > 32767 || !CyberGfxBase || !OS3_VRAM_Wanted()) {
        return NULL;
    }
    if (SDL_AtomicGet(&vram_bitmaps) >= OS3_VRAM_MOST) {
        return NULL;
    }
    ortg = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0);
    if (!ortg) {
        return NULL; /* not OpenRTG */
    }
    sbm = win->WScreen->RastPort.BitMap;
    /* The screen's own bitmap: in a board's video RAM, 32-bit ARGB */
    if (ortg->lib_Version > 0 || ortg->lib_Revision >= OS3_VRAM_ORTG_REVISION) {
        if (sbm && ORTG_BitMapInfo(sbm, &si) && si.monitor && si.depth == 32 && si.format == OS3_VRAM_ORTG_ARGB32) {
            /* Each value asked on its own: bebbo gcc's inlines must not nest. */
            depth = GetCyberMapAttr(sbm, CYBRMATTR_DEPTH);
            bm = AllocBitMap((ULONG)w, (ULONG)h, depth, BMF_DISPLAYABLE | BMF_MINPLANES, sbm);
        }
    }
    if (bm) {
        lock = LockBitMapTags(bm, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr,
                              LBMI_PIXFMT, (ULONG)&pixfmt, TAG_DONE);
        if (lock) {
            UnLockBitMap(lock);
        }
        /* Kept only in the screen's board's video RAM: ORTG_BitMapInfo's
           monitor is 0 for a bitmap OpenRTG put in Fast RAM. */
        if (lock && base && pixfmt == PIXFMT_ARGB32 && bpr >= (ULONG)w * 4 &&
            ORTG_BitMapInfo(bm, &bi) && bi.monitor == si.monitor && (ULONG)bi.memory == base) {
            kept = SDL_TRUE;
        }
    }
    CloseLibrary(ortg);
    if (!kept) {
        if (bm) {
            FreeBitMap(bm);
        }
        return NULL;
    }
    /* SDL's own surfaces start cleared */
    SDL_memset((void *)base, 0, (size_t)bpr * (size_t)h);
    SDL_AtomicIncRef(&vram_bitmaps);
    *pixels = (void *)base;
    *pitch = (int)bpr;
    return bm;
}

void SDL_OS3_VRAM_FreeBitMap(struct BitMap *bm)
{
    if (bm) {
        FreeBitMap(bm);
        SDL_AtomicDecRef(&vram_bitmaps);
    }
}

SDL_Surface *SDL_OS3_VRAM_CreateSurface(SDL_Window *window, int w, int h, Uint32 format)
{
    OS3_VRAMSurface *vs;
    void *pixels = NULL;
    int pitch = 0;

    if (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_RGB888) {
        return NULL; /* the 32-bit board's own layout only */
    }
    vs = (OS3_VRAMSurface *)SDL_calloc(1, sizeof(*vs));
    if (!vs) {
        return NULL;
    }
    vs->bm = SDL_OS3_VRAM_AllocBitMap(SDL_OS3_IntuitionWindow(window), w, h, &pixels, &pitch);
    if (vs->bm) {
        vs->surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, pitch, format);
    }
    if (!vs->surface) {
        SDL_OS3_VRAM_FreeBitMap(vs->bm);
        SDL_free(vs);
        return NULL;
    }
    SDL_AtomicLock(&vram_lock);
    vs->next = vram_surfaces;
    vram_surfaces = vs;
    SDL_AtomicUnlock(&vram_lock);
    return vs->surface;
}

void SDL_OS3_VRAM_FreeSurface(SDL_Surface *surface)
{
    OS3_VRAMSurface **p, *vs = NULL;

    if (!surface) {
        return;
    }
    SDL_AtomicLock(&vram_lock);
    for (p = &vram_surfaces; *p; p = &(*p)->next) {
        if ((*p)->surface == surface) {
            vs = *p;
            *p = vs->next;
            break;
        }
    }
    SDL_AtomicUnlock(&vram_lock);
    SDL_FreeSurface(surface); /* SDL_PREALLOC: the pixels are the bitmap's */
    if (vs) {
        SDL_OS3_VRAM_FreeBitMap(vs->bm);
        SDL_free(vs);
    }
}

#endif /* SDL_VIDEO_RENDER_OPENGPU */
