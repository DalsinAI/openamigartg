/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * File names in a satellite (SDL2_image.module, SDL2_mixer.module and the
 * static libraries): a name the caller gives is turned into the file's full
 * path, in the caller's own process, before it goes on to a decoder in
 * another process, to the cores or to datatypes.library
 * (library/modules/common/ogpu_path.h says why). Its own file: dos.library's
 * names (Open, Close, Lock...) stay out of SDL_mixer's and SDL_image's sources,
 * which use them for their own. */
#ifndef SAT_PATH_H
#define SAT_PATH_H

/* name as a full Amiga path in buf (size bytes), or name itself when it can't
 * be made one. */
const char *sat_abspath(const char *name, char *buf, unsigned long size);

/* Requesters off for the calling process (-1 as its window), around an open
 * that may fail on purpose; the value it returns goes back to
 * sat_requesters_restore. */
void *sat_requesters_off(void);
void sat_requesters_restore(void *saved);

#endif
