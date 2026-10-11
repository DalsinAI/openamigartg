/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDLTextCheck: distinguish physical SDL keyboard events from translated
 * SDL_TEXTINPUT on the AmigaOS 3.x backend. It is deliberately small enough
 * to run both against SDL2.module and against libSDL2_static.a.
 *
 * Controls:
 *   Escape  quit
 *   F1      clear counters
 *   F2      toggle SDL_StartTextInput / SDL_StopTextInput
 */
#include "SDL.h"

#include <stdio.h>
#include <string.h>

static unsigned key_down_count;
static unsigned key_up_count;
static unsigned text_count;
static int focused;
static char last_text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
static char last_hex[SDL_TEXTINPUTEVENT_TEXT_SIZE * 3 + 1];

static void bytes_to_hex(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t left = sizeof(last_hex);
    char *out = last_hex;

    last_hex[0] = '\0';
    while (*p && left > 3) {
        int n = SDL_snprintf(out, left, "%02X%s", (unsigned)*p,
                             p[1] ? " " : "");
        if (n <= 0 || (size_t)n >= left) {
            break;
        }
        out += n;
        left -= (size_t)n;
        p++;
    }
}

static void draw_status(SDL_Window *window)
{
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    SDL_Rect bar;
    Uint32 bg;
    Uint32 ink;
    int w;
    int h;

    if (!surface) {
        return;
    }

    bg = SDL_MapRGB(surface->format,
                    SDL_IsTextInputActive() ? 28 : 72,
                    SDL_IsTextInputActive() ? 52 : 32,
                    SDL_IsTextInputActive() ? 78 : 32);
    ink = SDL_MapRGB(surface->format, 220, 220, 220);
    SDL_FillRect(surface, NULL, bg);

    w = surface->w;
    h = surface->h;

    bar.x = 20;
    bar.y = 24;
    bar.w = (int)(key_down_count % (unsigned)(w > 60 ? w - 40 : 1));
    bar.h = 18;
    SDL_FillRect(surface, &bar, ink);

    bar.y = 58;
    bar.w = (int)(key_up_count % (unsigned)(w > 60 ? w - 40 : 1));
    SDL_FillRect(surface, &bar, ink);

    bar.y = 92;
    bar.w = (int)(text_count % (unsigned)(w > 60 ? w - 40 : 1));
    bar.h = h > 130 ? 26 : 12;
    SDL_FillRect(surface, &bar, SDL_MapRGB(surface->format, 238, 76, 82));

    if (focused) {
        bar.x = w > 44 ? w - 36 : 0;
        bar.y = 8;
        bar.w = 28;
        bar.h = 28;
        SDL_FillRect(surface, &bar, SDL_MapRGB(surface->format, 96, 214, 128));
    }

    SDL_UpdateWindowSurface(window);
}

static void update_title(SDL_Window *window)
{
    char title[256];

    SDL_snprintf(title, sizeof(title),
                 "SDLTextCheck | text %s | focus %s | key %u/%u | text %u | bytes %s",
                 SDL_IsTextInputActive() ? "ON" : "OFF",
                 focused ? "YES" : "NO",
                 key_down_count, key_up_count, text_count,
                 last_hex[0] ? last_hex : "-");
    SDL_SetWindowTitle(window, title);
    draw_status(window);
}

static void print_key(const char *kind, const SDL_KeyboardEvent *key)
{
    printf("%-8s scancode=%d name=\"%s\" key=%ld keyname=\"%s\" mods=0x%04x repeat=%u\n",
           kind,
           (int)key->keysym.scancode,
           SDL_GetScancodeName(key->keysym.scancode),
           (long)key->keysym.sym,
           SDL_GetKeyName(key->keysym.sym),
           (unsigned)key->keysym.mod,
           (unsigned)key->repeat);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Event event;
    int running = 1;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDLTextCheck: SDL_Init failed: %s\n", SDL_GetError());
        return 20;
    }

    window = SDL_CreateWindow("SDLTextCheck",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              720, 180, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDLTextCheck: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }

    last_text[0] = '\0';
    last_hex[0] = '\0';
    focused = SDL_GetKeyboardFocus() == window;
    SDL_StartTextInput();

    printf("SDLTextCheck ready. Escape quits; F1 clears; F2 toggles text input.\n");
    printf("Expected printable press: one KEYDOWN, one TEXTINPUT, one KEYUP.\n");
    fflush(stdout);
    update_title(window);

    while (running && SDL_WaitEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            running = 0;
            break;

        case SDL_KEYDOWN:
            key_down_count++;
            print_key("KEYDOWN", &event.key);

            if (!event.key.repeat && event.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            } else if (!event.key.repeat && event.key.keysym.sym == SDLK_F1) {
                key_down_count = 0;
                key_up_count = 0;
                text_count = 0;
                last_text[0] = '\0';
                last_hex[0] = '\0';
                printf("CLEAR\n");
                fflush(stdout);
            } else if (!event.key.repeat && event.key.keysym.sym == SDLK_F2) {
                if (SDL_IsTextInputActive()) {
                    SDL_StopTextInput();
                } else {
                    SDL_StartTextInput();
                }
                printf("TEXTSTATE %s\n",
                       SDL_IsTextInputActive() ? "enabled" : "disabled");
                fflush(stdout);
            }
            update_title(window);
            break;

        case SDL_KEYUP:
            key_up_count++;
            print_key("KEYUP", &event.key);
            update_title(window);
            break;

        case SDL_TEXTINPUT:
            text_count++;
            SDL_strlcpy(last_text, event.text.text, sizeof(last_text));
            bytes_to_hex(last_text);
            printf("TEXTINPUT window=%u bytes=%s text=\"%s\"\n",
                   (unsigned)event.text.windowID,
                   last_hex[0] ? last_hex : "-", last_text);
            fflush(stdout);
            update_title(window);
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                focused = 1;
                printf("FOCUS gained\n");
                fflush(stdout);
                update_title(window);
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                focused = 0;
                printf("FOCUS lost\n");
                fflush(stdout);
                update_title(window);
            } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                       event.window.event == SDL_WINDOWEVENT_RESIZED ||
                       event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                draw_status(window);
            }
            break;

        default:
            break;
        }
    }

    SDL_StopTextInput();
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("SDLTextCheck done: keydown=%u keyup=%u text=%u\n",
           key_down_count, key_up_count, text_count);
    fflush(stdout);
    return 0;
}
