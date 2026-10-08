/*
  SDL 2 on AmigaOS 3.x: opengpu.library, shared by the opengpu renderer and
  the window present.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).

  SDL_OPENGPU=0 (SetEnv, or SDL_SetHint) keeps SDL off OpenGPU entirely:
  the software renderer, and windows shown by WritePixelArray.
*/
#ifndef SDL_os3gpu_h_
#define SDL_os3gpu_h_

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

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

#endif /* SDL_VIDEO_RENDER_OPENGPU */
#endif
