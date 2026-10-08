/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Work for the x86 or ARM64 cores, from SDL's satellites: a named service
 * ("media.decode/1", DalsinAI/openamigaservice docs/MEDIA_DECODE.md) through
 * openservice.device, which takes it to the services card (AmigaChrome) or
 * a paired Cradle on the LAN. When neither offers the service,
 * sat_service_open fails and the caller does the work on this CPU.
 *
 * Unlike a datatype's, these requests are not tied to one task: SDL2_mixer
 * opens a music in the program's task and decodes the rest of it from
 * SDL's audio thread. So the reply port signals nobody until a task waits
 * on it; the waiting task borrows a signal of its own for the wait
 * (sat_service_wait). Any task may start, poll, wait for and close a
 * request, one at a time. */
#ifndef SAT_SERVICE_H
#define SAT_SERVICE_H

#include <exec/types.h>
#include <exec/ports.h>
#include "devices/openservice.h"

struct sat_svc {
    struct sat_svc *next;           /* every open one, for sat_service_close_all */
    struct MsgPort port;            /* PA_IGNORE, but while a task waits */
    struct OSRequest io;
    UWORD handle;
    UBYTE dev_open, svc_open, busy;
    UBYTE lost;                     /* a request never came back: unusable, never freed */
};

/* How long a request may take before it is given up: the caller then does
 * the work itself. A picture or two seconds of sound take well under one. */
#define SAT_SERVICE_TIMEOUT_MS 10000

/* NULL when openservice.device isn't installed or nothing offers name. */
struct sat_svc *sat_service_open(const char *name);
/* Starts one request (the last one must be finished): buffer i is written
 * by the service when flags bit i is set. */
void sat_service_start(struct sat_svc *s, UWORD op, ULONG arg, ULONG flags,
                       const struct OSBuffer buf[4], const ULONG extra[4]);
/* 1 when no request is running (the last one has finished). */
int sat_service_poll(struct sat_svc *s);
/* Waits for the request; its status (0 OK, OSERR_* or the service's own).
 * After SAT_SERVICE_TIMEOUT_MS it is aborted (OSERR_CANCELLED), or, when
 * even that isn't answered, the sat_svc is lost (OSERR_LOST) and every later
 * call on it fails at once. */
LONG sat_service_wait(struct sat_svc *s, ULONG *result, ULONG *aux);
/* start and wait */
LONG sat_service_call(struct sat_svc *s, UWORD op, ULONG arg, ULONG flags,
                      const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux);
/* Waits for a running request, if any, and drops its answer. */
void sat_service_abort(struct sat_svc *s);
void sat_service_close(struct sat_svc *s);
/* Every one still open (the program ends). */
void sat_service_close_all(void);

/* A file name's extension as media.decode's hint: up to four letters in
 * capitals, space-padded ("photo.tga" -> 'TGA '); 0 when none. */
ULONG sat_service_hint(const char *name);

#endif
