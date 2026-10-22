/*
  SDL 2 on AmigaOS 3.x: opengpu.library, shared by the opengpu renderer and
  the window present.
  Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).

  SDL_OPENGPU=0 (SetEnv, or SDL_SetHint) keeps SDL off OpenGPU entirely:
  the software renderer, and windows shown by WritePixelArray.
*/
#ifndef SDL_os3gpu_h_
#define SDL_os3gpu_h_

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

#include "SDL_video.h"
#include "SDL_surface.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"
#include <exec/libraries.h>
#include <opengpu/opengpu.h>
#include <opengpu/build.h>

/* SDL's own base, so a program that opens opengpu.library keeps its own. */
extern struct Library *SDL_OS3_OpenGPUBase;
#define OPENGPU_BASE_NAME SDL_OS3_OpenGPUBase
#include <inline/opengpu.h>

/* Opens opengpu.library (counted). SDL_FALSE when it isn't installed or
   SDL_OPENGPU=0. */
extern SDL_bool SDL_OS3_GPU_Open(void);
extern void SDL_OS3_GPU_Close(void);

/* OpenGPU's slot format for a CyberGraphX pixel format, 0 for none. */
extern int SDL_OS3_GPU_FormatOfPixfmt(unsigned long pixfmt);
/* OpenGPU's slot format for an SDL pixel format, 0 for none. */
extern int SDL_OS3_GPU_FormatOfSDL(Uint32 format);

/* Showing a window's surface with OpenGPU, split so that the copy can go at
   the end of a batch someone else has built (the opengpu renderer puts it
   after the frame's drawing, so a frame is one submit and one wait).

   SDL_OS3_GPU_PresentAppend locks the window's layer and the screen's
   bitmap and adds to b: the screen's bitmap in dst_slot, the surface in
   src_slot, and a copy of the rects (in window pixels) through each visible
   part of the layer. More than SDL_OS3_GPU_MAXRECTS rects are joined into
   one. It returns 1 when it added them: send b, wait for it, then call
   SDL_OS3_GPU_PresentEnd, which lets the bitmap go, draws the covered parts
   into the layer's backing store with the CPU, and lets the layer go.
   0: OpenGPU can't show this window (nothing is held, b unchanged); the
   caller draws it as before. SDL_OS3_GPU_NOROOM: b has no room for it
   (nothing is held, b unchanged); send b and try again with an empty one. */
struct OGPUBatch;
struct Layer;
#define SDL_OS3_GPU_MAXRECTS 8
#define SDL_OS3_GPU_NOROOM   (-1)

typedef struct SDL_OS3_GPUPresent {
    SDL_Window *window;
    struct Layer *layer;
    void *bmlock;
    SDL_Surface *surface;
    SDL_Rect rects[SDL_OS3_GPU_MAXRECTS];
    int numrects;
    SDL_bool covered;    /* a part of the window is behind another */
} SDL_OS3_GPUPresent;

extern int SDL_OS3_GPU_PresentAppend(SDL_Window *window, SDL_Surface *surface, struct OGPUBatch *b,
                                     int dst_slot, int src_slot, const SDL_Rect *rects, int numrects,
                                     SDL_OS3_GPUPresent *p);
extern void SDL_OS3_GPU_PresentEnd(SDL_OS3_GPUPresent *p);

#endif /* SDL_VIDEO_RENDER_OPENGPU */
#endif
