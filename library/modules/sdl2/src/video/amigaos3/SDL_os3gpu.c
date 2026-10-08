/*
  SDL 2 on AmigaOS 3.x: opengpu.library, shared by the opengpu renderer and
  the window present.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

#include "SDL_hints.h"
#include <proto/exec.h>
#include <cybergraphx/cybergraphics.h>

#include "SDL_os3gpu.h"

struct Library *SDL_OS3_OpenGPUBase = NULL;
static int SDL_OS3_GPU_opens = 0;

SDL_bool SDL_OS3_GPU_Open(void)
{
    if (!SDL_GetHintBoolean("SDL_OPENGPU", SDL_TRUE)) {
        return SDL_FALSE;
    }
    if (!SDL_OS3_OpenGPUBase) {
        SDL_OS3_OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
        if (!SDL_OS3_OpenGPUBase) {
            return SDL_FALSE;
        }
    }
    SDL_OS3_GPU_opens++;
    return SDL_TRUE;
}

void SDL_OS3_GPU_Close(void)
{
    if (SDL_OS3_GPU_opens > 0 && --SDL_OS3_GPU_opens == 0 && SDL_OS3_OpenGPUBase) {
        CloseLibrary(SDL_OS3_OpenGPUBase);
        SDL_OS3_OpenGPUBase = NULL;
    }
}

int SDL_OS3_GPU_FormatOfPixfmt(unsigned long pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_ARGB32:
        return OGPU_FMT_ARGB32;
    case PIXFMT_RGB16:
        return OGPU_FMT_RGB565;
    default:
        return 0;
    }
}

#endif /* SDL_VIDEO_RENDER_OPENGPU */
