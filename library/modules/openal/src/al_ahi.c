/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Mix's output on the Amiga: the mixed stream through ahi.device's unit 0
 * (AHI_DEFAULT_UNIT), the way SDL2.module's AHI driver plays
 * (library/modules/sdl2/src/audio/amigaos3/SDL_os3audio.c):
 *   - the device, its message port and both requests belong to a process of
 *     the output's own ("OpenAL Mix"), as a message port signals only the
 *     task that made it;
 *   - double buffered with ahir_Link: each update is mixed and sent linked
 *     to the one playing, then the process waits for the one before to end.
 *     The latency is two updates (ALC_REFRESH: 25 a second, 80 ms, unless
 *     the program asks for another);
 *   - 16-bit stereo (AHIST_S16S) at the device's rate. alcOpenDevice reads
 *     unit 0's rate from AHI's prefs (ENV:Sys/ahi.prefs), so AHI needn't
 *     resample again; ALC_FREQUENCY overrides it.
 * When the unit can't be opened, or a write comes back with an error (the
 * board taken by a program that allocated it itself), the device is
 * disconnected (ALC_EXT_disconnect): every source stops and ALC_CONNECTED
 * is ALC_FALSE.
 *
 * In OpenAL.module (built -fbaserel32) the process starts with the A4 of
 * the program's copy of the module (module_rt.h); in libopenal_static.a it
 * is a plain process. */
#include "al_internal.h"

#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>
#include <stdlib.h>
#include <string.h>

#ifdef AL_MODULE_BUILD
#include "module_rt.h"
#else
#define ogpu_module_thread_a4(child) ((void)(child))
#define OGPU_MODULE_THREAD_ENTRY(name, fn) \
    void name(void) { fn(); }
#endif

#define AL_AHI_UNIT AHI_DEFAULT_UNIT
#define AL_AHI_PRIORITY 5       /* above programs at 0, so the next update is ready in time */
#define AL_AHI_STACK 16384

struct al_ahi {
    ALCdevice *device;
    struct Task *thread;        /* the output's process while it runs */
    struct Task *waiter;        /* who waits for it to start, or to end */
    BYTE waiter_sig;
    volatile BYTE quit;
};

/* Unit 0's mixing rate from AHI's prefs (an IFF PREF file whose AHIU
 * chunks are struct AHIUnitPrefs), or 0. */
static ULONG al_ahi_prefs_frequency(ULONG unit)
{
    BPTR fh;
    UBYTE *buf;
    LONG n, p;
    ULONG freq = 0;
    buf = malloc(2048);
    if (!buf) {
        return 0;
    }
    fh = Open((CONST_STRPTR) "ENV:Sys/ahi.prefs", MODE_OLDFILE);
    if (!fh) {
        free(buf);
        return 0;
    }
    n = Read(fh, buf, 2048);
    Close(fh);
    if (n >= 12 && !memcmp(buf, "FORM", 4) && !memcmp(buf + 8, "PREF", 4)) {
        for (p = 12; p + 8 <= n;) {
            ULONG size = ((ULONG)buf[p + 4] << 24) | ((ULONG)buf[p + 5] << 16) | ((ULONG)buf[p + 6] << 8) | buf[p + 7];
            LONG data = p + 8;
            if (size > (ULONG)n) {
                break;
            }
            if (!memcmp(buf + p, "AHIU", 4) && size >= 12 && data + 12 <= n && buf[data] == unit) {
                freq = ((ULONG)buf[data + 8] << 24) | ((ULONG)buf[data + 9] << 16) | ((ULONG)buf[data + 10] << 8) | buf[data + 11];
            }
            p = data + (LONG)size + (LONG)(size & 1);
        }
    }
    free(buf);
    return freq;
}

/* Whether ahi.device opens at all (AHI_NO_UNIT, as SDL's driver asks). */
static int al_ahi_available(void)
{
    struct MsgPort *port;
    struct AHIRequest *req;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    int ok = 0;
    port = CreateMsgPort();
    if (!port) {
        return 0;
    }
    req = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest));
    if (req) {
        req->ahir_Version = 4;
        me->pr_WindowPtr = (APTR)-1L;
        if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)req, 0) == 0) {
            ok = 1;
            CloseDevice((struct IORequest *)req);
        }
        me->pr_WindowPtr = oldwin;
        DeleteIORequest((struct IORequest *)req);
    }
    DeleteMsgPort(port);
    return ok;
}

ALCboolean al_out_open(ALCdevice *d)
{
    struct al_ahi *o;
    if (!al_ahi_available()) {
        return ALC_FALSE;
    }
    o = calloc(1, sizeof *o);
    if (!o) {
        return ALC_FALSE;
    }
    o->device = d;
    d->out = o;
    d->frequency = al_ahi_prefs_frequency(AL_AHI_UNIT);
    return ALC_TRUE;
}

void al_out_close(ALCdevice *d)
{
    free(d->out);
    d->out = NULL;
}

/* The output's process. */
static void al_ahi_main(void)
{
    struct Task *me = FindTask(NULL);
    struct al_ahi *o = me->tc_UserData;
    ALCdevice *d = o->device;
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req[2] = { NULL, NULL }, *inflight = NULL, *sent = NULL;
    ALshort *buf[2] = { NULL, NULL };
    ULONG bytes = d->update_frames * 4;
    int cur = 0, open = 0, ok = 0, i;

    if (port) {
        req[0] = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest));
    }
    if (req[0]) {
        req[0]->ahir_Version = 4;
        open = OpenDevice((CONST_STRPTR)AHINAME, AL_AHI_UNIT, (struct IORequest *)req[0], 0) == 0;
    }
    if (open) {
        req[1] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);
        buf[0] = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
        buf[1] = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
        if (req[1] && buf[0] && buf[1]) {
            CopyMem(req[0], req[1], sizeof(struct AHIRequest));
            ok = 1;
        }
    }
    /* Started: the caller of alcCreateContext goes on. */
    Signal(o->waiter, 1UL << o->waiter_sig);
    if (!ok) {
        al_device_disconnect(d);
    }

    while (ok && !o->quit) {
        struct AHIRequest *r = req[cur];
        al_mix_render(d, buf[cur], d->update_frames);
        r->ahir_Std.io_Message.mn_Node.ln_Pri = 60;
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = buf[cur];
        r->ahir_Std.io_Length = bytes;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Frequency = d->frequency;
        r->ahir_Type = AHIST_S16S;
        r->ahir_Volume = 0x10000;
        r->ahir_Position = 0x8000;
        r->ahir_Link = inflight;        /* starts as soon as that one ends */
        SendIO((struct IORequest *)r);
        sent = r;
        /* The one before plays out: then its buffer is free again. */
        if (inflight) {
            while (!CheckIO((struct IORequest *)inflight) && !o->quit) {
                Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);
            }
            if (o->quit) {
                break;
            }
            WaitIO((struct IORequest *)inflight);
            if (inflight->ahir_Std.io_Error) {
                inflight = NULL;
                al_device_disconnect(d);
                ok = 0;
                break;
            }
        }
        inflight = sent;
        sent = NULL;
        cur ^= 1;
    }

    /* What is still with AHI: aborted, and taken back. */
    for (i = 0; i < 2; i++) {
        struct AHIRequest *r = i ? inflight : sent;
        if (r) {
            if (!CheckIO((struct IORequest *)r)) {
                AbortIO((struct IORequest *)r);
            }
            WaitIO((struct IORequest *)r);
        }
    }
    if (open) {
        CloseDevice((struct IORequest *)req[0]);
    }
    if (req[1]) {
        FreeVec(req[1]);
    }
    if (req[0]) {
        DeleteIORequest((struct IORequest *)req[0]);
    }
    if (buf[0]) {
        FreeVec(buf[0]);
    }
    if (buf[1]) {
        FreeVec(buf[1]);
    }
    if (port) {
        DeleteMsgPort(port);
    }
    /* Sound gone or never there: wait to be told to end. */
    while (!o->quit) {
        Wait(SIGBREAKF_CTRL_C);
    }
    /* Forbid: nothing runs until this process has ended, so the module's
     * code may go as soon as the one waiting is woken. */
    Forbid();
    Signal(o->waiter, 1UL << o->waiter_sig);
}

static OGPU_MODULE_THREAD_ENTRY(al_ahi_entry, al_ahi_main)

ALCboolean al_out_start(ALCdevice *d)
{
    struct al_ahi *o = d->out;
    struct Process *proc;
    struct TagItem tags[10];
    BYTE sig;
    int nt = 0;

    sig = AllocSignal(-1);
    if (sig < 0) {
        return ALC_FALSE;
    }
    o->quit = 0;
    o->waiter = FindTask(NULL);
    o->waiter_sig = sig;
    SetSignal(0, 1UL << sig);
    tags[nt].ti_Tag = NP_Entry;       tags[nt++].ti_Data = (ULONG)al_ahi_entry;
    tags[nt].ti_Tag = NP_Name;        tags[nt++].ti_Data = (ULONG) "OpenAL Mix";
    tags[nt].ti_Tag = NP_StackSize;   tags[nt++].ti_Data = AL_AHI_STACK;
    tags[nt].ti_Tag = NP_Priority;    tags[nt++].ti_Data = AL_AHI_PRIORITY;
    tags[nt].ti_Tag = NP_Input;       tags[nt++].ti_Data = 0;
    tags[nt].ti_Tag = NP_Output;      tags[nt++].ti_Data = 0;
    tags[nt].ti_Tag = NP_CloseInput;  tags[nt++].ti_Data = FALSE;
    tags[nt].ti_Tag = NP_CloseOutput; tags[nt++].ti_Data = FALSE;
    tags[nt].ti_Tag = NP_WindowPtr;   tags[nt++].ti_Data = (ULONG)-1L;  /* no requesters */
    tags[nt].ti_Tag = TAG_DONE;       tags[nt].ti_Data = 0;
    /* Forbid: the process mustn't run before it has its data and its A4. */
    Forbid();
    proc = CreateNewProc(tags);
    if (proc) {
        proc->pr_Task.tc_UserData = o;
        ogpu_module_thread_a4(&proc->pr_Task);
        o->thread = &proc->pr_Task;
    }
    Permit();
    if (!proc) {
        FreeSignal(sig);
        return ALC_FALSE;
    }
    Wait(1UL << sig);
    FreeSignal(sig);
    return ALC_TRUE;
}

void al_out_stop(ALCdevice *d)
{
    struct al_ahi *o = d->out;
    BYTE sig;
    if (!o || !o->thread) {
        return;
    }
    /* This task may not be the one that started it: its own signal. */
    sig = AllocSignal(-1);
    if (sig < 0) {
        sig = SIGBREAKB_CTRL_F;
    }
    SetSignal(0, 1UL << sig);
    Forbid();
    o->waiter = FindTask(NULL);
    o->waiter_sig = sig;
    o->quit = 1;
    Signal(o->thread, SIGBREAKF_CTRL_C);
    Permit();
    Wait(1UL << sig);
    if (sig != SIGBREAKB_CTRL_F) {
        FreeSignal(sig);
    }
    o->thread = NULL;
}
