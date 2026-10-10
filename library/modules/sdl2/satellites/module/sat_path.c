/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * See sat_path.h. */
#include "ogpu_path.h"
#include "sat_path.h"

const char *sat_abspath(const char *name, char *buf, unsigned long size)
{
    return ogpu_abspath(name, buf, (ULONG)size);
}

void *sat_requesters_off(void)
{
    return ogpu_quiet_requesters();
}

void sat_requesters_restore(void *saved)
{
    ogpu_restore_requesters((APTR)saved);
}
