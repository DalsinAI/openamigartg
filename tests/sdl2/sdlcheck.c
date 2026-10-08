/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * sdlcheck: SDL 2's Amiga back ends, one line each: timers and threads,
 * audio (which driver, and whether its callback keeps time), the clipboard,
 * joysticks, environment variables and paths. Run on an Amiga with
 * SDL2.module, or linked statically. Exit 0 when every check passes. */
#include "SDL.h"
#include <stdio.h>

static int failures = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("%-10s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

static SDL_atomic_t callbacks;
static SDL_atomic_t frames_played;

static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    /* a quiet 440 Hz tone, 16-bit stereo */
    static Uint32 phase = 0;
    Sint16 *s = (Sint16 *)stream;
    int i, n = len / 4;
    for (i = 0; i < n; i++, phase++) {
        Sint16 v = (Sint16)(((phase * 440 * 2 / 22050) & 1) ? 800 : -800);
        s[2 * i] = s[2 * i + 1] = v;
    }
    SDL_AtomicAdd(&frames_played, n);
    SDL_AtomicAdd(&callbacks, 1);
}

static int SDLCALL thread_fn(void *data)
{
    SDL_Delay(50);
    return 7;
}

int main(int argc, char *argv[])
{
    char line[256];
    Uint32 t0, took;
    int rc, i;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }

    /* Timers and threads */
    t0 = SDL_GetTicks();
    for (i = 0; i < 10; i++) {
        SDL_Delay(10);
    }
    took = SDL_GetTicks() - t0;
    SDL_snprintf(line, sizeof(line), "SDL_Delay(10) x10 took %u ms (dos.library's Delay would take 200)", (unsigned)took);
    report("timer", took >= 100 && took < 160, line);
    {
        SDL_Thread *t = SDL_CreateThread(thread_fn, "check", NULL);
        SDL_WaitThread(t, &rc);
        SDL_snprintf(line, sizeof(line), "a thread ran and returned %d", rc);
        report("thread", rc == 7, line);
    }

    /* Audio: the driver, and a second of callbacks */
    {
        SDL_AudioSpec want, have;
        SDL_AudioDeviceID dev;
        SDL_zero(want);
        want.freq = 22050;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_cb;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!dev) {
            SDL_snprintf(line, sizeof(line), "no audio device: %s", SDL_GetError());
            report("audio", 0, line);
        } else {
            SDL_PauseAudioDevice(dev, 0);
            SDL_Delay(1000);
            SDL_PauseAudioDevice(dev, 1);
            SDL_snprintf(line, sizeof(line), "driver %s, %d Hz, %d callbacks, %d frames in 1 s (%d expected)",
                         SDL_GetCurrentAudioDriver(), have.freq, SDL_AtomicGet(&callbacks),
                         SDL_AtomicGet(&frames_played), have.freq);
            /* the first two buffers go at once (double buffering), then one per buffer played */
            report("audio", SDL_AtomicGet(&frames_played) >= have.freq * 8 / 10 &&
                            SDL_AtomicGet(&frames_played) <= have.freq * 13 / 10 + 2 * have.samples, line);
            SDL_CloseAudioDevice(dev);
        }
    }

    /* The clipboard, with a Latin-1 character */
    {
        const char *text = "SDL 2 on AmigaOS 3.2: caf\xc3\xa9";
        char *back;
        SDL_SetClipboardText(text);
        back = SDL_GetClipboardText();
        SDL_snprintf(line, sizeof(line), "wrote and read back \"%s\"", back ? back : "(nothing)");
        report("clipboard", back && SDL_strcmp(back, text) == 0, line);
        SDL_free(back);
    }

    /* Joysticks (lowlevel.library) */
    SDL_snprintf(line, sizeof(line), "%d joystick(s): %s", SDL_NumJoysticks(),
                 SDL_NumJoysticks() > 0 ? SDL_JoystickNameForIndex(0) : "none plugged in");
    report("joystick", SDL_NumJoysticks() >= 0, line);

    /* Environment variables: SetEnv's, and SDL_setenv's */
    {
        const char *wb = SDL_getenv("Workbench");
        SDL_setenv("SDLCHECK", "yes", 1);
        SDL_snprintf(line, sizeof(line), "Workbench=%s, SDLCHECK=%s", wb ? wb : "(unset)",
                     SDL_getenv("SDLCHECK") ? SDL_getenv("SDLCHECK") : "(unset)");
        report("env", wb != NULL && SDL_getenv("SDLCHECK") && SDL_strcmp(SDL_getenv("SDLCHECK"), "yes") == 0, line);
    }

    /* Paths */
    {
        char *base = SDL_GetBasePath();
        char *pref = SDL_GetPrefPath("Dalsin", "sdlcheck");
        SDL_snprintf(line, sizeof(line), "base %s, prefs %s", base ? base : "(none)", pref ? pref : "(none)");
        report("paths", base != NULL, line);
        SDL_free(base);
        SDL_free(pref);
    }

    printf("sdlcheck: %s\n", failures ? "some checks failed" : "all checks passed");
    SDL_Quit();
    return failures ? 5 : 0;
}
