/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_mixer.module: SDL2_mixer 2.8.2 (Zlib) as a shared, resident module in
 * LIBS:OpenGPU/ (stubs/sdl2/sat_module.h). It mixes into SDL's audio device
 * (SDL2.module: AHI, else Paula). Ogg Vorbis, MP3, FLAC and tracker modules
 * are decoded by the x86 or ARM64 cores (media.decode/1, ogpu_mixer.c)
 * when a services card or a paired Cradle has the service, else by
 * SDL2_mixer's own decoders on this CPU. */
#include "SDL_mixer.h"
#include "sat_service.h"

static void sat_mixer_close_sdl(void)
{
    int freq, channels;
    Uint16 format;
    while (Mix_QuerySpec(&freq, &format, &channels)) {
        Mix_CloseAudio();
    }
    Mix_Quit();
}

#define SAT_PROCS "SDL_mixer_procs.h"
#define SAT_CLOSE_WITH_SDL sat_mixer_close_sdl();
#define SAT_CLOSE_ALWAYS sat_service_close_all();
#include "sat_module_body.h"
