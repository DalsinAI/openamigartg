/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MiniGL's context, as programs see it: a handle. What is inside belongs to
 * minigl.library. Also the types of the functions mglMainLoop calls. */
#ifndef MGL_CONTEXT_H
#define MGL_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

struct Window;
struct BitMap;

struct GLcontext_t;
typedef struct GLcontext_t *GLcontext;

typedef enum {
    MGLKEY_F1, MGLKEY_F2, MGLKEY_F3, MGLKEY_F4, MGLKEY_F5, MGLKEY_F6, MGLKEY_F7, MGLKEY_F8,
    MGLKEY_F9, MGLKEY_F10,
    MGLKEY_CUP, MGLKEY_CDOWN, MGLKEY_CLEFT, MGLKEY_CRIGHT
} MGLspecial;

typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial special_key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);   /* MGL_BUTTON_ bits */
typedef void (*IdleFn)(void);

#ifdef __cplusplus
}
#endif

#endif
