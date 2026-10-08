/*
  SDL 2 audio for AmigaOS 3.x through AHI (ahi.device, unit AHI_DEFAULT_UNIT).
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
  Replaces libSDL2-amigaos3's AHI driver; its Paula driver stays as the
  fallback when ahi.device can't be opened.

  - 8 or 16-bit, mono or stereo, at the rate the program asks (AHI
    resamples to its mode). Other formats are converted by SDL.
  - The device, its message port and both requests are opened in SDL's
    audio thread (ThreadInit), not in the task that called SDL_OpenAudio:
    a message port signals only the task that made it, and the old driver
    waited on a port the main task owned, which hung when the first buffer
    hadn't finished at once.
  - Double buffered with ahir_Link, so AHI plays the buffers back to back
    with no gap. The audio thread fills one buffer while the other plays:
    the latency is two buffers (spec.samples frames each), which keeps the
    sound in step with the picture to within a buffer.
*/

#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_AHI

#include "SDL_audio.h"
#include "SDL_timer.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"
#include "SDL_os3audio.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <clib/alib_protos.h>

#define OS3AHI_DRIVER_NAME "ahi"

static void OS3AHI_DetectDevices(void)
{
}

static int OS3AHI_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    SDL_AudioFormat test_format;
    int found = 0;

    (void)devname;

    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (!hidden) {
        return SDL_OutOfMemory();
    }
    _this->hidden = hidden;

    if (_this->spec.channels > 2) {
        _this->spec.channels = 2;
    }
    if (_this->spec.channels < 1) {
        _this->spec.channels = 1;
    }

    for (test_format = SDL_FirstAudioFormat(_this->spec.format); test_format && !found;
         test_format = SDL_NextAudioFormat()) {
        switch (test_format) {
        case AUDIO_S16MSB:
            found = 1;
            break;
        case AUDIO_S8:
            found = 1;
            break;
        default:
            break;
        }
        if (found) {
            _this->spec.format = test_format;
        }
    }
    if (!found) {
        _this->spec.format = AUDIO_S16MSB; /* SDL converts to it */
    }

    if (_this->spec.format == AUDIO_S8) {
        hidden->ahi_type = (_this->spec.channels == 2) ? AHIST_S8S : AHIST_M8S;
    } else {
        hidden->ahi_type = (_this->spec.channels == 2) ? AHIST_S16S : AHIST_M16S;
    }

    SDL_CalculateAudioSpec(&_this->spec);
    hidden->ahi_freq = (ULONG)_this->spec.freq;
    hidden->bufsize = (ULONG)_this->spec.size;

    hidden->mixbuf[0] = (Uint8 *)AllocVec(hidden->bufsize, MEMF_PUBLIC | MEMF_CLEAR);
    hidden->mixbuf[1] = (Uint8 *)AllocVec(hidden->bufsize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!hidden->mixbuf[0] || !hidden->mixbuf[1]) {
        return SDL_OutOfMemory();
    }
    SDL_memset(hidden->mixbuf[0], _this->spec.silence, hidden->bufsize);
    SDL_memset(hidden->mixbuf[1], _this->spec.silence, hidden->bufsize);
    return 0;
}

/* In the audio thread: the port, the requests and the device. */
static void OS3AHI_ThreadInit(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;

    hidden->port = CreateMsgPort();
    if (!hidden->port) {
        return;
    }
    hidden->req[0] = (struct AHIRequest *)CreateIORequest(hidden->port, sizeof(struct AHIRequest));
    if (!hidden->req[0]) {
        return;
    }
    hidden->req[0]->ahir_Version = 4;
    me->pr_WindowPtr = (APTR)-1L;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)hidden->req[0], 0) != 0) {
        me->pr_WindowPtr = oldwin;
        DeleteIORequest((struct IORequest *)hidden->req[0]);
        hidden->req[0] = NULL;
        return;
    }
    me->pr_WindowPtr = oldwin;
    hidden->req[1] = (struct AHIRequest *)AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);
    if (!hidden->req[1]) {
        return;
    }
    SDL_memcpy(hidden->req[1], hidden->req[0], sizeof(struct AHIRequest));
    hidden->device_open = 1;
    hidden->current = 0;
    hidden->inflight = NULL;
}

static Uint8 *OS3AHI_GetDeviceBuf(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    return hidden->mixbuf[hidden->current];
}

static void OS3AHI_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    struct AHIRequest *req;

    if (!hidden->device_open) {
        return;
    }
    req = hidden->req[hidden->current];
    req->ahir_Std.io_Message.mn_Node.ln_Pri = 60;
    req->ahir_Std.io_Command = CMD_WRITE;
    req->ahir_Std.io_Data = hidden->mixbuf[hidden->current];
    req->ahir_Std.io_Length = hidden->bufsize;
    req->ahir_Std.io_Offset = 0;
    req->ahir_Frequency = hidden->ahi_freq;
    req->ahir_Type = hidden->ahi_type;
    req->ahir_Volume = 0x10000;
    req->ahir_Position = 0x8000;
    req->ahir_Link = hidden->inflight; /* starts as soon as that one ends */
    SendIO((struct IORequest *)req);
    hidden->sent = req;
}

/* Wait until the older buffer has played, so it can be filled again while
   the one just sent plays. */
static void OS3AHI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;

    if (!hidden->device_open) {
        SDL_Delay((_this->spec.samples * 1000) / _this->spec.freq);
        return;
    }
    if (hidden->inflight) {
        WaitIO((struct IORequest *)hidden->inflight);
    }
    hidden->inflight = hidden->sent;
    hidden->current ^= 1;
}

static void OS3AHI_ThreadDeinit(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    int i;

    if (hidden->device_open) {
        for (i = 0; i < 2; i++) {
            struct IORequest *io = (struct IORequest *)hidden->req[i];
            if (io && (io == (struct IORequest *)hidden->inflight || io == (struct IORequest *)hidden->sent)) {
                if (!CheckIO(io)) {
                    AbortIO(io);
                }
                WaitIO(io);
            }
        }
        hidden->inflight = hidden->sent = NULL;
        CloseDevice((struct IORequest *)hidden->req[0]);
        hidden->device_open = 0;
    }
    if (hidden->req[1]) {
        FreeVec(hidden->req[1]);
        hidden->req[1] = NULL;
    }
    if (hidden->req[0]) {
        DeleteIORequest((struct IORequest *)hidden->req[0]);
        hidden->req[0] = NULL;
    }
    if (hidden->port) {
        DeleteMsgPort(hidden->port);
        hidden->port = NULL;
    }
}

static void OS3AHI_CloseDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;

    if (!hidden) {
        return;
    }
    if (hidden->port) {
        /* The thread never got to ThreadDeinit (it didn't start). */
        OS3AHI_ThreadDeinit(_this);
    }
    if (hidden->mixbuf[0]) {
        FreeVec(hidden->mixbuf[0]);
    }
    if (hidden->mixbuf[1]) {
        FreeVec(hidden->mixbuf[1]);
    }
    SDL_free(hidden);
    _this->hidden = NULL;
}

/* Whether ahi.device opens at all; if not, SDL tries Paula next. */
static int OS3AHI_Available(void)
{
    struct MsgPort *p;
    struct AHIRequest *req;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    int ok = 0;

    p = CreateMsgPort();
    if (!p) {
        return 0;
    }
    req = (struct AHIRequest *)CreateIORequest(p, sizeof(struct AHIRequest));
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
    DeleteMsgPort(p);
    return ok;
}

static SDL_bool OS3AHI_Init(SDL_AudioDriverImpl *impl)
{
    if (!OS3AHI_Available()) {
        return SDL_FALSE;
    }
    impl->DetectDevices = OS3AHI_DetectDevices;
    impl->OpenDevice = OS3AHI_OpenDevice;
    impl->ThreadInit = OS3AHI_ThreadInit;
    impl->ThreadDeinit = OS3AHI_ThreadDeinit;
    impl->PlayDevice = OS3AHI_PlayDevice;
    impl->GetDeviceBuf = OS3AHI_GetDeviceBuf;
    impl->WaitDevice = OS3AHI_WaitDevice;
    impl->CloseDevice = OS3AHI_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->SupportsNonPow2Samples = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap OS3AHI_bootstrap = {
    OS3AHI_DRIVER_NAME, "AmigaOS AHI audio", OS3AHI_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_AHI */
