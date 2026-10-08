/*
  SDL 2 audio for AmigaOS 3.x through AHI.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
*/

#ifndef SDL_os3audio_h_
#define SDL_os3audio_h_

#include "../../SDL_internal.h"
#include "../SDL_sysaudio.h"

#include <devices/ahi.h>

struct SDL_PrivateAudioData {
    struct MsgPort    *port;      /* the audio thread's */
    struct AHIRequest *req[2];
    struct AHIRequest *inflight;  /* playing, or queued to play next */
    struct AHIRequest *sent;      /* sent by the last PlayDevice */
    int                current;   /* the buffer being filled */
    int                device_open;
    Uint8             *mixbuf[2];
    ULONG              bufsize;
    ULONG              ahi_type;
    ULONG              ahi_freq;
};

#define _THIS SDL_AudioDevice *_this

extern AudioBootStrap OS3AHI_bootstrap;

#endif /* SDL_os3audio_h_ */
