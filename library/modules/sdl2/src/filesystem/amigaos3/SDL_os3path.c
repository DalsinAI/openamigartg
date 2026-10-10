/* Copyright (c) 2026 Dalsin Limited. OpenGPU's SDL 2, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * See SDL_os3path.h. Its own file: dos.library's names (Lock, AddPart...)
 * stay out of SDL's headers. */
#include "../../SDL_internal.h"

#if SDL_FILESYSTEM_AMIGAOS3

#include "ogpu_path.h"
#include "SDL_os3path.h"

const char *SDL_OS3_AbsPath(const char *name, char *buf, size_t size)
{
    return ogpu_abspath(name, buf, (ULONG)size);
}

#endif
