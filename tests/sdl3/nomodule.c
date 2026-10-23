/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * nomodule.m: a program on libSDL3.a ends when SDL3.module isn't there.
 * It calls SDL_Quit at its end as programs do, from atexit() and from a
 * destructor. With LIBS:OpenGPU/SDL3.module renamed away, run it from a
 * Shell: one line, "SDL 3: this program needs LIBS:OpenGPU/SDL3.module
 * (OpenGPU), which isn't installed.", then the prompt, with return code 20.
 * (It used to print that line for ever: the destructor's SDL_Quit tried the
 * module again and called exit() again, inside libnix's exit list.) With
 * the module in place it prints "nomodule: SDL_Init ok" and returns 0.
 * stubs/sdl3's host test is tests/sdl3/stub_nomodule.c. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static void __attribute__((destructor)) quit_at_end(void)
{
    SDL_Quit();
}

int main(void)
{
    atexit(SDL_Quit);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("nomodule: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("nomodule: SDL_Init ok\n");
    return 0;
}
