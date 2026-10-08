/* Copyright (c) 2026 Dalsin Limited. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * Bounce: an SDL 1.2 program (SDL_SetVideoMode, SDL_FillRect, SDL_Flip).
 * On OpenGPU, SDL 1.2 is sdl12-compat on OpenGPU's SDL 2 (SDL2.module), so
 * SDL 1.2 programs draw through the same engine as SDL 2 ones. Squares
 * bounce in a 320x240 window; the title shows the frame rate. Esc or the
 * close gadget quits.
 *   Bounce [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 bounce.c -o Bounce $(sdl-config --cflags --libs)
 */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240
#define N 12

int main(int argc, char **argv)
{
    struct { int x, y, dx, dy; Uint32 colour; } sq[N];
    SDL_Surface *screen;
    Uint32 start, last;
    int i, seconds = 0, quit = 0, frames = 0;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Bounce: SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    screen = SDL_SetVideoMode(W, H, 0, SDL_SWSURFACE);
    if (!screen) {
        printf("Bounce: SDL_SetVideoMode: %s\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }
    SDL_WM_SetCaption("Bounce", "Bounce");
    srand(1);
    for (i = 0; i < N; i++) {
        sq[i].x = rand() % (W - 16);
        sq[i].y = rand() % (H - 16);
        sq[i].dx = 1 + rand() % 3;
        sq[i].dy = 1 + rand() % 3;
        sq[i].colour = SDL_MapRGB(screen->format, 64 + rand() % 192, 64 + rand() % 192, 64 + rand() % 192);
    }
    start = last = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 16, 16, 48));
        for (i = 0; i < N; i++) {
            SDL_Rect r;
            sq[i].x += sq[i].dx;
            sq[i].y += sq[i].dy;
            if (sq[i].x < 0 || sq[i].x > W - 16) sq[i].dx = -sq[i].dx, sq[i].x += 2 * sq[i].dx;
            if (sq[i].y < 0 || sq[i].y > H - 16) sq[i].dy = -sq[i].dy, sq[i].y += 2 * sq[i].dy;
            r.x = (Sint16)sq[i].x;
            r.y = (Sint16)sq[i].y;
            r.w = r.h = 16;
            SDL_FillRect(screen, &r, sq[i].colour);
        }
        SDL_Flip(screen);
        frames++;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = 1;
        if (SDL_GetTicks() - last >= 1000) {
            char title[64];
            sprintf(title, "Bounce: %.1f fps", frames * 1000.0 / (SDL_GetTicks() - start));
            SDL_WM_SetCaption(title, "Bounce");
            last = SDL_GetTicks();
        }
        if (seconds && SDL_GetTicks() - start >= (Uint32)seconds * 1000)
            quit = 1;
    }
    printf("Bounce: %d frames in %.1f s\n", frames, (SDL_GetTicks() - start) / 1000.0);
    SDL_Quit();
    return 0;
}
