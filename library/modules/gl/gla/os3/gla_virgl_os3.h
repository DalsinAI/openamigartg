/* Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * virgl's transport on AmigaOS: request blocks as OGPU_OP_VIRGL batches
 * through opengpu.library (gla_virgl_os3.c). */
#ifndef GLA_VIRGL_OS3_H
#define GLA_VIRGL_OS3_H

#include "../gla_core.h"

/* Fill t when opengpu.library carries OGPU_OP_VIRGL (OGPU_Query says
 * "full": the Cradle's ring with virglrenderer); 0 when it doesn't, and
 * the caller uses softpipe. */
int gla_os3_virgl_transport(struct gla_virgl_transport *t);

#endif
