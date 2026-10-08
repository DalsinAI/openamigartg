/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_image's decoding ladder on OpenGPU (patches/sdl2_image): who decodes
 * a picture that IMG_Load, IMG_Load_RW, IMG_LoadTyped_RW or IMG_LoadTexture*
 * is given.
 *   1. The x86 or ARM64 cores, through media.decode/1 on the services card
 *      or a paired Cradle, for the formats that cost a 68k most: JPEG,
 *      PNG (but paletted ones), WebP, AVIF, HEIC, JPEG XL and TIFF.
 *   2. SDL2_image's own decoders on this CPU, for the rest, and for all of
 *      them when no card or Cradle offers the service.
 *   3. media.decode/1 again, for a format SDL2_image doesn't know (PSD,
 *      EXR, camera RAW...).
 *   4. The Amiga's datatypes (IMG_Load with a file name), for a format only
 *      an installed datatype reads.
 * Rungs 1 and 3 give 32-bit surfaces: SDL_PIXELFORMAT_ARGB8888 when the
 * picture has alpha, else SDL_PIXELFORMAT_RGB888.
 *
 * SDL_IMAGE_DECODER (a hint, or SetEnv): "auto" (the default), "cpu" (only
 * SDL2_image's own decoders, exactly as upstream), "service" (rung 1 for
 * every format). */
#ifndef OGPU_IMAGE_H
#define OGPU_IMAGE_H

#include "SDL.h"

/* pass 0: rung 1; pass 1: rung 3. NULL (and src where it was) when the
   rung doesn't decode it. */
SDL_Surface *ogpu_image_load(SDL_RWops *src, const char *type, int pass);
/* Rung 4. */
SDL_Surface *ogpu_image_load_datatype(const char *file);
/* The IMG_INIT_ flags of want the service decodes (IMG_Init). */
int ogpu_image_init_flags(int want);
/* The service, closed (IMG_Quit, the program's end). */
void ogpu_image_close(void);

#endif
