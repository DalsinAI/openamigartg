/* Copyright (c) 2026 Dalsin Limited. OpenGPU's SDL 2, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * File names that mean the same in every process (library/modules/common/
 * ogpu_path.h). SDL_RWFromFile calls this first, so every SDL file call
 * (SDL_LoadBMP, SDL_LoadWAV, SDL_LoadFile, IMG_Load, Mix_LoadMUS,
 * TTF_OpenFont...) opens the full path, whatever process the file is read in
 * later. */
#ifndef SDL_OS3PATH_H
#define SDL_OS3PATH_H

#include <stddef.h>

/* name as a full Amiga path in buf, or name itself when it can't be made one
 * (a handler's stream, a name that isn't there and has no folder). */
extern const char *SDL_OS3_AbsPath(const char *name, char *buf, size_t size);

#endif
