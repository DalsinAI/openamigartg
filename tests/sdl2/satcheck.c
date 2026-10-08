/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * satcheck: SDL 2's satellite libraries on AmigaOS 3.x, one line each.
 *   image  saves a picture as PNG and JPEG and loads both back
 *   mixer  which formats it decodes; plays a WAV and a ProTracker module
 *          made in memory (through libxmp)
 *   ttf    renders text with a TrueType font (satcheck FONT.ttf)
 *   net    a UDP packet to itself, and an HTTP HEAD to aminet.net, over
 *          bsdsocket.library
 * Exit 0 when every check that could run passed. */
#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"
#include "SDL_net.h"
#include <stdio.h>

static int failures = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("%-6s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

static void check_image(void)
{
    char line[200];
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 64, 32, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *png, *jpg;
    Uint32 *px;
    int x, y, ok;

    for (y = 0; y < 32; y++) {
        px = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (x = 0; x < 64; x++) {
            px[x] = 0xFF000000u | ((Uint32)(x * 4) << 16) | ((Uint32)(y * 8) << 8) | 0x40u;
        }
    }
    ok = IMG_SavePNG(s, "T:satcheck.png") == 0 && IMG_SaveJPG(s, "T:satcheck.jpg", 90) == 0;
    png = IMG_Load("T:satcheck.png");
    jpg = IMG_Load("T:satcheck.jpg");
    SDL_snprintf(line, sizeof(line), "PNG %dx%d, JPEG %dx%d, loaded back%s%s",
                 png ? png->w : 0, png ? png->h : 0, jpg ? jpg->w : 0, jpg ? jpg->h : 0,
                 (ok && png && jpg) ? "" : ": ", (ok && png && jpg) ? "" : IMG_GetError());
    if (ok && png && jpg) {
        /* PNG exactly, JPEG within its loss, at two corners */
        SDL_Surface *a = SDL_ConvertSurfaceFormat(png, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_Surface *b = SDL_ConvertSurfaceFormat(jpg, SDL_PIXELFORMAT_ARGB8888, 0);
        Uint32 a0 = ((Uint32 *)a->pixels)[0], a1 = ((Uint32 *)((Uint8 *)a->pixels + 31 * a->pitch))[63];
        Uint32 b1 = ((Uint32 *)((Uint8 *)b->pixels + 31 * b->pitch))[63];
        int d = (int)((b1 >> 16) & 255) - 252;
        ok = a0 == 0xFF000040u && a1 == 0xFFFCF840u && d > -12 && d < 12;
        SDL_snprintf(line + SDL_strlen(line), sizeof(line) - SDL_strlen(line),
                     "; corners %08x %08x (PNG), %08x (JPEG)", (unsigned)a0, (unsigned)a1, (unsigned)b1);
        SDL_FreeSurface(a);
        SDL_FreeSurface(b);
    }
    report("image", ok && png && jpg && png->w == 64 && jpg->h == 32, line);
    SDL_FreeSurface(png);
    SDL_FreeSurface(jpg);
    SDL_FreeSurface(s);
}

/* A one-pattern ProTracker module: a square wave on C-3, held. */
static Uint8 *make_mod(int *size)
{
    const int len = 1084 + 1024 + 64;
    Uint8 *m = (Uint8 *)SDL_calloc(1, len);
    int i;
    SDL_memcpy(m, "satcheck", 8);
    /* sample 1: 32 words, volume 64, loop over the whole sample */
    m[20 + 22] = 0; m[20 + 23] = 32;
    m[20 + 25] = 64;
    m[20 + 26] = 0; m[20 + 27] = 0;
    m[20 + 28] = 0; m[20 + 29] = 32;
    for (i = 1; i < 31; i++) {
        m[20 + i * 30 + 29] = 1; /* empty samples: loop length 1 word */
    }
    m[950] = 1;                  /* song length */
    m[951] = 127;
    SDL_memcpy(m + 1080, "M.K.", 4);
    /* row 0, channel 0: sample 1, period 214 (C-3) */
    m[1084] = 0x00; m[1085] = 214; m[1086] = 0x10; m[1087] = 0x00;
    for (i = 0; i < 64; i++) {
        m[1084 + 1024 + i] = (Uint8)((i & 16) ? 0x40 : 0xC0);
    }
    *size = len;
    return m;
}

/* One second of a 440 Hz tone as a WAV file in memory. */
static Uint8 *make_wav(int *size)
{
    const int rate = 22050, n = rate;
    const int len = 44 + n * 2;
    Uint8 *w = (Uint8 *)SDL_calloc(1, len);
    Sint16 *s = (Sint16 *)(w + 44);
    int i;
    SDL_memcpy(w, "RIFF", 4);
    *(Uint32 *)(w + 4) = SDL_SwapLE32(len - 8);
    SDL_memcpy(w + 8, "WAVEfmt ", 8);
    *(Uint32 *)(w + 16) = SDL_SwapLE32(16);
    *(Uint16 *)(w + 20) = SDL_SwapLE16(1);
    *(Uint16 *)(w + 22) = SDL_SwapLE16(1);
    *(Uint32 *)(w + 24) = SDL_SwapLE32(rate);
    *(Uint32 *)(w + 28) = SDL_SwapLE32(rate * 2);
    *(Uint16 *)(w + 32) = SDL_SwapLE16(2);
    *(Uint16 *)(w + 34) = SDL_SwapLE16(16);
    SDL_memcpy(w + 36, "data", 4);
    *(Uint32 *)(w + 40) = SDL_SwapLE32(n * 2);
    for (i = 0; i < n; i++) {
        s[i] = (Sint16)SDL_SwapLE16((Uint16)(((i * 440 * 2 / rate) & 1) ? 1500 : -1500));
    }
    *size = len;
    return w;
}

static void check_mixer(void)
{
    char line[256];
    int flags = Mix_Init(MIX_INIT_FLAC | MIX_INIT_MOD | MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_MID);
    int i, n, size, playing_mod = 0;
    Uint8 *data;
    Mix_Chunk *chunk;
    Mix_Music *mus;

    SDL_snprintf(line, sizeof(line), "decoders:%s%s%s%s%s", (flags & MIX_INIT_OGG) ? " Ogg" : "",
                 (flags & MIX_INIT_MP3) ? " MP3" : "", (flags & MIX_INIT_FLAC) ? " FLAC" : "",
                 (flags & MIX_INIT_MOD) ? " MOD" : "", (flags & MIX_INIT_MID) ? " MIDI" : "");
    report("mixer", (flags & (MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC | MIX_INIT_MOD)) ==
                    (MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC | MIX_INIT_MOD), line);
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024) < 0) {
        SDL_snprintf(line, sizeof(line), "Mix_OpenAudio: %s", Mix_GetError());
        report("mixer", 0, line);
        return;
    }
    data = make_wav(&size);
    chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(data, size), 1);
    n = chunk ? Mix_PlayChannel(-1, chunk, 0) : -1;
    SDL_Delay(300);
    SDL_snprintf(line, sizeof(line), "a WAV made in memory: loaded %s, channel %d playing %d",
                 chunk ? "yes" : Mix_GetError(), n, n >= 0 ? Mix_Playing(n) : 0);
    report("mixer", chunk && n >= 0 && Mix_Playing(n), line);
    Mix_HaltChannel(-1);
    Mix_FreeChunk(chunk);
    SDL_free(data);

    data = make_mod(&size);
    mus = Mix_LoadMUS_RW(SDL_RWFromConstMem(data, size), 1);
    if (mus && Mix_PlayMusic(mus, -1) == 0) {
        for (i = 0; i < 10; i++) {
            SDL_Delay(50);
        }
        playing_mod = Mix_PlayingMusic();
    }
    SDL_snprintf(line, sizeof(line), "a ProTracker module made in memory: %s, type %d, playing %d",
                 mus ? "loaded" : Mix_GetError(), mus ? (int)Mix_GetMusicType(mus) : -1, playing_mod);
    report("mixer", mus && Mix_GetMusicType(mus) == MUS_MOD && playing_mod, line);
    Mix_HaltMusic();
    Mix_FreeMusic(mus);
    SDL_free(data);
    Mix_CloseAudio();
    Mix_Quit();
}

static void check_ttf(const char *font_file)
{
    char line[200];
    TTF_Font *font;
    SDL_Surface *s;

    if (TTF_Init() < 0) {
        report("ttf", 0, TTF_GetError());
        return;
    }
    if (!font_file) {
        printf("ttf    skip  no font given (satcheck FONT.ttf)\n");
        TTF_Quit();
        return;
    }
    font = TTF_OpenFont(font_file, 24);
    s = font ? TTF_RenderUTF8_Blended(font, "SDL 2 on AmigaOS 3.2", (SDL_Color){ 255, 255, 255, 255 }) : NULL;
    SDL_snprintf(line, sizeof(line), "%s at 24 points: %dx%d anti-aliased%s%s", font_file, s ? s->w : 0, s ? s->h : 0,
                 font ? "" : ": ", font ? "" : TTF_GetError());
    report("ttf", s && s->w > 100 && s->h > 10, line);
    SDL_FreeSurface(s);
    if (font) {
        TTF_CloseFont(font);
    }
    TTF_Quit();
}

/* TCP: an HTTP HEAD to a web server, the first line of its answer. */
static void check_tcp(const char *host)
{
    char line[256], reply[128];
    IPaddress ip;
    TCPsocket s = NULL;
    int n = 0;
    const char *req = "HEAD / HTTP/1.0\r\nHost: aminet.net\r\n\r\n";

    if (SDLNet_ResolveHost(&ip, host, 80) == 0 && (s = SDLNet_TCP_Open(&ip)) != NULL) {
        SDLNet_TCP_Send(s, req, (int)SDL_strlen(req));
        n = SDLNet_TCP_Recv(s, reply, sizeof(reply) - 1);
    }
    reply[n > 0 ? n : 0] = '\0';
    if (SDL_strchr(reply, '\r')) {
        *SDL_strchr(reply, '\r') = '\0';
    }
    SDL_snprintf(line, sizeof(line), "TCP to %s:80: %s%s", host, n > 0 ? reply : "no answer: ", n > 0 ? "" : SDLNet_GetError());
    report("net", n > 0 && SDL_strncmp(reply, "HTTP/", 5) == 0, line);
    if (s) {
        SDLNet_TCP_Close(s);
    }
}

static void check_net(void)
{
    char line[200];
    UDPsocket sock;
    UDPpacket *out, *in;
    IPaddress self;
    int got = 0, i, sent = 0;

    if (SDLNet_Init() < 0) {
        report("net", 0, SDLNet_GetError());
        return;
    }
    sock = SDLNet_UDP_Open(42420);
    out = SDLNet_AllocPacket(64);
    in = SDLNet_AllocPacket(64);
    if (!sock || !out || !in || SDLNet_ResolveHost(&self, "127.0.0.1", 42420) < 0) {
        SDL_snprintf(line, sizeof(line), "UDP socket: %s", SDLNet_GetError());
        report("net", 0, line);
    } else {
        SDL_strlcpy((char *)out->data, "hello from SDL_net", 64);
        out->len = (int)SDL_strlen((char *)out->data) + 1;
        out->address = self;
        sent = SDLNet_UDP_Send(sock, -1, out);
        for (i = 0; i < 40 && !got; i++) {
            got = SDLNet_UDP_Recv(sock, in);
            if (!got) {
                SDL_Delay(25);
            }
        }
        SDL_snprintf(line, sizeof(line), "UDP to itself on 127.0.0.1: sent %d, %s%s", sent,
                     got > 0 ? (char *)in->data : "nothing came back: ", got > 0 ? "" : SDLNet_GetError());
        report("net", got > 0 && SDL_strcmp((char *)in->data, "hello from SDL_net") == 0, line);
    }
    SDLNet_FreePacket(out);
    SDLNet_FreePacket(in);
    if (sock) {
        SDLNet_UDP_Close(sock);
    }
    check_tcp("aminet.net");
    SDLNet_Quit();
}

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    check_image();
    check_mixer();
    check_ttf(argc > 1 ? argv[1] : NULL);
    check_net();
    printf("satcheck: %s\n", failures ? "some checks failed" : "all checks passed");
    SDL_Quit();
    return failures ? 5 : 0;
}
