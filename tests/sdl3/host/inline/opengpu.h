/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * opengpu.library's module calls for stubs/sdl3's host test, through the
 * base the stub names (OPENGPU_BASE_NAME), as sfdc's inline header has them. */
#ifndef INLINE_OPENGPU_H
#define INLINE_OPENGPU_H
#include <exec/types.h>
struct Library;
APTR test_ModuleOpen(struct Library *base, CONST_STRPTR name, ULONG version, APTR *table);
void test_ModuleClose(struct Library *base, APTR handle);
#define OGPU_ModuleOpen(name, version, table) test_ModuleOpen(OPENGPU_BASE_NAME, (name), (version), (table))
#define OGPU_ModuleClose(handle) test_ModuleClose(OPENGPU_BASE_NAME, (handle))
#endif
