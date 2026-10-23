/*
  SDL 3 on AmigaOS 3.x: opengpu.library, shared by the opengpu renderer
  (src/render/opengpu) and the window framebuffer.
  Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).

  SDL_OPENGPU=0 (SetEnv, or SDL_SetHint) keeps SDL off OpenGPU entirely:
  the software renderer, and windows shown by WritePixelArray.
*/
#ifndef SDL_os3gpu_h_
#define SDL_os3gpu_h_

#include "SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_OPENGPU

#include <exec/libraries.h>
#include <opengpu/opengpu.h>
#include <opengpu/build.h>

/* SDL's own base, so a program that opens opengpu.library keeps its own. */
extern struct Library *SDL_OS3_OpenGPUBase;
#define OPENGPU_BASE_NAME SDL_OS3_OpenGPUBase
#include <inline/opengpu.h>

/* Opens opengpu.library (counted). false when it isn't installed or
   SDL_OPENGPU is 0. */
extern bool SDL_OS3_GPU_Open(void);
extern void SDL_OS3_GPU_Close(void);

/* Stream v1.7 (opengpu.library 0.19): SDL 3's premultiplied blend modes,
   PIXELART, clamping one way, palette textures, the RGBA32 and ABGR32 byte
   orders, P010 and the BT.709 full range and BT.2020 colour spaces. Asked
   once, by the first SDL_OS3_GPU_Open. */
extern bool SDL_OS3_GPU_HasV17(void);

/* OpenGPU's format for a CyberGraphX pixel format, 0 for none. */
extern int SDL_OS3_GPU_FormatOfPixfmt(unsigned long pixfmt);
/* OpenGPU's format for an SDL pixel format, 0 for none. The pixels are as
   SDL keeps them in memory, so a packed format maps by this machine's byte
   order (on the Amiga, SDL_PIXELFORMAT_ARGB8888 is OpenGPU's ARGB32). */
extern int SDL_OS3_GPU_FormatOfSDL(SDL_PixelFormat format);

/* Showing a window's surface with OpenGPU, split so that the copy can go at
   the end of a batch someone else has built (the opengpu renderer puts it
   after the frame's drawing, so a frame is one submit and one wait).

   SDL_OS3_GPU_PresentAppend locks the window's layer and the screen's
   bitmap and adds to b: the screen's bitmap in dst_slot, the surface in
   src_slot, and a copy of the rects (in window pixels) through each visible
   part of the layer, inside the window's borders. More than
   SDL_OS3_GPU_MAXRECTS rects are joined into one. It returns 1 when it
   added them: send b, wait for it, then call SDL_OS3_GPU_PresentEnd, which
   lets the bitmap go, draws the covered parts into the layer's backing
   store with the CPU (a smart refresh window's), and lets the layer go.
   0: OpenGPU can't show this window (nothing is held, b unchanged); the
   caller draws it as before. SDL_OS3_GPU_NOROOM: b has no room for it
   (nothing is held, b unchanged); send b and try again with an empty one.
   SDL_OS3_GPU_Abandon lets go what PresentAppend holds when b wasn't sent. */
struct OGPUBatch;
struct Layer;
#define SDL_OS3_GPU_MAXRECTS 8
#define SDL_OS3_GPU_NOROOM   (-1)

typedef struct SDL_OS3_GPUPresent
{
    SDL_Window *window;
    struct Layer *layer;
    void *bmlock;
    SDL_Surface *surface;
    int ox, oy;          /* the window's inside, in its layer */
    SDL_Rect rects[SDL_OS3_GPU_MAXRECTS];
    int numrects;
    bool covered;        /* a part of the window is behind another */
} SDL_OS3_GPUPresent;

extern int SDL_OS3_GPU_PresentAppend(SDL_Window *window, SDL_Surface *surface, struct OGPUBatch *b,
                                     int dst_slot, int src_slot, const SDL_Rect *rects, int numrects,
                                     SDL_OS3_GPUPresent *p);
extern void SDL_OS3_GPU_PresentEnd(SDL_OS3_GPUPresent *p);
extern void SDL_OS3_GPU_Abandon(SDL_OS3_GPUPresent *p);

/* The window framebuffer's present (OS3_UpdateWindowFramebuffer): the rects
   of the window's surface through OpenGPU in a batch of their own, waited
   for. false: not shown (the caller uses WritePixelArray). */
extern bool SDL_OS3_GPU_Present(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects);

#endif /* SDL_VIDEO_RENDER_OPENGPU */
#endif /* SDL_os3gpu_h_ */
