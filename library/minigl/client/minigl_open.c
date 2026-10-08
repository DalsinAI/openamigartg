/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libminigl.a: MiniGLOpen opens minigl.library (MINIGL_VERSION or later)
 * and takes its table, which must be of this header's ABI and at least this
 * header's size; MiniGLClose lets it go. */
#include <proto/exec.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>

/* minigl_getdispatch.s: the library's one function, at -30. */
extern const MGLDispatchTable *MiniGLGetDispatchTableLVO(void);

BOOL MiniGLOpen(void)
{
    const MGLDispatchTable *d;
    if (MiniGLBase && MiniGLDispatch) return TRUE;
    if (!MiniGLBase) {
        MiniGLBase = OpenLibrary((CONST_STRPTR)MINIGLNAME, MINIGL_VERSION);
        if (!MiniGLBase) return FALSE;
    }
    d = MiniGLGetDispatchTableLVO();
    if (!d || d->abiVersion != MINIGL_DISPATCH_ABI_VERSION || d->structSize < sizeof(MGLDispatchTable)) {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
        MiniGLDispatch = 0;
        return FALSE;
    }
    MiniGLDispatch = d;
    return TRUE;
}

void MiniGLClose(void)
{
    MiniGLDispatch = 0;
    if (MiniGLBase) {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
    }
}
