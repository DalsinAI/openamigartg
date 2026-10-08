/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * rendercompare: draws the same scenes with SDL's software renderer and with
 * the opengpu renderer, into a target texture each, reads both back and
 * compares them. One line per scene: how many pixels differ by more than a
 * small tolerance (filtering and rounding differ a little between the two),
 * and the largest difference. Run on an Amiga with SDL2.module, or linked
 * statically. Exit 0 when every scene matches. */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>

#define W 256
#define H 192
#define TOLERANCE 24       /* per channel */
#define EDGE_SHARE 2.0     /* percent of pixels allowed past it (edges of rotated pictures) */

static SDL_Texture *make_sprite(SDL_Renderer *r)
{
    /* 64x48: a colour gradient whose alpha falls off towards the edges */
    Uint32 px[64 * 48];
    int x, y;
    SDL_Texture *t;
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 64; x++) {
            int dx = x < 32 ? x : 63 - x, dy = y < 24 ? y : 47 - y;
            int a = (dx < dy ? dx : dy) * 16;
            if (a > 255) {
                a = 255;
            }
            px[y * 64 + x] = ((Uint32)a << 24) | ((Uint32)(x * 4) << 16) | ((Uint32)(y * 5) << 8) | (Uint32)((x + y) * 2);
        }
    }
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 64, 48);
    SDL_UpdateTexture(t, NULL, px, 64 * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static SDL_Texture *make_yuv(SDL_Renderer *r)
{
    /* 64x48 I420: stripes of luma, a chroma ramp */
    Uint8 planes[64 * 48 * 3 / 2];
    int x, y;
    SDL_Texture *t;
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 64; x++) {
            planes[y * 64 + x] = (Uint8)(16 + ((x / 8) * 27) % 220);
        }
    }
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 32; x++) {
            planes[64 * 48 + y * 32 + x] = (Uint8)(64 + x * 4);
            planes[64 * 48 + 32 * 24 + y * 32 + x] = (Uint8)(64 + y * 6);
        }
    }
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STATIC, 64, 48);
    if (t) {
        SDL_UpdateTexture(t, NULL, planes, 64);
    }
    return t;
}

static const char *const scenes[] = {
    "clear and opaque fills",
    "blended fills (BLEND, ADD, MOD, MUL)",
    "lines and points, blended",
    "copies: alpha, scaled, ADD",
    "copies with colour modulation",
    "rotated and flipped copies (RenderCopyEx)",
    "geometry (RenderGeometry)",
    "YUV texture (I420)",
};
#define NSCENES ((int)(sizeof(scenes) / sizeof(scenes[0])))

static void draw(SDL_Renderer *r, int scene, SDL_Texture *sprite, SDL_Texture *yuv)
{
    SDL_Rect rc;
    SDL_FRect fr;
    int i;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 40, 60, 90, 255);
    SDL_RenderClear(r);
    switch (scene) {
    case 0:
        for (i = 0; i < 8; i++) {
            rc.x = i * 30;
            rc.y = i * 20;
            rc.w = 50;
            rc.h = 40;
            SDL_SetRenderDrawColor(r, (Uint8)(i * 30), (Uint8)(255 - i * 30), 128, 255);
            SDL_RenderFillRect(r, &rc);
        }
        break;
    case 1: {
        const SDL_BlendMode modes[] = { SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD, SDL_BLENDMODE_MUL };
        for (i = 0; i < 4; i++) {
            SDL_SetRenderDrawColor(r, 200, 100, 50, 255);
            rc.x = 10 + i * 60;
            rc.y = 10;
            rc.w = 50;
            rc.h = 170;
            SDL_RenderFillRect(r, &rc);
            SDL_SetRenderDrawBlendMode(r, modes[i]);
            SDL_SetRenderDrawColor(r, 60, 180, 220, 128);
            rc.x = 0;
            rc.y = 40 + i * 35;
            rc.w = W;
            rc.h = 25;
            SDL_RenderFillRect(r, &rc);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
        break;
    }
    case 2:
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (i = 0; i < 16; i++) {
            SDL_SetRenderDrawColor(r, 255, (Uint8)(i * 16), 0, (Uint8)(80 + i * 10));
            SDL_RenderDrawLine(r, 0, i * 12, W - 1, H - 1 - i * 12);
            SDL_RenderDrawPoint(r, 10 + i * 15, 100);
        }
        break;
    case 3:
        for (i = 0; i < 4; i++) {
            fr.x = 10.0f + i * 60;
            fr.y = 20.0f;
            fr.w = 48.0f + i * 8;
            fr.h = 36.0f + i * 6;
            SDL_SetTextureAlphaMod(sprite, (Uint8)(255 - i * 50));
            SDL_SetTextureBlendMode(sprite, i == 3 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
            SDL_RenderCopyF(r, sprite, NULL, &fr);
        }
        SDL_SetTextureAlphaMod(sprite, 255);
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
        break;
    case 4:
        for (i = 0; i < 4; i++) {
            rc.x = 10 + i * 60;
            rc.y = 60;
            rc.w = 64;
            rc.h = 48;
            SDL_SetTextureColorMod(sprite, (Uint8)(255 - i * 60), (Uint8)(100 + i * 50), 200);
            SDL_RenderCopy(r, sprite, NULL, &rc);
        }
        SDL_SetTextureColorMod(sprite, 255, 255, 255);
        break;
    case 5:
        for (i = 0; i < 4; i++) {
            SDL_Point centre = { 32, 24 };
            rc.x = 20 + i * 55;
            rc.y = 60;
            rc.w = 64;
            rc.h = 48;
            SDL_RenderCopyEx(r, sprite, NULL, &rc, 30.0 * i + 15.0, &centre,
                             (SDL_RendererFlip)(i & 3));
        }
        break;
    case 6: {
        SDL_Vertex v[6];
        for (i = 0; i < 6; i++) {
            v[i].color.r = (Uint8)(i * 40);
            v[i].color.g = (Uint8)(255 - i * 40);
            v[i].color.b = 128;
            v[i].color.a = 255;
        }
        v[0].position.x = 20; v[0].position.y = 20; v[0].tex_coord.x = 0; v[0].tex_coord.y = 0;
        v[1].position.x = 200; v[1].position.y = 30; v[1].tex_coord.x = 1; v[1].tex_coord.y = 0;
        v[2].position.x = 60; v[2].position.y = 170; v[2].tex_coord.x = 0; v[2].tex_coord.y = 1;
        v[3].position.x = 120; v[3].position.y = 100; v[3].tex_coord.x = 0; v[3].tex_coord.y = 0;
        v[4].position.x = 250; v[4].position.y = 120; v[4].tex_coord.x = 1; v[4].tex_coord.y = 0;
        v[5].position.x = 180; v[5].position.y = 190; v[5].tex_coord.x = 1; v[5].tex_coord.y = 1;
        SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
        SDL_RenderGeometry(r, sprite, v + 3, 3, NULL, 0);
        break;
    }
    case 7:
        if (yuv) {
            rc.x = 20;
            rc.y = 20;
            rc.w = 128;
            rc.h = 96;
            SDL_RenderCopy(r, yuv, NULL, &rc);
            rc.x = 160;
            rc.w = 64;
            rc.h = 48;
            SDL_RenderCopy(r, yuv, NULL, &rc);
        }
        break;
    }
}

static Uint32 *render_all(const char *driver, char *name, size_t namelen)
{
    SDL_Window *win;
    SDL_Renderer *r;
    SDL_Texture *target, *sprite, *yuv;
    SDL_RendererInfo info;
    Uint32 *out = (Uint32 *)SDL_calloc(NSCENES, W * H * 4);
    int s;

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver);
    win = SDL_CreateWindow(driver, 20, 20, W, H, 0);
    r = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!r || !out) {
        printf("%s: no renderer (%s)\n", driver, SDL_GetError());
        return NULL;
    }
    SDL_GetRendererInfo(r, &info);
    SDL_strlcpy(name, info.name, namelen);
    sprite = make_sprite(r);
    yuv = make_yuv(r);
    target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    for (s = 0; s < NSCENES; s++) {
        SDL_SetRenderTarget(r, target);
        draw(r, s, sprite, yuv);
        SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, out + (size_t)s * W * H, W * 4);
        SDL_SetRenderTarget(r, NULL);
        SDL_RenderCopy(r, target, NULL, NULL);
        SDL_RenderPresent(r);
    }
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(sprite);
    if (yuv) {
        SDL_DestroyTexture(yuv);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    return out;
}

int main(int argc, char *argv[])
{
    char a_name[32] = "", b_name[32] = "";
    Uint32 *a, *b;
    int s, failed = 0;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    a = render_all("software", a_name, sizeof(a_name));
    b = render_all("opengpu", b_name, sizeof(b_name));
    if (!a || !b) {
        SDL_Quit();
        return 20;
    }
    printf("rendercompare: %s against %s, %dx%d, tolerance %d a channel\n", b_name, a_name, W, H, TOLERANCE);
    for (s = 0; s < NSCENES; s++) {
        const Uint32 *pa = a + (size_t)s * W * H, *pb = b + (size_t)s * W * H;
        long off = 0;
        int worst = 0, i;
        double share;
        for (i = 0; i < W * H; i++) {
            int c, d = 0;
            for (c = 0; c < 24; c += 8) {
                int e = (int)((pa[i] >> c) & 255) - (int)((pb[i] >> c) & 255);
                if (e < 0) {
                    e = -e;
                }
                if (e > d) {
                    d = e;
                }
            }
            if (d > worst) {
                worst = d;
            }
            if (d > TOLERANCE) {
                off++;
            }
        }
        share = 100.0 * off / (W * H);
        printf("%-44s %6ld pixels differ (%5.2f%%), largest %3d  %s\n", scenes[s], off, share, worst,
               share <= EDGE_SHARE ? "same" : "DIFFERENT");
        if (share > EDGE_SHARE) {
            failed = 1;
        }
    }
    SDL_free(a);
    SDL_free(b);
    SDL_Quit();
    return failed ? 5 : 0;
}
