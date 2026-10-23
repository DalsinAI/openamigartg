/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * dos/dos.h's names that stubs/sdl3 uses, for its host test. */
#ifndef DOS_DOS_H
#define DOS_DOS_H
#include <exec/types.h>
typedef long BPTR;
#define BADDR(x) ((APTR)((x) << 2))
#define ERROR_OBJECT_NOT_FOUND 205
#endif
