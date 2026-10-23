/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 *
 * Runs one of SDL 3's own test programs, unchanged, for a set time and keeps
 * what it logs, as tests/sdl2/sdltest_run.c does for SDL 2. The test is
 * built with its main renamed sdltest_main (for a test on SDL's main callbacks,
 * that is the main SDL_main.h writes) and linked with this file:
 *   - SDL_Log goes to standard output (an AmigaDOS redirection catches it),
 *     not to the console window libnix opens for standard error;
 *   - with SDLTEST_SECONDS=N in the environment, SDL_EVENT_QUIT arrives
 *     after N seconds, so the test ends by itself and prints its frame rate. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/dos.h>

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

static Uint32 SDLCALL time_up(void *userdata, SDL_TimerID timerID, Uint32 interval)
{
    SDL_Event e;
    (void)userdata;
    (void)timerID;
    (void)interval;
    SDL_zero(e);
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
    return 0;
}

int main(int argc, char *argv[])
{
    char secs[16];
    int n = 0;

    /* SetEnv's local variable, else ENV:'s. */
    if (GetVar((CONST_STRPTR) "SDLTEST_SECONDS", (STRPTR)secs, sizeof(secs), 0) > 0) {
        n = atoi(secs);
    }
    SDL_SetLogOutputFunction(log_to_stdout, NULL);
    if (n > 0) {
        printf("sdltest_run: SDL_EVENT_QUIT in %d seconds (timer %u)\n", n,
               (unsigned)SDL_AddTimer((Uint32)n * 1000, time_up, NULL));
        fflush(stdout);
    }
    return sdltest_main(argc, argv);
}
