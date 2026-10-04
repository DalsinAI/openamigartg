/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
#ifndef OPENRTG_DISPLAYDB_H
#define OPENRTG_DISPLAYDB_H
#include <exec/libraries.h>
#include "modes.h"
/* Turns OpenRTG's answers in the display database on or off (patching on
 * first use; never unpatched). tables[1..4] are the monitors' mode tables. */
int ortg_displaydb(struct Library *gfx, struct ortg_mode_table **tables, int on);
#endif
