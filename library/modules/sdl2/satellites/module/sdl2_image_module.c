/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_image.module: SDL2_image 2.8.12 (Zlib) as a shared, resident module
 * in LIBS:OpenGPU/ (stubs/sdl2/sat_module.h). Its pictures are decoded by
 * the ladder in ogpu_image.c: the x86 or ARM64 cores (media.decode/1) for
 * the heavy formats, SDL2_image's own decoders on this CPU, then the
 * Amiga's datatypes. */
#include "SDL_image.h"
#include "ogpu_image.h"

#define SAT_PROCS "SDL_image_procs.h"
#define SAT_CLOSE_WITH_SDL IMG_Quit();
#define SAT_CLOSE_ALWAYS ogpu_image_close();
#include "sat_module_body.h"
