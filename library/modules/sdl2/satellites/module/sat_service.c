/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openservice.device for SDL's satellites (sat_service.h). */
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/timer.h>
#include <string.h>

#include "sat_service.h"

#ifdef SAT_TRACE
/* A build for the lab only: -DSAT_TRACE='"FILE"' adds each step to FILE. */
#include <dos/dos.h>
static void trace(const char *what, ULONG a, ULONG b)
{
    static const char hex[] = "0123456789abcdef";
    char line[96];
    int n = 0, i;
    BPTR f;
    ULONG v[4];
    struct DateStamp ds;
    DateStamp(&ds);
    v[0] = (ULONG)(ds.ds_Minute % 60) * 3000 + (ULONG)ds.ds_Tick;   /* ticks within the hour */
    v[1] = (ULONG)FindTask(NULL);
    v[2] = a;
    v[3] = b;
    while (*what && n < 40) line[n++] = *what++;
    for (i = 0; i < 4; i++) {
        int k;
        line[n++] = ' ';
        for (k = 28; k >= 0; k -= 4) line[n++] = hex[(v[i] >> k) & 15];
    }
    line[n++] = '\n';
    if ((f = Open((CONST_STRPTR)SAT_TRACE, MODE_READWRITE)) != 0) {
        Seek(f, 0, OFFSET_END);
        Write(f, line, n);
        Close(f);
    }
}
#define TRACE(w, a, b) trace(w, (ULONG)(a), (ULONG)(b))
#else
#define TRACE(w, a, b) ((void)0)
#endif

static struct sat_svc *open_list = NULL;
static struct SignalSemaphore list_lock;
static int list_ready = 0;

static void lock_list(void)
{
    Forbid();
    if (!list_ready) {
        InitSemaphore(&list_lock);
        list_ready = 1;
    }
    Permit();
    ObtainSemaphore(&list_lock);
}

/* The request has come back: take it off the port. Under Forbid. */
static void took(struct sat_svc *s)
{
    if (!(s->io.os_Req.io_Flags & IOF_QUICK) && s->io.os_Req.io_Message.mn_Node.ln_Type == NT_REPLYMSG) {
        Remove(&s->io.os_Req.io_Message.mn_Node);
        s->io.os_Req.io_Message.mn_Node.ln_Type = NT_FREEMSG;
    }
    s->busy = 0;
}

/* The device replies from its interrupt: read what it writes afresh each
   time, and keep the compiler from moving memory accesses across. */
#define BARRIER() __asm__ __volatile__("" : : : "memory")

static int returned(struct sat_svc *s)
{
    volatile UBYTE *flags = &s->io.os_Req.io_Flags;
    volatile UBYTE *type = &s->io.os_Req.io_Message.mn_Node.ln_Type;
    BARRIER();
    return (*flags & IOF_QUICK) || *type == NT_REPLYMSG;
}

void sat_service_start(struct sat_svc *s, UWORD op, ULONG arg, ULONG flags,
                       const struct OSBuffer buf[4], const ULONG extra[4])
{
    int i;
    if (s->lost) {
        return;                     /* sat_service_wait says OSERR_LOST */
    }
    s->io.os_Req.io_Command = OSCMD_CALL;
    s->io.os_Service = s->handle;
    s->io.os_Op = op;
    s->io.os_Arg = arg;
    s->io.os_Flags = flags;
    for (i = 0; i < 4; i++) {
        s->io.os_Buf[i].ob_Data = buf ? buf[i].ob_Data : NULL;
        s->io.os_Buf[i].ob_Length = buf ? buf[i].ob_Length : 0;
        s->io.os_Extra[i] = extra ? extra[i] : 0;
    }
    s->io.os_Status = 0;
    s->io.os_Result = 0;
    s->io.os_Aux = 0;
    s->busy = 1;
    TRACE("start", s, op);
    SendIO((struct IORequest *)&s->io);
    TRACE("sent", s, s->io.os_Req.io_Message.mn_Node.ln_Type);
}

int sat_service_poll(struct sat_svc *s)
{
    int done = 1;
    if (s->busy) {
        Forbid();
        if (returned(s)) {
            took(s);
        } else {
            done = 0;
        }
        Permit();
    }
    return done;
}

/* A timer for a wait, in the waiting task. NULL if it can't be had. */
struct wait_timer {
    struct MsgPort *port;
    struct timerequest *req;
};

static int timer_start(struct wait_timer *w, ULONG ms)
{
    w->port = CreateMsgPort();
    w->req = w->port ? (struct timerequest *)CreateIORequest(w->port, sizeof(*w->req)) : NULL;
    if (!w->req || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)w->req, 0) != 0) {
        if (w->req) {
            DeleteIORequest((struct IORequest *)w->req);
        }
        if (w->port) {
            DeleteMsgPort(w->port);
        }
        w->port = NULL;
        w->req = NULL;
        return 0;
    }
    w->req->tr_node.io_Command = TR_ADDREQUEST;
    w->req->tr_time.tv_secs = ms / 1000;
    w->req->tr_time.tv_micro = (ms % 1000) * 1000;
    SendIO((struct IORequest *)w->req);
    return 1;
}

static void timer_stop(struct wait_timer *w)
{
    if (w->req) {
        if (!CheckIO((struct IORequest *)w->req)) {
            AbortIO((struct IORequest *)w->req);
        }
        WaitIO((struct IORequest *)w->req);
        CloseDevice((struct IORequest *)w->req);
        DeleteIORequest((struct IORequest *)w->req);
        DeleteMsgPort(w->port);
    }
}

/* Until the request comes back or ms pass: 1 when it is back. The port
   signals this task while it waits. A reply before the port is set (from
   the device's worker) is seen by the check after it; one after it
   signals. */
static int wait_back(struct sat_svc *s, ULONG ms)
{
    struct wait_timer w;
    BYTE sig;
    ULONG mask;
    int timed = 0;

    if (returned(s)) {
        return 1;
    }
    sig = AllocSignal(-1);
    if (sig < 0 || !timer_start(&w, ms)) {
        /* no signal or timer: look every tick */
        ULONG ticks = ms / 20 + 1;
        if (sig >= 0) {
            FreeSignal(sig);
        }
        while (!returned(s) && ticks--) {
            Delay(1);
        }
        return returned(s);
    }
    mask = (1UL << sig) | (1UL << w.port->mp_SigBit);
    Disable();
    s->port.mp_SigTask = FindTask(NULL);
    s->port.mp_SigBit = sig;
    s->port.mp_Flags = PA_SIGNAL;
    Enable();
    BARRIER();
    while (!returned(s) && !timed) {
        ULONG got = Wait(mask);
        TRACE("woke", got, s->io.os_Req.io_Message.mn_Node.ln_Type);
        (void)got;
        timed = CheckIO((struct IORequest *)w.req) != NULL;
    }
    Disable();
    s->port.mp_Flags = PA_IGNORE;
    s->port.mp_SigTask = NULL;
    Enable();
    timer_stop(&w);
    FreeSignal(sig);
    return returned(s);
}

LONG sat_service_wait(struct sat_svc *s, ULONG *result, ULONG *aux)
{
    TRACE("wait", s, s->io.os_Req.io_Message.mn_Node.ln_Type);
    if (s->lost) {
        return OSERR_LOST;
    }
    if (s->busy && !wait_back(s, SAT_SERVICE_TIMEOUT_MS)) {
        /* No answer: ask the device to give it back, and wait a little more.
           If even that doesn't come, the request (and this sat_svc's memory)
           is left to the device for good: the caller does the work itself. */
        TRACE("timeout", s, s->io.os_Op);
        AbortIO((struct IORequest *)&s->io);
        if (!wait_back(s, 2000)) {
            s->lost = 1;
            return OSERR_LOST;
        }
        Forbid();
        took(s);
        Permit();
        return OSERR_CANCELLED;
    }
    if (s->busy) {
        Forbid();
        took(s);
        Permit();
    }
    TRACE("done", s, s->io.os_Status);
    if (result) {
        *result = s->io.os_Result;
    }
    if (aux) {
        *aux = s->io.os_Aux;
    }
    return s->io.os_Req.io_Error ? OSERR_LOST : s->io.os_Status;
}

LONG sat_service_call(struct sat_svc *s, UWORD op, ULONG arg, ULONG flags,
                      const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux)
{
    sat_service_start(s, op, arg, flags, buf, extra);
    return sat_service_wait(s, result, aux);
}

/* A running request is let finish (a decode takes a fraction of a second),
   with the wait's own time limit, rather than aborted. */
void sat_service_abort(struct sat_svc *s)
{
    if (s->busy) {
        sat_service_wait(s, NULL, NULL);
    }
}

/* A request of the device's own (open, close), with the same port. */
static LONG device_cmd(struct sat_svc *s, UWORD cmd, APTR data, ULONG len)
{
    if (s->lost) {
        return OSERR_LOST;
    }
    s->io.os_Req.io_Command = cmd;
    s->io.os_Service = s->handle;
    s->io.os_Buf[0].ob_Data = data;
    s->io.os_Buf[0].ob_Length = len;
    s->io.os_Buf[1].ob_Length = s->io.os_Buf[2].ob_Length = s->io.os_Buf[3].ob_Length = 0;
    s->io.os_Flags = 0;
    s->io.os_Status = 0;
    s->busy = 1;
    TRACE("cmd", s, cmd);
    SendIO((struct IORequest *)&s->io);
    return sat_service_wait(s, NULL, NULL);
}

struct sat_svc *sat_service_open(const char *name)
{
    struct sat_svc *s = AllocVec(sizeof(*s), MEMF_PUBLIC | MEMF_CLEAR);
    if (!s) {
        return NULL;
    }
    s->port.mp_Node.ln_Type = NT_MSGPORT;
    s->port.mp_Flags = PA_IGNORE;
    s->port.mp_MsgList.lh_Head = (struct Node *)&s->port.mp_MsgList.lh_Tail;
    s->port.mp_MsgList.lh_Tail = NULL;
    s->port.mp_MsgList.lh_TailPred = (struct Node *)&s->port.mp_MsgList.lh_Head;
    s->io.os_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    s->io.os_Req.io_Message.mn_ReplyPort = &s->port;
    s->io.os_Req.io_Message.mn_Length = sizeof(s->io);
    if (OpenDevice((CONST_STRPTR)OPENSERVICE_NAME, 0, (struct IORequest *)&s->io, 0) != 0) {
        FreeVec(s);
        return NULL;
    }
    s->dev_open = 1;
    if (device_cmd(s, OSCMD_OPEN, (APTR)name, strlen(name)) != OSERR_OK) {
        CloseDevice((struct IORequest *)&s->io);
        FreeVec(s);
        return NULL;
    }
    s->handle = (UWORD)s->io.os_Result;
    s->svc_open = 1;
    lock_list();
    s->next = open_list;
    open_list = s;
    ReleaseSemaphore(&list_lock);
    return s;
}

static void close_one(struct sat_svc *s)
{
    sat_service_abort(s);
    if (s->lost) {
        return;                     /* the device still has the request: leave it all */
    }
    if (s->svc_open) {
        device_cmd(s, OSCMD_CLOSE, NULL, 0);
    }
    if (s->dev_open) {
        CloseDevice((struct IORequest *)&s->io);
    }
    FreeVec(s);
}

void sat_service_close(struct sat_svc *s)
{
    struct sat_svc **p;
    if (!s) {
        return;
    }
    lock_list();
    for (p = &open_list; *p; p = &(*p)->next) {
        if (*p == s) {
            *p = s->next;
            break;
        }
    }
    ReleaseSemaphore(&list_lock);
    close_one(s);
}

void sat_service_close_all(void)
{
    struct sat_svc *s;
    if (!list_ready) {
        return;
    }
    lock_list();
    while ((s = open_list) != NULL) {
        open_list = s->next;
        close_one(s);
    }
    ReleaseSemaphore(&list_lock);
}

ULONG sat_service_hint(const char *name)
{
    const char *dot = NULL, *p;
    ULONG h = 0;
    int i;
    if (!name) {
        return 0;
    }
    for (p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
        } else if (*p == '/' || *p == ':') {
            dot = NULL;
        }
    }
    p = dot ? dot + 1 : name;   /* a bare type ("PNG") is its own hint */
    if (!*p || strlen(p) > 4) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        char c = *p ? *p++ : ' ';
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        h = (h << 8) | (UBYTE)c;
    }
    return h;
}
