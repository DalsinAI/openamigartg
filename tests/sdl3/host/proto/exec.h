/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * exec.library's calls that stubs/sdl3 makes, answered by its host test
 * (tests/sdl3/stub_nomodule.c). */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/execbase.h>
extern struct ExecBase *SysBase;
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *lib);
#endif
