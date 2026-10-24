/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Voices' output on the Amiga: the board's voices through AHI's low-level
 * calls, on ACAHI's Host mix mode (audio ID 0x00AC0001, acaudio.audio's
 * driver: acaudio.doc):
 *   - alcOpenDevice opens ahi.device's AHI_NO_UNIT for AHI's library calls
 *     and takes n voices with AHI_AllocAudio, with AHI's four all-of-memory
 *     sounds (address 0, length 0xFFFFFFFF: AHI_SetSound's offset is where
 *     the frames are) for 8 and 16-bit, mono and stereo; a master volume of
 *     n / 2, which the driver scales by 2 / n, so a voice plays at the gain
 *     Voices gives it; and AHIET_CHANNELINFO, the voices' positions after
 *     each PlayerFunc;
 *   - SoundFunc (a voice's segment ended) is al_voices_event, PlayerFunc
 *     (50 a second) the clock, the channel info al_voices_info; each wakes
 *     the device's task ("OpenAL Voices", priority 5), which does
 *     al_voices_service. They run at interrupt time: no lock, no FPU;
 *   - the module's copy of the data is reached through A4: each hook's
 *     h_Data holds the A4 of the program's copy (module_rt.h) and its entry
 *     is al_vhook_stub, which sets A4 and calls h_SubEntry.
 * AHI_AllocAudio fails when the mode isn't there or the board hasn't n
 * voices free (acaudio.audio's broker shares the board among several
 * AHI_AllocAudio; an older driver serves one at a time); alcOpenDevice
 * then tries fewer, and the default device is Mix. */
#include "al_internal.h"

#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <utility/hooks.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <stdlib.h>
#include <string.h>

#ifdef AL_MODULE_BUILD
#include "module_rt.h"
#define AL_HOOK_A4() ((ULONG)ogpu_module_a4())
#else
#define ogpu_module_thread_a4(child) ((void)(child))
#define OGPU_MODULE_THREAD_ENTRY(name, fn) \
    void name(void) { fn(); }
#define AL_HOOK_A4() 0ul
#endif

#define AL_VMODE 0x00AC0001ul           /* AmigaChrome: Host mix 16 bit stereo++ */
#define AL_VTICK 50                     /* PlayerFunc, a second */
#define AL_VRATE 44100                  /* the board mixes at */
#define AL_VPRIORITY 5
#define AL_VSTACK 16384

struct Library *AHIBase;
static ULONG al_vahi_users;             /* devices with AHI open */

struct al_vahi {
    ALCdevice *device;
    struct MsgPort port;                /* PA_IGNORE: the opening needs no signal */
    struct AHIRequest *req;
    struct AHIAudioCtrl *ctrl;
    struct AHIEffChannelInfo *chinfo;
    struct Hook sound_hook, player_hook, info_hook;
    ALuint n;
    volatile ULONG ticks;
    struct Task *task;                  /* the task, from al_vout_start to al_vout_stop */
    struct Task *volatile service;      /* the task, while it takes signals */
    volatile BYTE service_sig;
    struct Task *waiter;
    BYTE waiter_sig;
    volatile BYTE quit;
    BOOL playing;
};

/* ---- The hooks' entry ----------------------------------------------------------------- */

/* al_vhook.S: A4 from h_Data, then h_SubEntry(hook, object, message). */
extern ULONG al_vhook_stub(void);

static void al_vhook(struct Hook *h, ULONG (*fn)(struct Hook *, APTR, APTR), APTR data)
{
    memset(h, 0, sizeof *h);
    h->h_Entry = (HOOKFUNC)al_vhook_stub;
    h->h_SubEntry = (HOOKFUNC)fn;
    h->h_Data = data;
}

/* SoundFunc: a voice's segment ended. */
static ULONG al_vsound(struct Hook *h, APTR ctrl, APTR msg)
{
    struct AHIAudioCtrl *a = ctrl;
    ALCdevice *d = a->ahiac_UserData;
    struct al_vahi *o = d->out;
    (void)h;
    al_voices_event(d, ((struct AHISoundMessage *)msg)->ahism_Channel);
    if (o->service) {
        Signal(o->service, 1UL << o->service_sig);
    }
    return 0;
}

/* PlayerFunc: the clock. */
static ULONG al_vplayer(struct Hook *h, APTR ctrl, APTR msg)
{
    struct AHIAudioCtrl *a = ctrl;
    ALCdevice *d = a->ahiac_UserData;
    struct al_vahi *o = d->out;
    (void)h;
    (void)msg;
    o->ticks++;
    if (o->service) {
        Signal(o->service, 1UL << o->service_sig);
    }
    return 0;
}

/* The channel info, after each PlayerFunc. */
static ULONG al_vinfo(struct Hook *h, APTR ctrl, APTR msg)
{
    struct AHIAudioCtrl *a = ctrl;
    ALCdevice *d = a->ahiac_UserData;
    struct AHIEffChannelInfo *ci = msg;
    UWORD k;
    (void)h;
    for (k = 0; k < ci->ahieci_Channels; k++) {
        al_voices_info(d, k, ci->ahieci_Offset[k]);
    }
    return 0;
}

/* ---- AHI ---------------------------------------------------------------------------- */

static void al_vport_init(struct MsgPort *p)
{
    memset(p, 0, sizeof *p);
    p->mp_Node.ln_Type = NT_MSGPORT;
    p->mp_Flags = PA_IGNORE;
    NewList(&p->mp_MsgList);
}

/* ahi.device's AHI_NO_UNIT, for AHI's library calls: AHIBase. */
static struct AHIRequest *al_vahi_open(struct MsgPort *port)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    struct AHIRequest *req = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest));
    LONG err;
    if (!req) {
        return NULL;
    }
    req->ahir_Version = 4;
    me->pr_WindowPtr = (APTR)-1L;
    err = OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)req, 0);
    me->pr_WindowPtr = oldwin;
    if (err) {
        DeleteIORequest((struct IORequest *)req);
        return NULL;
    }
    Forbid();
    AHIBase = (struct Library *)req->ahir_Std.io_Device;
    al_vahi_users++;
    Permit();
    return req;
}

static void al_vahi_close(struct AHIRequest *req)
{
    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    Forbid();
    if (--al_vahi_users == 0) {
        AHIBase = NULL;
    }
    Permit();
}

/* The Host mix mode is there, with acaudio.audio as its driver. */
static int al_vmode_ok(void)
{
    char name[32];
    struct TagItem tags[3];
    int i;
    name[0] = 0;
    tags[0].ti_Tag = AHIDB_Driver;    tags[0].ti_Data = (ULONG)name;
    tags[1].ti_Tag = AHIDB_BufferLen; tags[1].ti_Data = sizeof name;
    tags[2].ti_Tag = TAG_DONE;        tags[2].ti_Data = 0;
    if (!AHI_GetAudioAttrsA(AL_VMODE, NULL, tags)) {
        return 0;
    }
    name[sizeof name - 1] = 0;
    for (i = 0; name[i]; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            name[i] += 'a' - 'A';
        }
    }
    return !strcmp(name, "acaudio");
}

ALCboolean al_vout_available(void)
{
    struct MsgPort port;
    struct AHIRequest *req;
    int ok;
    al_vport_init(&port);
    req = al_vahi_open(&port);
    if (!req) {
        return ALC_FALSE;
    }
    ok = al_vmode_ok();
    al_vahi_close(req);
    return ok ? ALC_TRUE : ALC_FALSE;
}

static void al_vahi_free(ALCdevice *d)
{
    struct al_vahi *o = d->out;
    UWORD k;
    if (o->ctrl) {
        if (o->playing) {
            struct TagItem tags[2];
            tags[0].ti_Tag = AHIC_Play; tags[0].ti_Data = FALSE;
            tags[1].ti_Tag = TAG_DONE;  tags[1].ti_Data = 0;
            AHI_ControlAudioA(o->ctrl, tags);
        }
        if (o->chinfo) {
            ULONG cancel = AHIET_CHANNELINFO | AHIET_CANCEL;
            AHI_SetEffect(&cancel, o->ctrl);
        }
        for (k = 0; k < 4; k++) {
            AHI_UnloadSound(k, o->ctrl);
        }
        AHI_FreeAudio(o->ctrl);
    }
    if (o->chinfo) {
        FreeVec(o->chinfo);
    }
    if (o->req) {
        al_vahi_close(o->req);
    }
    free(o);
    d->out = NULL;
}

ALCboolean al_vout_open(ALCdevice *d, ALuint n)
{
    struct al_vahi *o = calloc(1, sizeof *o);
    struct TagItem tags[12];
    struct AHIEffMasterVolume mv;
    ULONG a4 = AL_HOOK_A4();
    UWORD k;
    int nt = 0;
    if (!o) {
        return ALC_FALSE;
    }
    o->device = d;
    o->n = n;
    o->service_sig = -1;
    d->out = o;
    al_vport_init(&o->port);
    o->req = al_vahi_open(&o->port);
    if (!o->req || !al_vmode_ok()) {
        al_vahi_free(d);
        return ALC_FALSE;
    }
    al_vhook(&o->sound_hook, al_vsound, (APTR)a4);
    al_vhook(&o->player_hook, al_vplayer, (APTR)a4);
    al_vhook(&o->info_hook, al_vinfo, (APTR)a4);
    tags[nt].ti_Tag = AHIA_AudioID;       tags[nt++].ti_Data = AL_VMODE;
    tags[nt].ti_Tag = AHIA_MixFreq;       tags[nt++].ti_Data = AL_VRATE;
    tags[nt].ti_Tag = AHIA_Channels;      tags[nt++].ti_Data = n;
    tags[nt].ti_Tag = AHIA_Sounds;        tags[nt++].ti_Data = 4;
    tags[nt].ti_Tag = AHIA_SoundFunc;     tags[nt++].ti_Data = (ULONG)&o->sound_hook;
    tags[nt].ti_Tag = AHIA_PlayerFunc;    tags[nt++].ti_Data = (ULONG)&o->player_hook;
    tags[nt].ti_Tag = AHIA_PlayerFreq;    tags[nt++].ti_Data = (ULONG)AL_VTICK << 16;
    tags[nt].ti_Tag = AHIA_MinPlayerFreq; tags[nt++].ti_Data = (ULONG)AL_VTICK << 16;
    tags[nt].ti_Tag = AHIA_MaxPlayerFreq; tags[nt++].ti_Data = (ULONG)AL_VTICK << 16;
    tags[nt].ti_Tag = AHIA_UserData;      tags[nt++].ti_Data = (ULONG)d;
    tags[nt].ti_Tag = TAG_DONE;           tags[nt].ti_Data = 0;
    o->ctrl = AHI_AllocAudioA(tags);
    if (!o->ctrl) {
        al_vahi_free(d);
        return ALC_FALSE;
    }
    /* AHI's all-of-memory sounds, one for each format: sound k is AHI's
     * sample type k (AHIST_M8S, M16S, S8S, S16S). */
    for (k = 0; k < 4; k++) {
        struct AHISampleInfo si;
        si.ahisi_Type = k;
        si.ahisi_Address = NULL;
        si.ahisi_Length = 0xFFFFFFFFul;
        if (AHI_LoadSound(k, AHIST_DYNAMICSAMPLE, &si, o->ctrl) != AHIE_OK) {
            al_vahi_free(d);
            return ALC_FALSE;
        }
    }
    /* n / 2: the driver's 2 / n then gives each voice its own gain. */
    mv.ahie_Effect = AHIET_MASTERVOLUME;
    mv.ahiemv_Volume = (Fixed)(n * 0x8000ul);
    AHI_SetEffect(&mv, o->ctrl);
    o->chinfo = AllocVec(sizeof *o->chinfo + n * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
    if (!o->chinfo) {
        al_vahi_free(d);
        return ALC_FALSE;
    }
    o->chinfo->ahie_Effect = AHIET_CHANNELINFO;
    o->chinfo->ahieci_Func = &o->info_hook;
    o->chinfo->ahieci_Channels = (UWORD)n;
    if (AHI_SetEffect(o->chinfo, o->ctrl) != AHIE_OK) {
        FreeVec(o->chinfo);
        o->chinfo = NULL;       /* no positions: offsets go by the segments' ends */
    }
    for (k = 0; k < n; k++) {
        AHI_SetFreq(k, 0, o->ctrl, AHISF_IMM);
        AHI_SetVol(k, 0, 0x8000, o->ctrl, AHISF_IMM);
        AHI_SetSound(k, AHI_NOSOUND, 0, 0, o->ctrl, AHISF_IMM);
    }
    tags[0].ti_Tag = AHIC_Play; tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = TAG_DONE;  tags[1].ti_Data = 0;
    if (AHI_ControlAudioA(o->ctrl, tags) != AHIE_OK) {
        al_vahi_free(d);
        return ALC_FALSE;
    }
    o->playing = TRUE;
    d->frequency = AL_VRATE;
    d->refresh = AL_VTICK;
    return ALC_TRUE;
}

void al_vout_close(ALCdevice *d)
{
    if (d->out) {
        al_vahi_free(d);
    }
}

/* ---- The task ---------------------------------------------------------------------- */

static void al_vservice_main(void)
{
    struct Task *me = FindTask(NULL);
    struct al_vahi *o = me->tc_UserData;
    ALCdevice *d = o->device;
    BYTE sig = AllocSignal(-1);
    o->service_sig = sig;
    if (sig >= 0) {
        o->service = me;
    }
    Signal(o->waiter, 1UL << o->waiter_sig);
    while (sig >= 0 && !o->quit) {
        ULONG got = Wait((1UL << sig) | SIGBREAKF_CTRL_C);
        if (o->quit) {
            break;
        }
        if (got & (1UL << sig)) {
            al_voices_service(d);
        }
    }
    Disable();
    o->service = NULL;
    Enable();
    if (sig >= 0) {
        FreeSignal(sig);
    }
    while (!o->quit) {
        Wait(SIGBREAKF_CTRL_C);
    }
    /* Forbid: nothing runs until this process has ended, so the module's
     * code may go as soon as the one waiting is woken. */
    Forbid();
    Signal(o->waiter, 1UL << o->waiter_sig);
}

static OGPU_MODULE_THREAD_ENTRY(al_vservice_entry, al_vservice_main)

ALCboolean al_vout_start(ALCdevice *d)
{
    struct al_vahi *o = d->out;
    struct Process *proc;
    struct TagItem tags[10];
    struct Task *t = NULL;
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
    tags[nt].ti_Tag = NP_Entry;       tags[nt++].ti_Data = (ULONG)al_vservice_entry;
    tags[nt].ti_Tag = NP_Name;        tags[nt++].ti_Data = (ULONG) "OpenAL Voices";
    tags[nt].ti_Tag = NP_StackSize;   tags[nt++].ti_Data = AL_VSTACK;
    tags[nt].ti_Tag = NP_Priority;    tags[nt++].ti_Data = AL_VPRIORITY;
    tags[nt].ti_Tag = NP_Input;       tags[nt++].ti_Data = 0;
    tags[nt].ti_Tag = NP_Output;      tags[nt++].ti_Data = 0;
    tags[nt].ti_Tag = NP_CloseInput;  tags[nt++].ti_Data = FALSE;
    tags[nt].ti_Tag = NP_CloseOutput; tags[nt++].ti_Data = FALSE;
    tags[nt].ti_Tag = NP_WindowPtr;   tags[nt++].ti_Data = (ULONG)-1L;
    tags[nt].ti_Tag = TAG_DONE;       tags[nt].ti_Data = 0;
    Forbid();
    proc = CreateNewProc(tags);
    if (proc) {
        proc->pr_Task.tc_UserData = o;
        ogpu_module_thread_a4(&proc->pr_Task);
        t = &proc->pr_Task;
    }
    Permit();
    if (!t) {
        FreeSignal(sig);
        return ALC_FALSE;
    }
    Wait(1UL << sig);
    FreeSignal(sig);
    o->task = t;
    return ALC_TRUE;
}

void al_vout_stop(ALCdevice *d)
{
    struct al_vahi *o = d->out;
    struct Task *t;
    BYTE sig;
    if (!o || !o->task) {
        return;
    }
    t = o->task;
    sig = AllocSignal(-1);
    if (sig < 0) {
        sig = SIGBREAKB_CTRL_F;
    }
    SetSignal(0, 1UL << sig);
    Forbid();
    o->waiter = FindTask(NULL);
    o->waiter_sig = sig;
    o->quit = 1;
    Signal(t, SIGBREAKF_CTRL_C);
    Permit();
    Wait(1UL << sig);
    if (sig != SIGBREAKB_CTRL_F) {
        FreeSignal(sig);
    }
    o->task = NULL;
}

/* ---- The voices ------------------------------------------------------------------- */

unsigned al_vout_sound(ALCdevice *d, ALint bits, ALint channels)
{
    (void)d;
    return (bits == 16 ? 1u : 0u) + (channels == 2 ? 2u : 0u);
}

ALuint al_vout_frame(ALCdevice *d, const void *data, ALuint frames, ALint bits, ALint channels)
{
    (void)d;
    (void)frames;
    return (ALuint)((ULONG)data / (ULONG)(bits / 8 * channels));
}

void al_vout_play(ALCdevice *d, ALuint k, unsigned sound, ALuint offset, ALuint frames, int imm)
{
    struct al_vahi *o = d->out;
    AHI_SetSound((UWORD)k, sound == AL_VOUT_NOSOUND ? AHI_NOSOUND : (UWORD)sound, offset, (LONG)frames, o->ctrl,
                 imm ? AHISF_IMM : AHISF_NONE);
}

void al_vout_freq(ALCdevice *d, ALuint k, ALuint freq)
{
    struct al_vahi *o = d->out;
    AHI_SetFreq((UWORD)k, freq, o->ctrl, AHISF_IMM);
}

void al_vout_vol(ALCdevice *d, ALuint k, ALint vol, ALint pan)
{
    struct al_vahi *o = d->out;
    AHI_SetVol((UWORD)k, vol, pan, o->ctrl, AHISF_IMM);
}

void al_vout_lock(ALCdevice *d)
{
    (void)d;
    Disable();
}

void al_vout_unlock(ALCdevice *d)
{
    (void)d;
    Enable();
}

ALuint al_vout_clock(ALCdevice *d)
{
    struct al_vahi *o = d->out;
    return (ALuint)o->ticks * (AL_VRATE / AL_VTICK);
}

void al_vout_wake(ALCdevice *d)
{
    struct al_vahi *o = d->out;
    if (o->service) {
        Signal(o->service, 1UL << o->service_sig);
    }
}
