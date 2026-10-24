/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDLTone: an SDL 2 program's sound, for OpenAL's lab (Design-OpenAL
 * section 6: OpenAL and an SDL program's sound at once, through the broker
 * in acaudio.audio). SDL's AHI driver plays through ahi.device's unit 0,
 * whose AHI_AllocAudio is a client of the board beside OpenAL's Voices.
 *
 *   SDLTone [seconds] [hz]      a tone on the right channel only (the left
 *                               silent), 3 seconds of 660 Hz unless asked
 *
 * It prints the audio driver and what SDL opened, and returns 0 when the
 * sound opened and played, 5 when not. Built with SDL 2's libSDL2.a
 * (library/modules/openal/Makefile, lab-sdl). */
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double phase, step;
static volatile long played;

static void SDLCALL fill(void *user, Uint8 *stream, int len)
{
    Sint16 *s = (Sint16 *)stream;
    int i, n = len / 4;
    (void)user;
    for (i = 0; i < n; i++) {
        s[2 * i] = 0;
        s[2 * i + 1] = (Sint16)(8000.0 * sin(phase));
        phase += step;
        if (phase > 6.283185307179586) {
            phase -= 6.283185307179586;
        }
    }
    played += n;
}

int main(int argc, char **argv)
{
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    int seconds = argc > 1 ? atoi(argv[1]) : 3;
    double hz = argc > 2 ? atof(argv[2]) : 660.0;
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("SDLTone: SDL_Init: %s\n", SDL_GetError());
        return 5;
    }
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = fill;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        printf("SDLTone: no audio (%s): %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "no driver",
               SDL_GetError());
        SDL_Quit();
        return 5;
    }
    step = 2.0 * 3.141592653589793 * hz / have.freq;
    printf("SDLTone: %s, %d Hz, %d channels, %d frames a buffer; %.0f Hz on the right for %d seconds\n",
           SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples, hz, seconds);
    SDL_PauseAudioDevice(dev, 0);
    SDL_Delay((Uint32)seconds * 1000);
    SDL_PauseAudioDevice(dev, 1);
    printf("SDLTone: %ld frames played\n", (long)played);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return played > 0 ? 0 : 5;
}
