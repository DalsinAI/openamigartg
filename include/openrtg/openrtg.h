/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * openrtg.library's public interface (version 0.1, phase 2's first part). */
#ifndef OPENRTG_OPENRTG_H
#define OPENRTG_OPENRTG_H

#define OPENRTG_NAME    "openrtg.library"
#define OPENRTG_VERSION 0

/* One mode of a monitor (the same layout as library/modes.h's ortg_mode). */
struct OpenRTGMode {
    unsigned long  mode_id;
    unsigned short width, height;
    unsigned char  depth;          /* 8, 16 or 32 */
    unsigned char  format;         /* 0: 8-bit CLUT, 1: 16-bit, 2: 32-bit ARGB */
    unsigned char  standard;       /* 1: in the Standard list */
    char           name[32];       /* "OpenRTG.1: 1920x1080 32-bit" */
};

#endif
