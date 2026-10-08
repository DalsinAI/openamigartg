/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MiniGL's screen modes (mglGetSupportedScreenModes) and mglLockBack's
 * answer. */
#ifndef __MGL_MODES_H
#define __MGL_MODES_H

#define MGL_MAX_MODE 80

typedef struct {
    GLint id;                       /* for mglCreateContextFromID */
    GLint width, height;
    GLint bit_depth;
    char mode_name[MGL_MAX_MODE];
} MGLScreenMode;

typedef struct {
    ULONG width, height, depth;
    ULONG pixel_format;             /* CyberGraphX PIXFMT_ */
    void *base_address;
    ULONG pitch;                    /* bytes per row */
} MGLLockInfo;

typedef GLboolean (*MGLScreenModeCallback)(MGLScreenMode *);   /* GL_TRUE to stop */

#endif
