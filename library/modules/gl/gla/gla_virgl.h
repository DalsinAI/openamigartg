/* Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * The GLA core's virgl winsys (gla_virgl.c). */
#ifndef GLA_VIRGL_H
#define GLA_VIRGL_H

#include "gla_core.h"

struct sw_winsys;
struct virgl_winsys;

/* Display targets come from sws; everything the host reads or writes from
 * t's memory; request blocks go through t. NULL when the host says no. */
struct virgl_winsys *gla_virgl_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t);

#endif
