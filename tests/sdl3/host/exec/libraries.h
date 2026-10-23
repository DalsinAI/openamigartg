/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * exec/libraries.h's struct Library, with what stubs/sdl3 reads, for its
 * host test (tests/sdl3/stub_nomodule.c). */
#ifndef EXEC_LIBRARIES_H
#define EXEC_LIBRARIES_H
#include <exec/types.h>
struct Library {
    UWORD lib_Version;
    UWORD lib_Revision;
};
#endif
