/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 *
 * Runs one of SDL's own test programs, unchanged, for a set time and keeps
 * what it logs. The test is compiled with -Dmain=sdltest_main and linked
 * with this file:
 *   - SDL_Log goes to standard output (an AmigaDOS redirection catches it),
 *     not to the console window libnix opens for standard error;
 *   - with SDLTEST_SECONDS=N in the environment, SDL_QUIT arrives after N
 *     seconds, so the test ends by itself and prints its frame rate. */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>

extern int sdltest_main(int argc, char *argv[]);

static void SDLCALL log_to_stdout(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;
    (void)priority;
    fputs(message, stdout);
    if (*message && message[SDL_strlen(message) - 1] != '\n') {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

static Uint32 SDLCALL time_up(Uint32 interval, void *param)
{
    SDL_Event e;
    (void)interval;
    (void)param;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *secs = SDL_getenv("SDLTEST_SECONDS"); /* reads SetEnv and ENV: */
    int rc;

    SDL_LogSetOutputFunction(log_to_stdout, NULL);
    if (secs && atoi(secs) > 0) {
        SDL_InitSubSystem(SDL_INIT_TIMER);
        SDL_AddTimer((Uint32)atoi(secs) * 1000, time_up, NULL);
    }
    rc = sdltest_main(argc, argv);
    if (secs && atoi(secs) > 0) {
        SDL_QuitSubSystem(SDL_INIT_TIMER);
    }
    return rc;
}
