/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * satladder: who decodes what, through SDL2_image.module and
 * SDL2_mixer.module, and how long it takes.
 *
 *   satladder DIR [SECONDS] [window]
 *
 * For each picture in DIR (photo.jpg, photo.png, pal.png, photo.webp,
 * photo.avif, photo.tga, photo.iff, big.jpg, big.png): IMG_Load with
 * SDL_IMAGE_DECODER "auto" (the ladder) and "cpu" (SDL2_image's own
 * decoders), the best time of three each, the surfaces' formats, and how
 * far the pixels are from the lossless original (photo.png, big.png): the
 * largest difference, or for a lossy format the mean; then IMG_LoadTexture
 * onto a renderer: a software one on a surface, or with "window" a window's. For each sound (tone.wav, tone.ogg,
 * tone.mp3, tone.flac, tune.mod): Mix_LoadWAV both ways, with the length
 * each decoded; then Mix_LoadMUS and Mix_PlayMusic for SECONDS (default 3),
 * with the position music had reached. One line each; the last says how
 * many failed. SetEnv SATLADDER_DECODER cpu (or service) puts every
 * "auto" load on that decoder instead, to compare runs. */
#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include <stdio.h>

static int failures = 0;

static void say(const char *what, int ok, const char *line)
{
    printf("%-14s %s  %s\n", what, ok ? "pass" : "FAIL", line);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

/* Where the end has got to (SATLADDER_STEPS set), for a lab run that stops. */
static void step(const char *what)
{
    if (SDL_getenv("SATLADDER_STEPS")) {
        printf("step %s\n", what);
        fflush(stdout);
    }
}

static double ms_since(Uint64 t0)
{
    return (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* The largest difference of any channel (mean != 0: the mean, x10), both
   surfaces as ARGB8888. */
static int diff(SDL_Surface *a, SDL_Surface *b, int mean)
{
    double sum = 0.0;
    SDL_Surface *x = SDL_ConvertSurfaceFormat(a, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_Surface *y = SDL_ConvertSurfaceFormat(b, SDL_PIXELFORMAT_ARGB8888, 0);
    int worst = -1, i, j, k;
    if (x && y && x->w == y->w && x->h == y->h) {
        worst = 0;
        for (j = 0; j < x->h; j++) {
            const Uint8 *p = (const Uint8 *)x->pixels + j * x->pitch;
            const Uint8 *q = (const Uint8 *)y->pixels + j * y->pitch;
            for (i = 0; i < x->w * 4; i++) {
                if ((i & 3) == 0 && (a->format->Amask == 0 || b->format->Amask == 0)) {
                    continue;   /* no alpha on one side */
                }
                k = p[i] > q[i] ? p[i] - q[i] : q[i] - p[i];
                sum += k;
                if (k > worst) {
                    worst = k;
                }
            }
        }
    }
    if (mean && worst >= 0) {
        worst = (int)(sum * 10.0 / ((double)x->w * x->h * 3.0) + 0.5);
    }
    SDL_FreeSurface(x);
    SDL_FreeSurface(y);
    return worst;
}

/* The best of three loads, with the decoder given. */
static SDL_Surface *load(const char *path, const char *decoder, double *ms)
{
    SDL_Surface *s = NULL;
    const char *force = SDL_getenv("SATLADDER_DECODER");
    int i;
    if (force && SDL_strcmp(decoder, "auto") == 0) {
        decoder = force;
    }
    SDL_SetHint("SDL_IMAGE_DECODER", decoder);
    *ms = 1e9;
    for (i = 0; i < 3; i++) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        double t;
        SDL_FreeSurface(s);
        s = IMG_Load(path);
        t = ms_since(t0);
        if (t < *ms) {
            *ms = t;
        }
    }
    SDL_SetHint("SDL_IMAGE_DECODER", "auto");
    return s;
}

static void picture(const char *dir, const char *name, const char *orig, int lossy, SDL_Renderer *r)
{
    char path[256], line[300];
    SDL_Surface *a, *c, *o;
    double ta, tc, to;
    int da, dc;

    SDL_snprintf(path, sizeof(path), "%s/%s", dir, orig);
    o = load(path, "cpu", &to);
    SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
    a = load(path, "auto", &ta);
    c = load(path, "cpu", &tc);
    if (!a || !o) {
        SDL_snprintf(line, sizeof(line), "%s: ladder failed: %s", name, IMG_GetError());
        say("image", 0, line);
        SDL_FreeSurface(a);
        SDL_FreeSurface(c);
        SDL_FreeSurface(o);
        return;
    }
    da = diff(a, o, lossy);
    dc = c ? diff(c, o, lossy) : -1;
    SDL_snprintf(line, sizeof(line), "%s %dx%d: ladder %.1f ms (%s, %s %d.%d), cpu %s%.1f ms (%s, %s %d.%d)",
                 name, a->w, a->h, ta, SDL_GetPixelFormatName(a->format->format) + 16,
                 lossy ? "mean diff" : "max diff", da / (lossy ? 10 : 1), lossy ? da % 10 : 0,
                 c ? "" : "failed ", tc, c ? SDL_GetPixelFormatName(c->format->format) + 16 : "-",
                 lossy ? "mean diff" : "max diff", dc / (lossy ? 10 : 1), lossy ? (dc < 0 ? 0 : dc % 10) : 0);
    /* lossless: exact; lossy: about as far from the original as SDL2_image's
       own decoder is (within 1.0 on the mean), or within the format's own
       loss (a mean of 15) where SDL2_image has no decoder */
    say("image", da >= 0 && (lossy ? (dc >= 0 ? da <= dc + 10 : da <= 150) : da == 0), line);
    SDL_FreeSurface(o);
    if (r) {
        SDL_Texture *t;
        Uint32 fmt;
        int w = 0, h = 0;
        t = IMG_LoadTexture(r, path);
        if (t) {
            SDL_QueryTexture(t, &fmt, NULL, &w, &h);
        }
        SDL_snprintf(line, sizeof(line), "%s: texture %dx%d %s", name, w, h, t ? SDL_GetPixelFormatName(fmt) : IMG_GetError());
        say("texture", t && w == a->w && h == a->h, line);
        if (t) {
            SDL_RenderCopy(r, t, NULL, NULL);
            SDL_RenderPresent(r);
            SDL_DestroyTexture(t);
        }
    }
    SDL_FreeSurface(a);
    SDL_FreeSurface(c);
}

static void sound(const char *dir, const char *name, int seconds)
{
    char path[256], line[256];
    Mix_Chunk *a, *c;
    Mix_Music *m;
    double ta, tc, pos = -1.0;
    Uint64 t0;

    SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
    SDL_SetHint("SDL_MIXER_DECODER", "auto");
    t0 = SDL_GetPerformanceCounter();
    a = Mix_LoadWAV(path);
    ta = ms_since(t0);
    SDL_SetHint("SDL_MIXER_DECODER", "cpu");
    t0 = SDL_GetPerformanceCounter();
    c = Mix_LoadWAV(path);
    tc = ms_since(t0);
    SDL_SetHint("SDL_MIXER_DECODER", "auto");
    SDL_snprintf(line, sizeof(line), "%s: chunk ladder %u bytes %.0f ms, cpu %u bytes %.0f ms%s%s",
                 name, a ? (unsigned)a->alen : 0, ta, c ? (unsigned)c->alen : 0, tc,
                 a ? "" : ": ", a ? "" : Mix_GetError());
    /* the same length within a tenth (the service may halve the rate first) */
    say("chunk", a && (!c || (a->alen > c->alen * 9 / 10 && a->alen < c->alen * 11 / 10)), line);
    if (a) {
        Mix_PlayChannel(-1, a, 0);
        SDL_Delay(200);
        Mix_HaltChannel(-1);
    }
    Mix_FreeChunk(a);
    Mix_FreeChunk(c);

    m = Mix_LoadMUS(path);
    if (m && Mix_PlayMusic(m, 1) == 0) {
        SDL_Delay(seconds * 1000);
        pos = Mix_GetMusicPosition(m);
    }
    SDL_snprintf(line, sizeof(line), "%s: music %s, playing %d, at %.2f s after %d s%s%s", name,
                 m ? (Mix_GetMusicType(m) == MUS_MOD ? "MOD" : Mix_GetMusicType(m) == MUS_OGG ? "OGG" : Mix_GetMusicType(m) == MUS_MP3 ? "MP3" : Mix_GetMusicType(m) == MUS_FLAC ? "FLAC" : Mix_GetMusicType(m) == MUS_WAV ? "WAV" : "other") : "-", Mix_PlayingMusic(), pos, seconds,
                 m ? "" : ": ", m ? "" : Mix_GetError());
    /* tracker modules don't all give a position */
    say("music", m && (Mix_PlayingMusic() || pos >= seconds - 1) && (pos < 0 ? Mix_GetMusicType(m) == MUS_MOD : pos > seconds * 0.5), line);
    Mix_HaltMusic();
    Mix_FreeMusic(m);
}

/* SDL's log (SDL2_mixer's "Loaded music with ...") on standard output. */
static void SDLCALL log_out(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    printf("  log: %s\n", message);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "PROGDIR:satmedia";
    int seconds = argc > 2 ? SDL_atoi(argv[2]) : 3;
    SDL_Window *w = NULL;
    SDL_Renderer *r = NULL;
    SDL_Surface *target = NULL;
    char line[200];

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    SDL_LogSetOutputFunction(log_out, NULL);
    SDL_SetHint("SDL_MIXER_DEBUG_MUSIC_INTERFACES", "1");
    SDL_snprintf(line, sizeof(line), "IMG_Init %x of %x, Mix_Init %x", IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP | IMG_INIT_AVIF),
                 IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP | IMG_INIT_AVIF, Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC | MIX_INIT_MOD));
    say("init", 1, line);
    if (argc > 3 && SDL_strcmp(argv[3], "window") == 0) {
        w = SDL_CreateWindow("satladder", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 240, 0);
    }
    if (w) {
        r = SDL_CreateRenderer(w, -1, 0);
    } else if ((target = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888)) != NULL) {
        r = SDL_CreateSoftwareRenderer(target);
    }
    picture(dir, "photo.jpg", "photo.png", 1, r);
    picture(dir, "photo.png", "photo.png", 0, r);
    picture(dir, "pal.png", "pal.png", 0, r);
    picture(dir, "photo.webp", "photo.png", 1, r);
    picture(dir, "photo.avif", "photo.png", 1, r);
    picture(dir, "photo.tga", "photo.png", 0, r);
    picture(dir, "photo.iff", "photo.iff", 0, r);
    picture(dir, "big.jpg", "big.png", 1, r);
    picture(dir, "big.png", "big.png", 0, r);
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048) < 0) {
        say("audio", 0, Mix_GetError());
    } else {
        int f, ch;
        Uint16 fmt;
        Mix_QuerySpec(&f, &fmt, &ch);
        SDL_snprintf(line, sizeof(line), "Mix_OpenAudio: %d Hz, %d channels, format %x", f, ch, fmt);
        say("audio", 1, line);
        sound(dir, "tone.wav", seconds);
        sound(dir, "tone.ogg", seconds);
        sound(dir, "tone.mp3", seconds);
        sound(dir, "tone.flac", seconds);
        sound(dir, "tune.mod", seconds);
        step("Mix_CloseAudio");
        Mix_CloseAudio();
    }
    step("Mix_Quit");
    Mix_Quit();
    step("IMG_Quit");
    IMG_Quit();
    step("SDL_Quit");
    if (r) {
        SDL_DestroyRenderer(r);
    }
    if (w) {
        SDL_DestroyWindow(w);
    }
    SDL_FreeSurface(target);
    SDL_Quit();
    printf("SATLADDER_DONE %d failed\n", failures);
    return failures ? 5 : 0;
}
