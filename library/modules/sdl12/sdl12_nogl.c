/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * No SDL_GL_ calls: the table for programs that don't ask for GL
 * (sdl12_amiga.c). Its own member of libSDL.a. */
#include "sdl12_amiga.h"

const struct sdl12amiga_sym SDL12Amiga_gl[1] = { { "", 0 } };
const unsigned SDL12Amiga_ngl = 0;
