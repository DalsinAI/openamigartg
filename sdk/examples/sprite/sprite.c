/* Copyright (c) 2026 Dalsin Limited. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * Sprite: SDL 2's renderer on OpenGPU. Bouncing balls drawn from one
 * texture with alpha; OpenGPU composites them (SDL's opengpu renderer), or
 * the CPU does when OpenGPU can't. The title shows the frame rate. Esc or
 * the close gadget quits.
 *   Sprite [COUNT n] [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 sprite.c -o Sprite $(sdl2-config --cflags --libs)
 */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define SIZE 32
#define MAX 1000

struct ball { float x, y, dx, dy; };

/* A shaded ball with soft edges, made here so the program needs no files. */
static SDL_Texture *make_ball(SDL_Renderer *r)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SIZE, SIZE, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Texture *t;
    int x, y;
    if (!s)
        return NULL;
    for (y = 0; y < SIZE; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (x = 0; x < SIZE; x++) {
            float dx = x - SIZE / 2 + 0.5f, dy = y - SIZE / 2 + 0.5f;
            float d = SDL_sqrtf(dx * dx + dy * dy) / (SIZE / 2);
            float light = 1.0f - SDL_sqrtf((dx + 6) * (dx + 6) + (dy + 6) * (dy + 6)) / SIZE;
            int a = d >= 1.0f ? 0 : d > 0.85f ? (int)((1.0f - d) / 0.15f * 255) : 255;
            int c = (int)(80 + 175 * (light < 0 ? 0 : light));
            row[x] = ((Uint32)a << 24) | ((Uint32)c << 16) | ((Uint32)(c / 2) << 8) | (Uint32)(255 - c / 2);
        }
    }
    t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (t)
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

int main(int argc, char *argv[])
{
    static struct ball balls[MAX];
    int count = 100, seconds = 0, quit = 0, i, frames = 0, all = 0;
    Uint32 start, mark;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *ball;
    SDL_RendererInfo info;
    char title[96];

    for (i = 1; i < argc; i++) {
        if (!SDL_strcasecmp(argv[i], "COUNT") && i + 1 < argc)
            count = atoi(argv[++i]);
        else if (!SDL_strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    }
    if (count < 1 || count > MAX)
        count = 100;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Sprite: %s\n", SDL_GetError());
        return 20;
    }
    win = SDL_CreateWindow("Sprite", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    ball = ren ? make_ball(ren) : NULL;
    if (!ball) {
        printf("Sprite: %s\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }
    SDL_GetRendererInfo(ren, &info);

    for (i = 0; i < count; i++) {
        balls[i].x = (float)(rand() % (W - SIZE));
        balls[i].y = (float)(rand() % (H - SIZE));
        balls[i].dx = (rand() % 7 - 3) + 0.5f;
        balls[i].dy = (rand() % 7 - 3) + 0.5f;
    }

    start = mark = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        Uint32 now;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = 1;
        }
        SDL_SetRenderDrawColor(ren, 16, 24, 48, 255);
        SDL_RenderClear(ren);
        for (i = 0; i < count; i++) {
            struct ball *b = &balls[i];
            SDL_Rect dst;
            b->x += b->dx;
            b->y += b->dy;
            if (b->x < 0 || b->x > W - SIZE) b->dx = -b->dx;
            if (b->y < 0 || b->y > H - SIZE) b->dy = -b->dy;
            dst.x = (int)b->x;
            dst.y = (int)b->y;
            dst.w = dst.h = SIZE;
            SDL_RenderCopy(ren, ball, NULL, &dst);
        }
        SDL_RenderPresent(ren);
        frames++;
        all++;
        now = SDL_GetTicks();
        if (now - mark >= 1000) {
            SDL_snprintf(title, sizeof title, "Sprite  %d balls  %.1f fps (%s)", count, frames * 1000.0 / (now - mark), info.name);
            SDL_SetWindowTitle(win, title);
            frames = 0;
            mark = now;
        }
        if (seconds && now - start >= (Uint32)seconds * 1000)
            quit = 1;
    }
    printf("Sprite: %d balls, %d frames in %.1f s, %.1f fps, renderer %s\n", count, all,
           (SDL_GetTicks() - start) / 1000.0, all * 1000.0 / (SDL_GetTicks() - start + 1), info.name);
    SDL_DestroyTexture(ball);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
