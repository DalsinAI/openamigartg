/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
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

/* The Native place (0.17, the native screen mode's step 3): the size the
 * host's window asks for (ACRTG v5's WANT_W and WANT_H), in 8, 16 and
 * 32-bit. It is the last place in SIZES, so its ModeIDs are 0x6n221000,
 * 0x6n221100 and 0x6n221200 for good. */
#define ORTG_NATIVE_PLACE   0x22
#define ORTG_IS_NATIVE(id)  ((((id) >> 16) & 0xFFUL) == ORTG_NATIVE_PLACE && ((id) & 0x1000UL))   /* as openrtg.h */
/* The Native modes always come last in the table, after every other mode,
 * so the others keep their places when the Native size comes or goes. */
#define ORTG_NATIVE_MODES 3

/* What a board can show: its video RAM, the largest picture, and which
 * formats (bit n = enum ortg_format n). A board that can be asked for a
 * size (ACRTG v5 on) sets native; native_w and native_h are the size asked
 * for, 0 when none is (no Native modes then). */
struct ortg_caps {
    uint32_t vram_bytes;
    uint16_t max_width, max_height;
    uint8_t  formats;
    uint8_t  native;
    uint16_t native_w, native_h;
    uint16_t display_w, display_h;   /* the board's display (ACRTG v6 on), 0 when not known */
};

struct ortg_mode {
    uint32_t mode_id;
    uint16_t width, height;
    uint8_t  depth;                  /* 8, 16 or 32 */
    uint8_t  format;                 /* enum ortg_format */
    uint8_t  standard;               /* 1: in the Standard list */
    uint8_t  native;                 /* 1: the Native place, its size the host's */
    char     name[ORTG_NAME_LEN];    /* "OpenRTG.1: 1920x1080 32-bit", "OpenRTG.1: Native 32-bit" */
};

struct ortg_mode_table {
    int monitor;                     /* 1 to 4 */
    int all;                         /* 0: Standard (the default), 1: All */
    int native;                      /* 1: the board can be asked for a size (Native modes may come) */
    uint16_t max_width, max_height;  /* the board's largest picture, as caps had it */
    uint16_t display_w, display_h;   /* the board's display, as caps had it (0: not known) */
    int count;                       /* modes offered, in list order */
    struct ortg_mode modes[ORTG_MAX_MODES];
    int full_count;                  /* every mode the board shows (All); the Native ones last */
    struct ortg_mode full[ORTG_MAX_MODES];
};

/* The monitor part of a monitor's ModeIDs: their top byte (0x6n). */
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
 * Never a Native mode: a program that asks for a size must never get a
 * screen whose size the host changes (0.17). 0 when the table is empty. */
uint32_t ortg_best_mode(const struct ortg_mode_table *t, int width, int height, int depth);

/* The monitor's preferred mode in a format (MonitorInfo's PreferredModeID):
 * the mode at the board's display size, not a Native one, listed or not.
 * 0 when the display is not known or no mode has its size in that format. */
uint32_t ortg_preferred_mode(const struct ortg_mode_table *t, int format);

/* The ModeID of the Native place in a format; and the slot a mode's display
 * database record has (0 to ORTG_MAX_MODES - 1): a mode's own place in the
 * table, but the Native modes' always the last three, so their records stay
 * where they are while the Native size comes and goes. */
uint32_t ortg_native_id(int monitor, int format);
int ortg_record_slot(const struct ortg_mode_table *t, const struct ortg_mode *m);

#endif
