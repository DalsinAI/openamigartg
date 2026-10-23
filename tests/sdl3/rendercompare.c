/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * rendercompare (SDL 3): draws the same scenes with SDL's software renderer
 * and with the opengpu renderer, into a target texture each, reads both back
 * and compares them. One line per scene: how many pixels differ by more than
 * a small tolerance (filtering and rounding differ a little between the two),
 * and the largest difference. Run on an Amiga with SDL3.module, or linked
 * statically; tests/sdl3/rendercompare_host.sh runs it on a Linux PC with
 * OpenGPU's own core as the library. Exit 0 when every scene matches.
 *
 * Where the two renderers are meant to differ, the scene says so and is
 * compared more loosely: SDL's software renderer draws SDL_SCALEMODE_PIXELART
 * as nearest, where OpenGPU (as SDL's GPU renderers) gives each texel's
 * border one smoothed pixel; and it samples textured triangles nearest
 * whatever the scale mode. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define W 256
#define H 192
#define TOLERANCE 24       /* per channel */
#define EDGE_SHARE 2.0     /* percent of pixels allowed past it (edges of rotated pictures) */

typedef struct Textures
{
    SDL_Texture *sprite;   /* ARGB8888 with alpha */
    SDL_Texture *premul;   /* the same, premultiplied */
    SDL_Texture *rgba;     /* the same in SDL_PIXELFORMAT_RGBA32 */
    SDL_Texture *abgr;     /* and ABGR8888 */
    SDL_Texture *pal;      /* INDEX8 with a palette */
    SDL_Palette *palette;
    SDL_Texture *yuv;      /* I420 */
    SDL_Texture *nv12;
    SDL_Texture *yuy2;
    SDL_Texture *tile;     /* 16x16 checks, for wrapping */
    SDL_Texture *xrgb, *bgra, *rgb565, *xrgb1555; /* the sprite in the other listed formats */
    SDL_Texture *white;    /* 1x1 */
    SDL_Texture *nv12full; /* NV12 in BT.709's full range (stream v1.7; SDL converts it before) */
    SDL_Texture *odd;      /* I420, 64x47, filled through a lock in SDL's layout */
} Textures;

static Uint32 sprite_px[64 * 48];
static int resize_failed;

static void make_sprite_pixels(void)
{
    /* 64x48: a colour gradient whose alpha falls off towards the edges */
    int x, y;
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 64; x++) {
            int dx = x < 32 ? x : 63 - x, dy = y < 24 ? y : 47 - y;
            int a = (dx < dy ? dx : dy) * 16;
            if (a > 255) {
                a = 255;
            }
            sprite_px[y * 64 + x] = ((Uint32)a << 24) | ((Uint32)(x * 4) << 16) | ((Uint32)(y * 5) << 8) | (Uint32)((x + y) * 2);
        }
    }
}

static SDL_Texture *texture_of(SDL_Renderer *r, SDL_PixelFormat format, const Uint32 *argb, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, format, SDL_TEXTUREACCESS_STATIC, w, h);
    void *px = SDL_malloc((size_t)w * h * 4);
    if (t && px && SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_ARGB8888, argb, w * 4, format, px, w * 4)) {
        SDL_UpdateTexture(t, NULL, px, w * 4);
    }
    SDL_free(px);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

static void make_textures(SDL_Renderer *r, Textures *t)
{
    Uint32 pre[64 * 48], checks[16 * 16];
    Uint8 idx[64 * 48], planes[64 * 48 * 3 / 2];
    SDL_Color colours[256];
    int i, x, y;

    SDL_zerop(t);
    t->sprite = texture_of(r, SDL_PIXELFORMAT_ARGB8888, sprite_px, 64, 48);
    for (i = 0; i < 64 * 48; i++) {
        Uint32 p = sprite_px[i], a = p >> 24;
        pre[i] = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
    }
    t->premul = texture_of(r, SDL_PIXELFORMAT_ARGB8888, pre, 64, 48);
    t->rgba = texture_of(r, SDL_PIXELFORMAT_RGBA32, sprite_px, 64, 48);
    t->abgr = texture_of(r, SDL_PIXELFORMAT_ABGR8888, sprite_px, 64, 48);

    /* INDEX8: rings of colour indexes, an opaque palette */
    t->pal = SDL_CreateTexture(r, SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STATIC, 64, 48);
    t->palette = SDL_CreatePalette(256);
    for (i = 0; i < 256; i++) {
        colours[i].r = (Uint8)(i * 7);
        colours[i].g = (Uint8)(255 - i);
        colours[i].b = (Uint8)(i * 3 + 40);
        colours[i].a = 255;
    }
    SDL_SetPaletteColors(t->palette, colours, 0, 256);
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 64; x++) {
            idx[y * 64 + x] = (Uint8)(((x - 32) * (x - 32) + (y - 24) * (y - 24)) / 8);
        }
    }
    if (t->pal) {
        SDL_SetTexturePalette(t->pal, t->palette);
        SDL_UpdateTexture(t->pal, NULL, idx, 64);
        SDL_SetTextureBlendMode(t->pal, SDL_BLENDMODE_NONE);
    }

    /* I420, NV12 and YUY2: stripes of luma, a chroma ramp. The chroma stays
       in 80..220: below that a blue or red past -128 reads outside the clamp
       table of SDL's own C conversion (yuv_rgb_std.c), which the software
       renderer uses on the 68k. */
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 64; x++) {
            planes[y * 64 + x] = (Uint8)(16 + ((x / 8) * 27) % 220);
        }
    }
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 32; x++) {
            planes[64 * 48 + y * 32 + x] = (Uint8)(80 + x * 4);
            planes[64 * 48 + 32 * 24 + y * 32 + x] = (Uint8)(80 + y * 6);
        }
    }
    t->yuv = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STATIC, 64, 48);
    if (t->yuv) {
        SDL_UpdateTexture(t->yuv, NULL, planes, 64);
    }
    t->nv12 = SDL_CreateTexture(r, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STATIC, 64, 48);
    if (t->nv12) {
        Uint8 uv[32 * 24 * 2];
        for (y = 0; y < 24; y++) {
            for (x = 0; x < 32; x++) {
                uv[(y * 32 + x) * 2] = (Uint8)(80 + x * 4);
                uv[(y * 32 + x) * 2 + 1] = (Uint8)(80 + y * 6);
            }
        }
        SDL_UpdateNVTexture(t->nv12, NULL, planes, 64, uv, 64);
    }
    t->yuy2 = SDL_CreateTexture(r, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STATIC, 64, 48);
    if (t->yuy2) {
        Uint8 packed[64 * 48 * 2];
        for (y = 0; y < 48; y++) {
            for (x = 0; x < 64; x += 2) {
                Uint8 *q = packed + (y * 64 + x) * 2;
                q[0] = planes[y * 64 + x];
                q[1] = (Uint8)(80 + (x / 2) * 4);
                q[2] = planes[y * 64 + x + 1];
                q[3] = (Uint8)(80 + (y / 2) * 6);
            }
        }
        SDL_UpdateTexture(t->yuy2, NULL, packed, 64 * 2);
    }
    {
        static const Uint32 one = 0xFFFFFFFFu;
        t->white = texture_of(r, SDL_PIXELFORMAT_ARGB8888, &one, 1, 1);
    }
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_NV12);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 64);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 48);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_FULL);
        t->nv12full = SDL_CreateTextureWithProperties(r, props);
        SDL_DestroyProperties(props);
        if (t->nv12full) {
            Uint8 uv[32 * 24 * 2];
            for (y = 0; y < 24; y++) {
                for (x = 0; x < 32; x++) {
                    uv[(y * 32 + x) * 2] = (Uint8)(80 + x * 4);
                    uv[(y * 32 + x) * 2 + 1] = (Uint8)(200 - y * 4);
                }
            }
            SDL_UpdateNVTexture(t->nv12full, NULL, planes, 64, uv, 64);
        }
    }
    t->odd = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 64, 47);
    if (t->odd) {
        void *px;
        int pitch;
        if (SDL_LockTexture(t->odd, NULL, &px, &pitch)) {
            /* SDL's layout: Y at pitch, then U and V at half of it, (h + 1) / 2 rows each */
            Uint8 *yp = (Uint8 *)px, *up = yp + pitch * 47, *vp = up + ((pitch + 1) / 2) * 24;
            for (y = 0; y < 47; y++) {
                for (x = 0; x < 64; x++) {
                    yp[y * pitch + x] = (Uint8)(30 + (x + y) * 2);
                }
            }
            for (y = 0; y < 24; y++) {
                for (x = 0; x < 32; x++) {
                    up[y * ((pitch + 1) / 2) + x] = (Uint8)(90 + y * 4);
                    vp[y * ((pitch + 1) / 2) + x] = (Uint8)(200 - x * 3);
                }
            }
            SDL_UnlockTexture(t->odd);
        }
    }
    t->xrgb = texture_of(r, SDL_PIXELFORMAT_XRGB8888, sprite_px, 64, 48);
    t->bgra = texture_of(r, SDL_PIXELFORMAT_BGRA8888, sprite_px, 64, 48);
    t->rgb565 = texture_of(r, SDL_PIXELFORMAT_RGB565, sprite_px, 64, 48);
    t->xrgb1555 = texture_of(r, SDL_PIXELFORMAT_XRGB1555, sprite_px, 64, 48);

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            checks[y * 16 + x] = (((x / 4) + (y / 4)) & 1) ? 0xFFE0C020u : 0xFF2040A0u;
        }
    }
    t->tile = texture_of(r, SDL_PIXELFORMAT_ARGB8888, checks, 16, 16);
    if (t->tile) {
        SDL_SetTextureBlendMode(t->tile, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(t->tile, SDL_SCALEMODE_NEAREST);
    }
}

static void free_textures(Textures *t)
{
    SDL_Texture *all[] = { t->sprite, t->premul, t->rgba, t->abgr, t->pal, t->yuv, t->nv12, t->yuy2, t->tile,
                           t->xrgb, t->bgra, t->rgb565, t->xrgb1555, t->white, t->nv12full, t->odd };
    size_t i;
    for (i = 0; i < SDL_arraysize(all); i++) {
        if (all[i]) {
            SDL_DestroyTexture(all[i]);
        }
    }
    SDL_DestroyPalette(t->palette);
}

typedef struct Scene
{
    const char *name;
    double allowed;        /* percent of pixels past TOLERANCE */
} Scene;

static const Scene scenes[] = {
    { "clear and opaque fills", EDGE_SHARE },
    { "blended fills (all six blend modes)", EDGE_SHARE },
    { "lines and points, blended", EDGE_SHARE },
    { "copies: alpha, scaled, ADD", EDGE_SHARE },
    { "copies with colour modulation", EDGE_SHARE },
    { "rotated and flipped copies", EDGE_SHARE },
    { "geometry, plain and textured", 6.0 },        /* textured: nearest in software */
    { "YUV textures (I420, NV12, YUY2)", EDGE_SHARE },
    { "premultiplied copies (BLEND, ADD)", EDGE_SHARE },
    { "pixel art scaling", 25.0 },                  /* nearest in software */
    { "palette texture, its palette changed", EDGE_SHARE },
    { "RGBA32 and ABGR8888 textures", EDGE_SHARE },
    { "geometry, wrapped and clamped one way", 6.0 },
    { "viewport and clip", EDGE_SHARE },
    { "render scale", 4.0 },
    { "XRGB8888, BGRA8888, RGB565, XRGB1555 textures", EDGE_SHARE },
    { "a 1x1 stretched, lines of one point, tiling", 6.0 }, /* tiles: nearest in software */
    { "YUV: BT.709 full range, an odd height locked", EDGE_SHARE },
};
#define NSCENES ((int)SDL_arraysize(scenes))

static void quad(SDL_Renderer *r, SDL_Texture *t, float x, float y, float w, float h, float u1, float v1)
{
    SDL_Vertex v[4];
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    int i;
    for (i = 0; i < 4; i++) {
        v[i].color.r = v[i].color.g = v[i].color.b = v[i].color.a = 1.0f;
    }
    v[0].position.x = x;     v[0].position.y = y;     v[0].tex_coord.x = 0;  v[0].tex_coord.y = 0;
    v[1].position.x = x + w; v[1].position.y = y;     v[1].tex_coord.x = u1; v[1].tex_coord.y = 0;
    v[2].position.x = x + w; v[2].position.y = y + h; v[2].tex_coord.x = u1; v[2].tex_coord.y = v1;
    v[3].position.x = x;     v[3].position.y = y + h; v[3].tex_coord.x = 0;  v[3].tex_coord.y = v1;
    SDL_RenderGeometry(r, t, v, 4, order, 6);
}

static void draw(SDL_Renderer *r, int scene, Textures *t)
{
    SDL_FRect fr;
    int i;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 40, 60, 90, 255);
    SDL_RenderClear(r);
    switch (scene) {
    case 0:
        for (i = 0; i < 8; i++) {
            fr.x = (float)(i * 30);
            fr.y = (float)(i * 20);
            fr.w = 50;
            fr.h = 40;
            SDL_SetRenderDrawColor(r, (Uint8)(i * 30), (Uint8)(255 - i * 30), 128, 255);
            SDL_RenderFillRect(r, &fr);
        }
        break;
    case 1: {
        const SDL_BlendMode modes[] = { SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD, SDL_BLENDMODE_MUL,
                                        SDL_BLENDMODE_BLEND_PREMULTIPLIED, SDL_BLENDMODE_ADD_PREMULTIPLIED };
        for (i = 0; i < 6; i++) {
            SDL_SetRenderDrawColor(r, 200, 100, 50, 255);
            fr.x = (float)(8 + i * 41);
            fr.y = 8;
            fr.w = 34;
            fr.h = 176;
            SDL_RenderFillRect(r, &fr);
            SDL_SetRenderDrawBlendMode(r, modes[i]);
            /* premultiplied: the colour already times its alpha */
            if (i >= 4) {
                SDL_SetRenderDrawColor(r, 30, 90, 110, 128);
            } else {
                SDL_SetRenderDrawColor(r, 60, 180, 220, 128);
            }
            fr.x = 0;
            fr.y = (float)(20 + i * 27);
            fr.w = W;
            fr.h = 20;
            SDL_RenderFillRect(r, &fr);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
        break;
    }
    case 2:
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (i = 0; i < 16; i++) {
            SDL_SetRenderDrawColor(r, 255, (Uint8)(i * 16), 0, (Uint8)(80 + i * 10));
            SDL_RenderLine(r, 0, (float)(i * 12), W - 1, (float)(H - 1 - i * 12));
            SDL_RenderPoint(r, (float)(10 + i * 15), 100);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 250, 250, 250, 255);
        {
            const SDL_FPoint box[5] = { { 20, 150 }, { 120, 150 }, { 120, 185 }, { 20, 185 }, { 20, 150 } };
            SDL_RenderLines(r, box, 5);
        }
        break;
    case 3:
        for (i = 0; i < 4; i++) {
            fr.x = 10.0f + i * 60;
            fr.y = 20.0f;
            fr.w = 48.0f + i * 8;
            fr.h = 36.0f + i * 6;
            SDL_SetTextureAlphaMod(t->sprite, (Uint8)(255 - i * 50));
            SDL_SetTextureBlendMode(t->sprite, i == 3 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(r, t->sprite, NULL, &fr);
        }
        SDL_SetTextureAlphaMod(t->sprite, 255);
        SDL_SetTextureBlendMode(t->sprite, SDL_BLENDMODE_BLEND);
        break;
    case 4:
        for (i = 0; i < 4; i++) {
            fr.x = (float)(10 + i * 60);
            fr.y = 60;
            fr.w = 64;
            fr.h = 48;
            SDL_SetTextureColorMod(t->sprite, (Uint8)(255 - i * 60), (Uint8)(100 + i * 50), 200);
            SDL_RenderTexture(r, t->sprite, NULL, &fr);
        }
        SDL_SetTextureColorMod(t->sprite, 255, 255, 255);
        break;
    case 5:
        for (i = 0; i < 4; i++) {
            SDL_FPoint centre = { 32, 24 };
            fr.x = (float)(20 + i * 55);
            fr.y = 60;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTextureRotated(r, t->sprite, NULL, &fr, 30.0 * i + 15.0, &centre, (SDL_FlipMode)(i & 3));
        }
        break;
    case 6: {
        SDL_Vertex v[6];
        for (i = 0; i < 6; i++) {
            v[i].color.r = (float)(i * 40) / 255.0f;
            v[i].color.g = (float)(255 - i * 40) / 255.0f;
            v[i].color.b = 0.5f;
            v[i].color.a = 1.0f;
        }
        v[0].position.x = 20; v[0].position.y = 20; v[0].tex_coord.x = 0; v[0].tex_coord.y = 0;
        v[1].position.x = 200; v[1].position.y = 30; v[1].tex_coord.x = 1; v[1].tex_coord.y = 0;
        v[2].position.x = 60; v[2].position.y = 170; v[2].tex_coord.x = 0; v[2].tex_coord.y = 1;
        v[3].position.x = 120; v[3].position.y = 100; v[3].tex_coord.x = 0; v[3].tex_coord.y = 0;
        v[4].position.x = 250; v[4].position.y = 120; v[4].tex_coord.x = 1; v[4].tex_coord.y = 0;
        v[5].position.x = 180; v[5].position.y = 190; v[5].tex_coord.x = 1; v[5].tex_coord.y = 1;
        SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
        SDL_SetTextureScaleMode(t->sprite, SDL_SCALEMODE_NEAREST);
        SDL_RenderGeometry(r, t->sprite, v + 3, 3, NULL, 0);
        SDL_SetTextureScaleMode(t->sprite, SDL_SCALEMODE_LINEAR);
        break;
    }
    case 7:
        if (t->yuv) {
            fr.x = 20;
            fr.y = 20;
            fr.w = 128;
            fr.h = 96;
            SDL_RenderTexture(r, t->yuv, NULL, &fr);
            fr.x = 160;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTexture(r, t->yuv, NULL, &fr);
        }
        if (t->nv12) {
            fr.x = 160;
            fr.y = 100;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTexture(r, t->nv12, NULL, &fr);
        }
        if (t->yuy2) {
            fr.x = 20;
            fr.y = 130;
            fr.w = 96;
            fr.h = 48;
            SDL_RenderTexture(r, t->yuy2, NULL, &fr);
        }
        break;
    case 8:
        SDL_SetTextureBlendMode(t->premul, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        for (i = 0; i < 3; i++) {
            fr.x = (float)(10 + i * 70);
            fr.y = 20;
            fr.w = 64;
            fr.h = 48;
            SDL_SetTextureAlphaMod(t->premul, (Uint8)(255 - i * 80));
            SDL_RenderTexture(r, t->premul, NULL, &fr);
        }
        SDL_SetTextureAlphaMod(t->premul, 255);
        SDL_SetTextureBlendMode(t->premul, SDL_BLENDMODE_ADD_PREMULTIPLIED);
        fr.x = 40;
        fr.y = 90;
        fr.w = 128;
        fr.h = 96;
        SDL_RenderTexture(r, t->premul, NULL, &fr);
        break;
    case 9:
        SDL_SetTextureScaleMode(t->sprite, SDL_SCALEMODE_PIXELART);
        fr.x = 0;
        fr.y = 0;
        fr.w = 192;
        fr.h = 144;
        SDL_RenderTexture(r, t->sprite, NULL, &fr);
        fr.x = 196;
        fr.y = 10;
        fr.w = 56;
        fr.h = 42;
        SDL_RenderTexture(r, t->sprite, NULL, &fr);
        SDL_SetTextureScaleMode(t->sprite, SDL_SCALEMODE_LINEAR);
        break;
    case 10:
        if (t->pal) {
            SDL_Color c;
            SDL_SetTextureScaleMode(t->pal, SDL_SCALEMODE_NEAREST);
            fr.x = 10;
            fr.y = 10;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTexture(r, t->pal, NULL, &fr);
            fr.x = 80;
            fr.w = 128;
            fr.h = 96;
            SDL_RenderTexture(r, t->pal, NULL, &fr);
            /* a palette change: the next copy shows it, the earlier ones don't */
            c.r = 255;
            c.g = 255;
            c.b = 0;
            c.a = 255;
            for (i = 0; i < 40; i++) {
                SDL_SetPaletteColors(t->palette, &c, i, 1);
            }
            fr.x = 10;
            fr.y = 120;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTexture(r, t->pal, NULL, &fr);
        }
        break;
    case 11:
        fr.x = 10;
        fr.y = 20;
        fr.w = 64;
        fr.h = 48;
        if (t->rgba) {
            SDL_RenderTexture(r, t->rgba, NULL, &fr);
        }
        fr.x = 90;
        fr.w = 96;
        fr.h = 72;
        if (t->abgr) {
            SDL_RenderTexture(r, t->abgr, NULL, &fr);
        }
        fr.x = 10;
        fr.y = 110;
        fr.w = 64;
        fr.h = 48;
        if (t->abgr) {
            SDL_SetTextureColorMod(t->abgr, 255, 128, 64);
            SDL_RenderTextureRotated(r, t->abgr, NULL, &fr, 20.0, NULL, SDL_FLIP_NONE);
            SDL_SetTextureColorMod(t->abgr, 255, 255, 255);
        }
        break;
    case 12:
        SDL_SetRenderTextureAddressMode(r, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
        quad(r, t->tile, 8, 8, 112, 80, 3.5f, 2.5f);
        SDL_SetRenderTextureAddressMode(r, SDL_TEXTURE_ADDRESS_CLAMP, SDL_TEXTURE_ADDRESS_WRAP);
        quad(r, t->tile, 136, 8, 112, 80, 2.0f, 3.0f);
        SDL_SetRenderTextureAddressMode(r, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_CLAMP);
        quad(r, t->tile, 8, 100, 112, 80, 3.0f, 2.0f);
        SDL_SetRenderTextureAddressMode(r, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
        break;
    case 13: {
        SDL_Rect vp = { 30, 20, 180, 140 }, clip = { 10, 15, 140, 100 };
        SDL_SetRenderViewport(r, &vp);
        SDL_SetRenderDrawColor(r, 90, 30, 30, 255);
        SDL_RenderFillRect(r, NULL);
        SDL_SetRenderClipRect(r, &clip);
        SDL_SetRenderDrawColor(r, 200, 220, 40, 255);
        fr.x = -20;
        fr.y = -20;
        fr.w = 100;
        fr.h = 60;
        SDL_RenderFillRect(r, &fr);
        fr.x = 60;
        fr.y = 40;
        fr.w = 128;
        fr.h = 96;
        SDL_RenderTexture(r, t->sprite, NULL, &fr);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 160);
        SDL_RenderLine(r, 0, 0, 179, 139);
        {
            SDL_FPoint centre = { 32, 24 };
            fr.x = 0;
            fr.y = 60;
            fr.w = 64;
            fr.h = 48;
            SDL_RenderTextureRotated(r, t->sprite, NULL, &fr, 40.0, &centre, SDL_FLIP_NONE);
        }
        quad(r, t->sprite, 90, 0, 64, 48, 1.0f, 1.0f);
        SDL_SetRenderClipRect(r, NULL);
        SDL_SetRenderViewport(r, NULL);
        break;
    }
    case 14:
        SDL_SetRenderScale(r, 1.5f, 1.5f);
        SDL_SetRenderDrawColor(r, 220, 120, 60, 255);
        fr.x = 8;
        fr.y = 8;
        fr.w = 40;
        fr.h = 30;
        SDL_RenderFillRect(r, &fr);
        fr.x = 60;
        fr.y = 10;
        fr.w = 64;
        fr.h = 48;
        SDL_RenderTexture(r, t->sprite, NULL, &fr);
        {
            SDL_FPoint centre = { 32, 24 };
            fr.x = 20;
            fr.y = 60;
            SDL_RenderTextureRotated(r, t->sprite, NULL, &fr, 25.0, &centre, SDL_FLIP_HORIZONTAL);
        }
        SDL_SetRenderScale(r, 1.0f, 1.0f);
        break;
    case 15: {
        SDL_Texture *four[4];
        four[0] = t->xrgb;
        four[1] = t->bgra;
        four[2] = t->rgb565;
        four[3] = t->xrgb1555;
        for (i = 0; i < 4; i++) {
            if (!four[i]) {
                continue;
            }
            fr.x = (float)(8 + i * 62);
            fr.y = 10;
            fr.w = 56;
            fr.h = 42;
            SDL_RenderTexture(r, four[i], NULL, &fr);
            fr.y = 70;
            fr.w = 64;
            fr.h = 48;
            SDL_SetTextureBlendMode(four[i], SDL_BLENDMODE_NONE);
            SDL_RenderTexture(r, four[i], NULL, &fr);
            SDL_SetTextureBlendMode(four[i], SDL_BLENDMODE_BLEND);
            fr.y = 130;
            SDL_SetTextureColorMod(four[i], 200, 255, 120);
            SDL_RenderTexture(r, four[i], NULL, &fr);
            SDL_SetTextureColorMod(four[i], 255, 255, 255);
        }
        break;
    }
    case 16:
        /* a 1x1 tinted and stretched 200 times (past COMPOSITE_AFFINE's 127) */
        if (t->white) {
            SDL_SetTextureColorMod(t->white, 255, 60, 30);
            fr.x = 20;
            fr.y = 10;
            fr.w = 200;
            fr.h = 30;
            SDL_RenderTexture(r, t->white, NULL, &fr);
            SDL_SetTextureColorMod(t->white, 255, 255, 255);
        }
        /* lines of one point, and 1x1 outlines, in both draw modes */
        for (i = 0; i < 2; i++) {
            SDL_SetRenderDrawBlendMode(r, i ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 250, 250, 60, 200);
            SDL_RenderLine(r, (float)(30 + i * 10), 50, (float)(30 + i * 10), 50);
            fr.x = (float)(60 + i * 10);
            fr.y = 50;
            fr.w = 1;
            fr.h = 1;
            SDL_RenderRect(r, &fr);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        /* tiling: 64 repeats across, then 256 (past the core's 128) */
        if (t->tile) {
            fr.x = 0;
            fr.y = 60;
            fr.w = W;
            fr.h = 60;
            SDL_RenderTextureTiled(r, t->tile, NULL, 0.25f, &fr);
            fr.y = 130;
            SDL_RenderTextureTiled(r, t->tile, NULL, 0.0625f, &fr);
        }
        break;
    case 17:
        fr.x = 20;
        fr.y = 20;
        fr.w = 96;
        fr.h = 72;
        if (t->nv12full) {
            SDL_RenderTexture(r, t->nv12full, NULL, &fr);
        }
        fr.x = 140;
        fr.w = 64;
        fr.h = 47;
        if (t->odd) {
            SDL_RenderTexture(r, t->odd, NULL, &fr);
        }
        fr.y = 100;
        fr.w = 96;
        fr.h = 70;
        if (t->odd) {
            SDL_RenderTexture(r, t->odd, NULL, &fr);
        }
        break;
    }
}

static Uint32 *render_all(const char *driver, char *name, size_t namelen)
{
    SDL_Window *win;
    SDL_Renderer *r;
    SDL_Texture *target;
    Textures t;
    Uint32 *out = (Uint32 *)SDL_calloc(NSCENES, W * H * 4);
    int s;

    win = SDL_CreateWindow(driver, W, H, 0);
    r = win ? SDL_CreateRenderer(win, driver) : NULL;
    if (!r || !out) {
        printf("%s: no renderer (%s)\n", driver, SDL_GetError());
        return NULL;
    }
    SDL_strlcpy(name, SDL_GetRendererName(r), namelen);
    make_textures(r, &t);
    target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    for (s = 0; s < NSCENES; s++) {
        SDL_Surface *got, *argb;
        SDL_SetRenderTarget(r, target);
        draw(r, s, &t);
        got = SDL_RenderReadPixels(r, NULL);
        argb = got ? SDL_ConvertSurface(got, SDL_PIXELFORMAT_ARGB8888) : NULL;
        if (argb) {
            int y;
            for (y = 0; y < H && y < argb->h; y++) {
                SDL_memcpy(out + (size_t)s * W * H + (size_t)y * W, (Uint8 *)argb->pixels + (size_t)y * argb->pitch, W * 4);
            }
        } else {
            printf("%s: scene %d not read back (%s)\n", driver, s, SDL_GetError());
        }
        SDL_DestroySurface(argb);
        SDL_DestroySurface(got);
        SDL_SetRenderTarget(r, NULL);
        SDL_RenderTexture(r, target, NULL, NULL);
        SDL_RenderPresent(r);
    }
    /* The window made larger: the renderer takes its new surface. */
    if (SDL_SetWindowSize(win, W + 32, H + 16)) {
        int k, ww = 0, wh = 0, ow = 0, oh = 0;
        for (k = 0; k < 50; k++) {
            SDL_PumpEvents();
            SDL_GetWindowSizeInPixels(win, &ww, &wh);
            if (ww == W + 32 && wh == H + 16) {
                break;
            }
            SDL_Delay(10);
        }
        SDL_SetRenderDrawColor(r, 10, 200, 10, 255);
        SDL_RenderClear(r);
        SDL_RenderTexture(r, target, NULL, NULL);
        SDL_RenderPresent(r);
        SDL_GetRenderOutputSize(r, &ow, &oh);
        printf("%s: window %dx%d after resizing, the renderer's output %dx%d%s\n", driver, ww, wh, ow, oh,
               (ow == ww && oh == wh) ? "" : "  DIFFERENT");
        if (ow != ww || oh != wh) {
            resize_failed = 1;
        }
    }
    SDL_DestroyTexture(target);
    free_textures(&t);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    return out;
}

int main(int argc, char *argv[])
{
    char a_name[32] = "", b_name[32] = "";
    Uint32 *a, *b;
    int s, failed = 0;
    const char *dump = argc > 1 ? argv[1] : NULL;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    make_sprite_pixels();
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
        printf("%-42s %6ld pixels differ (%5.2f%%), largest %3d  %s\n", scenes[s].name, off, share, worst,
               share <= scenes[s].allowed ? "same" : "DIFFERENT");
        if (share > scenes[s].allowed) {
            failed = 1;
        }
    }
    /* RENDERCOMPARE_DUMP=prefix (or the first argument): each scene's two
       pictures as PPM files, to look at */
    if (!dump) {
        dump = SDL_getenv("RENDERCOMPARE_DUMP");
    }
    if (dump) {
        for (s = 0; s < NSCENES; s++) {
            int k;
            for (k = 0; k < 2; k++) {
                char path[512];
                FILE *f;
                const Uint32 *p = (k ? b : a) + (size_t)s * W * H;
                SDL_snprintf(path, sizeof(path), "%s%02d-%s.ppm", dump, s, k ? "opengpu" : "software");
                f = fopen(path, "wb");
                if (f) {
                    int i;
                    fprintf(f, "P6\n%d %d\n255\n", W, H);
                    for (i = 0; i < W * H; i++) {
                        unsigned char rgb[3] = { (unsigned char)(p[i] >> 16), (unsigned char)(p[i] >> 8), (unsigned char)p[i] };
                        fwrite(rgb, 1, 3, f);
                    }
                    fclose(f);
                }
            }
        }
    }
    SDL_free(a);
    SDL_free(b);
    SDL_Quit();
    return (failed || resize_failed) ? 5 : 0;
}
