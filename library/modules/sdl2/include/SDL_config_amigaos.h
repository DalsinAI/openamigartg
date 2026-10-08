/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL 2's configuration for AmigaOS 3.x on the 68k (Open): SDL2.module,
 * built with the GCC 16 stove (C11, libnix). SDL_config.h includes it when
 * __AMIGAOS3__ is defined (patches/sdl2/0001). SDL checks most HAVE_ names
 * with #ifdef, so a feature that isn't there is left undefined, not 0. */
#ifndef SDL_config_amigaos_h_
#define SDL_config_amigaos_h_
#define SDL_config_h_

#include "SDL_platform.h"

/* The C library: libnix */
#define HAVE_STDARG_H    1
#define HAVE_STDDEF_H    1
#define HAVE_STDINT_H    1
#define HAVE_STDLIB_H    1
#define HAVE_STDIO_H     1
#define HAVE_STRING_H    1
#define HAVE_CTYPE_H     1
#define HAVE_MATH_H      1
#define HAVE_FLOAT_H     1
#define HAVE_LIMITS_H    1

#define HAVE_MALLOC      1
#define HAVE_CALLOC      1
#define HAVE_REALLOC     1
#define HAVE_FREE        1
#define HAVE_QSORT       1
#define HAVE_ABS         1
#define HAVE_MEMSET      1
#define HAVE_MEMCPY      1
#define HAVE_MEMMOVE     1
#define HAVE_MEMCMP      1
#define HAVE_STRLEN      1
#define HAVE_STRLCPY     1
#define HAVE_STRDUP      1
#define HAVE_STRCHR      1
#define HAVE_STRRCHR     1
#define HAVE_STRSTR      1
#define HAVE_STRTOL      1
#define HAVE_STRTOUL     1
#define HAVE_STRTOLL     1
#define HAVE_STRTOULL    1
#define HAVE_STRTOD      1
#define HAVE_ATOI        1
#define HAVE_ATOF        1
#define HAVE_STRCMP      1
#define HAVE_STRNCMP     1
#define HAVE_STRCASECMP  1
#define HAVE_STRNCASECMP 1
#define HAVE_SSCANF      1
#define HAVE_VSSCANF     1
#define HAVE_SNPRINTF    1
#define HAVE_VSNPRINTF   1
/* No HAVE_GETENV or HAVE_SETENV: SDL keeps a program's own variables, and
   SDL_getenv reads AmigaDOS's local and global ones (patches/sdl2/0003). */

/* Maths: libnix's libm for the 68881/68040 FPU, float versions included */
#define HAVE_ACOS        1
#define HAVE_ACOSF       1
#define HAVE_ASIN        1
#define HAVE_ASINF       1
#define HAVE_ATAN        1
#define HAVE_ATANF       1
#define HAVE_ATAN2       1
#define HAVE_ATAN2F      1
#define HAVE_CEIL        1
#define HAVE_CEILF       1
#define HAVE_COPYSIGN    1
#define HAVE_COPYSIGNF   1
#define HAVE_COS         1
#define HAVE_COSF        1
#define HAVE_EXP         1
#define HAVE_EXPF        1
#define HAVE_FABS        1
#define HAVE_FABSF       1
#define HAVE_FLOOR       1
#define HAVE_FLOORF      1
#define HAVE_FMOD        1
#define HAVE_FMODF       1
#define HAVE_LOG         1
#define HAVE_LOGF        1
#define HAVE_LOG10       1
#define HAVE_LOG10F      1
#define HAVE_LROUND      1
#define HAVE_LROUNDF     1
#define HAVE_POW         1
#define HAVE_POWF        1
#define HAVE_ROUND       1
#define HAVE_ROUNDF      1
#define HAVE_SCALBN      1
#define HAVE_SCALBNF     1
#define HAVE_SIN         1
#define HAVE_SINF        1
#define HAVE_SQRT        1
#define HAVE_SQRTF       1
#define HAVE_TAN         1
#define HAVE_TANF        1
#define HAVE_TRUNC       1
#define HAVE_TRUNCF      1

#define SDL_BYTEORDER    SDL_BIG_ENDIAN

/* Atomics: no GCC builtins (they call __atomic_* functions, and CAS and TAS
   fail on Chip RAM); SDL's spinlocks use Forbid() (patches/sdl2/0001). */

/* Video: Intuition windows and screens; RTG through the CyberGraphX API
   (OpenRTG's cybergraphics.library), AGA through c2p. */
#define SDL_VIDEO_DRIVER_AMIGAOS3   1
#define SDL_VIDEO_DRIVER_DUMMY      1

/* Renderers: opengpu first when the build has OpenGPU's headers (the
   Makefile defines SDL_VIDEO_RENDER_OPENGPU), then software. */
#define SDL_VIDEO_RENDER_SW         1

/* Audio: AHI, then Paula's audio.device. */
#define SDL_AUDIO_DRIVER_AHI        1
#define SDL_AUDIO_DRIVER_PAULA      1
#define SDL_AUDIO_DRIVER_DUMMY      1

#define SDL_THREAD_AMIGAOS3         1
#define SDL_TIMER_AMIGAOS3          1
#define SDL_JOYSTICK_AMIGAOS3       1
#define SDL_FILESYSTEM_AMIGAOS3     1

#define SDL_HAPTIC_DISABLED         1
#define SDL_HIDAPI_DISABLED         1
#define SDL_SENSOR_DISABLED         1
#define SDL_LOADSO_DISABLED         1
#define SDL_POWER_DISABLED          1
#define SDL_LOCALE_DISABLED         1
#define SDL_MISC_DISABLED           1

#endif /* SDL_config_amigaos_h_ */
