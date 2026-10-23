/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* SDL 3's configuration for AmigaOS 3.x on the 68k (Open): SDL3.module and
   libSDL3_static.a, built with the GCC 16 stove and libnix. Written by the
   AmigaChrome Team, 2026, from SDL3_68k's (Zlib) configuration, whose back
   ends it uses (library/modules/sdl3/UPSTREAM.json). OpenGL waits for
   OpenGPU's GL module (SDL_OS3_SetGLBridge, as in SDL2.module). */
#ifndef SDL_build_config_amigaos3_h_
#define SDL_build_config_amigaos3_h_
#define SDL_build_config_h_

#include <SDL3/SDL_platform_defines.h>

/* libnix's C library. */
#define HAVE_LIBC 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_STRING_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MATH_H 1
#define HAVE_FLOAT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRDUP 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_SSCANF 1
#define HAVE_ACOS 1
#define HAVE_ASIN 1
#define HAVE_ATAN 1
#define HAVE_ATAN2 1
#define HAVE_EXP 1
#define HAVE_FABS 1
#define HAVE_FLOOR 1
#define HAVE_FMOD 1
#define HAVE_LOG 1
#define HAVE_POW 1
#define HAVE_SQRT 1
#define HAVE_TAN 1

/* The Amiga back ends: SDL3_68k's (patches/amigaos3). */
#define SDL_VIDEO_DRIVER_AMIGAOS3 1
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_VIDEO_RENDER_SW 1
#define SDL_AMIGAOS3_SW_ONLY 1     /* SDL3_68k's switch: no MiniGL */
#define SDL_AUDIO_DRIVER_AMIGAOS3 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_JOYSTICK_AMIGAOS3 1
#define SDL_THREAD_AMIGAOS3 1
#define SDL_TIMER_AMIGAOS3 1
#define SDL_TIME_AMIGAOS3 1
#define SDL_FILESYSTEM_AMIGAOS3 1
#define SDL_FSOPS_AMIGAOS3 1

/* What the Amiga doesn't have, or SDL 3 doesn't have for it yet. */
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_CAMERA_DRIVER_DUMMY 1
#define SDL_LOADSO_DUMMY 1
#define SDL_PROCESS_DUMMY 1
#define SDL_TRAY_DUMMY 1
#define SDL_GPU_DISABLED 1

#endif /* SDL_build_config_amigaos3_h_ */
