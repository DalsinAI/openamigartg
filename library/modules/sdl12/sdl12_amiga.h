/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * sdl12-compat's link to SDL 2 on AmigaOS 3: the tables (sdl12_amiga.c). */
#ifndef SDL12_AMIGA_H
#define SDL12_AMIGA_H

struct sdl12amiga_sym {
    const char *name;           /* SDL 2's name: "SDL_Init" */
    void *fn;                   /* libSDL.a's: SDL2X_Init */
};

extern const struct sdl12amiga_sym SDL12Amiga_syms[];
extern const unsigned SDL12Amiga_nsyms;
extern const struct sdl12amiga_sym SDL12Amiga_gl[];
extern const unsigned SDL12Amiga_ngl;

#endif
