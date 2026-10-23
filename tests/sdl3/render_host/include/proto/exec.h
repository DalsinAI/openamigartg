/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * exec.library's calls SDL_os3gpu.c makes, answered by the host harness
 * (tests/sdl3/render_host/host.c). */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/libraries.h>
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *lib);
#endif
