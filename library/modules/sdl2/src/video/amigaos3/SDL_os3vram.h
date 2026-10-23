/*
  SDL 2 on AmigaOS 3.x: SDL's window surface and the opengpu renderer's
  textures in an OpenRTG board's video RAM, so OpenGPU's GPU draws in them.
  Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).

  amigachrome's docs/design/Design-OpenGPU-2D-On-The-GPU.md 5.4 B (the
  user's decision, 17 October 2026). OpenRTG 0.19 puts a bitmap asked for
  with BMF_DISPLAYABLE, whose friend is in a board's video RAM, in that
  board's video RAM while there is room (openamigartg #94). Everything else
  stays in Fast RAM, as before: a screen that isn't a 32-bit OpenRTG one, a
  board without room, a bitmap OpenRTG put in Fast RAM after all.

  SDL_OPENGPU_VRAM=0 (SetEnv, or SDL_SetHint) keeps them in Fast RAM;
  SDL_OPENGPU=0 does too.
*/
#ifndef SDL_os3vram_h_
#define SDL_os3vram_h_

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

#include "../SDL_sysvideo.h"

struct Window;
struct BitMap;

/* A w x h 32-bit bitmap in the video RAM of the board that shows win's
   screen, when that is a 32-bit OpenRTG screen and the board has room; its
   pixels and pitch are LockBitMapTags'. NULL otherwise, with nothing kept. */
extern struct BitMap *SDL_OS3_VRAM_AllocBitMap(struct Window *win, int w, int h, void **pixels, int *pitch);
extern void SDL_OS3_VRAM_FreeBitMap(struct BitMap *bm);

/* A w x h surface of format (ARGB8888 or RGB888) in that video RAM, for
   window's screen; NULL when it can't be there (the caller then makes its
   own in Fast RAM). SDL_OS3_VRAM_FreeSurface frees any surface, and the
   bitmap under it when it is one of these. */
extern SDL_Surface *SDL_OS3_VRAM_CreateSurface(SDL_Window *window, int w, int h, Uint32 format);
extern void SDL_OS3_VRAM_FreeSurface(SDL_Surface *surface);

/* SDL_os3framebuffer.c: the window surface in video RAM, around the back
   end's framebuffer functions (OS3_CreateDevice calls it once they are set);
   and the Intuition window of one of this back end's SDL windows. */
extern void SDL_OS3_VRAM_HookFramebuffer(SDL_VideoDevice *device);
extern struct Window *SDL_OS3_IntuitionWindow(SDL_Window *window);

#endif /* SDL_VIDEO_RENDER_OPENGPU */
#endif
