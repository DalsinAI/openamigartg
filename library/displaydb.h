/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
#ifndef OPENRTG_DISPLAYDB_H
#define OPENRTG_DISPLAYDB_H
#include <exec/libraries.h>
#include "modes.h"
/* Turns OpenRTG's answers in the display database on or off (patching on
 * first use; never unpatched). tables[1..4] are the monitors' mode tables. */
int ortg_displaydb(struct Library *gfx, struct ortg_mode_table **tables, int on);
/* 0.17: the table's Native modes were made again (a new size): their
 * records are filled again at once. Called under Forbid. */
void ortg_displaydb_native(const struct ortg_mode_table *t);
/* 0.17 (openrtg_lib.c): the Native modes made again when the host has asked
 * for a new size since; monitor 0 for every monitor. One register read when
 * nothing changed; safe from any task, never waits. */
void ortg_native_check(int monitor);
#endif
