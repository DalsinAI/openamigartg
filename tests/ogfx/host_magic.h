/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For the host run of OpenGfx's leaves (tests/ogfx/run.sh): the 68k tag
 * ac_magic.h puts before each leaf is 68k code, so on the host each leaf is
 * its C body under its own name. Included before ogfx_leaves.c. */
#define AC_MAGIC_H
#define AC_MAGIC(name, id)
#define AC_MAGIC_BODY(name) name
#define AC_MAGIC_KEEP __attribute__((used, noinline))
