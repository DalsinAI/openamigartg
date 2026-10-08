/*
  SDL 2 on AmigaOS 3.x: SDL_GL_* on OpenGPU's GL module.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).

  GL is the program's own libGL.a (GL.module: Mesa on the PC's graphics
  chip through virgl, or softpipe on the 68k), handed to the module by
  libSDL2.a's SDL2_gl.o (stubs/sdl2/sdl2_module.h, struct SDL2GLBridge).
  SDL_OPENGPU_GL=cpu (SetEnv, or SDL_SetHint) draws GL with softpipe even
  where the PC's graphics chip is there.
*/
#ifndef SDL_os3gl_h_
#define SDL_os3gl_h_

#include "../SDL_sysvideo.h"

extern int OS3_GL_LoadLibrary(_THIS, const char *path);
extern void *OS3_GL_GetProcAddress(_THIS, const char *proc);
extern void OS3_GL_UnloadLibrary(_THIS);
extern SDL_GLContext OS3_GL_CreateContext(_THIS, SDL_Window *window);
extern int OS3_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context);
extern int OS3_GL_SetSwapInterval(_THIS, int interval);
extern int OS3_GL_GetSwapInterval(_THIS);
extern int OS3_GL_SwapWindow(_THIS, SDL_Window *window);
extern void OS3_GL_DeleteContext(_THIS, SDL_GLContext context);
/* A window's GL buffer, when the window closes (OS3_DestroyWindow). */
extern void OS3_GL_DestroyWindow(_THIS, SDL_Window *window);

#endif
