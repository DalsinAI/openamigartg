/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The display database's mode table: the modes each OpenRTG monitor offers,
 * built once per monitor, so NextDisplayInfo, FindDisplayInfo,
 * GetDisplayInfoData and BestModeIDA answer from a table and never work a
 * mode out per call (DESIGN.md, section 4). Plain C with no Amiga headers, so
 * the same file builds for the library and for the host tests.
 */
#ifndef OPENRTG_MODES_H
#define OPENRTG_MODES_H

#include <stdint.h>

#define ORTG_MAX_MONITORS 4          /* RTG monitors 1 to 4; monitor 0 is AGA */
#define ORTG_MAX_MODES    160
#define ORTG_NAME_LEN     32         /* graphics.library's DISPLAYNAMELEN */

/* Pixel formats a board can show. */
enum ortg_format { ORTG_CLUT8 = 0, ORTG_RGB16 = 1, ORTG_ARGB32 = 2 };
#define ORTG_FORMATS 3

/* What a board can show: its video RAM, the largest picture, and which
 * formats (bit n = enum ortg_format n). */
struct ortg_caps {
    uint32_t vram_bytes;
    uint16_t max_width, max_height;
    uint8_t  formats;
};

struct ortg_mode {
    uint32_t mode_id;
    uint16_t width, height;
    uint8_t  depth;                  /* 8, 16 or 32 */
    uint8_t  format;                 /* enum ortg_format */
    uint8_t  standard;               /* 1: in the Standard list */
    char     name[ORTG_NAME_LEN];    /* "OpenRTG.1: 1920x1080 32-bit" */
};

struct ortg_mode_table {
    int monitor;                     /* 1 to 4 */
    int all;                         /* 0: Standard (the default), 1: All */
    int count;                       /* modes offered, in list order */
    struct ortg_mode modes[ORTG_MAX_MODES];
    int full_count;                  /* every mode the board shows (All) */
    struct ortg_mode full[ORTG_MAX_MODES];
};

/* The monitor part of a monitor's ModeIDs (graphics.library's
 * MONITOR_ID_MASK, 0xFFFF1000, keeps all of a monitor's modes together). */
uint32_t ortg_monitor_id(int monitor);

/* Builds the table. Returns the number of modes offered, or -1 for a bad
 * monitor number. */
int ortg_build_modes(struct ortg_mode_table *t, int monitor, const struct ortg_caps *caps, int all);

/* Any mode the board shows, listed or not, so a saved ModeID from the All
 * list still works while Standard is chosen. NULL if the ModeID isn't one
 * of this monitor's. */
const struct ortg_mode *ortg_find_mode(const struct ortg_mode_table *t, uint32_t mode_id);

/* BestModeIDA's answer among the listed modes: the smallest that is at
 * least width x height with at least the depth asked for; if none is that
 * big, the largest with that depth; if no mode has that depth, the deepest.
 * 0 when the table is empty. */
uint32_t ortg_best_mode(const struct ortg_mode_table *t, int width, int height, int depth);

#endif
