/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The forms an attribute takes (the "Signature" column of the OpenAL 1.1
 * specification's attribute tables: f, 3f, fv, i, 3i, iv), and the errors
 * a call gets for a token (al_state.c says which section each comes from). */
#ifndef AL_ATTR_H
#define AL_ATTR_H

#include "al_internal.h"

#define AL_F 1
#define AL_3F 2
#define AL_FV 4
#define AL_I 8
#define AL_3I 16
#define AL_IV 32
#define AL_FORMS 63
#define AL_READONLY 64          /* query only (AL_SOURCE_STATE, AL_BUFFERS_QUEUED, ...) */

/* A token of any object (source, listener or buffer). */
ALboolean al_is_attribute(ALenum param);
/* The error for a set call in form given param, which its object takes in
 * forms (0: not one of its attributes); AL_NO_ERROR when it takes it. */
ALenum al_set_check(int forms, int form, ALenum param);
/* The same for a get call. */
ALenum al_get_check(int forms, int form);

#endif
