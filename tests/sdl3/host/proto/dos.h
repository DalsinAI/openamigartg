/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * dos.library's calls that stubs/sdl3 makes, answered by its host test
 * (tests/sdl3/stub_nomodule.c). */
#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <dos/dos.h>
#include <exec/libraries.h>
extern struct Library *DOSBase;
BPTR Output(void);
LONG FPuts(BPTR fh, CONST_STRPTR str);
LONG IoErr(void);
BPTR LoadSeg(CONST_STRPTR name);
void UnLoadSeg(BPTR seg);
#endif
