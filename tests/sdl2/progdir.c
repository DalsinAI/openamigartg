/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * progdir: file names mean the same in every process.
 *
 * PROGDIR: is the program's own folder, and a relative name is the current
 * directory's, but only for the process that has them. A name SDL, SDL_image
 * or SDL_mixer hands to another process (an SDL thread, a decoder reading a
 * piece ahead, the cores) used to raise AmigaDOS's "Please insert volume
 * PROGDIR: in any drive". Run it from its own folder (a Shell's CD, or
 * double-click), on SDL2.module, SDL2_image.module and SDL2_mixer.module
 * (progdir.m) or linked statically (progdir):
 *   every file name goes in as PROGDIR:name, as a relative name, and as the
 *   full path; written, read back, decoded, in the program's process and in
 *   an SDL thread;
 *   the thread has the program's home folder and current directory, and no
 *   requester window (a name that is not there fails and says so, it does
 *   not ask for a volume);
 *   a name that isn't there is an error, not a hang.
 * Exit 0 when every check passes. */
#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include <stdio.h>
#include <string.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>

static int failures = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("%-9s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

#define N_NAMES 3
static char name_txt[N_NAMES][300];
static char name_png[N_NAMES][300];
static char name_wav[N_NAMES][300];
static const char *kind[N_NAMES] = {"PROGDIR:", "relative", "full path"};

/* What a thread found; the thread's checks run there. */
static struct {
    int home, cwd, quiet;
    int read_ok[N_NAMES], image_ok[N_NAMES], wav_ok[N_NAMES], mus_ok[N_NAMES];
    int missing_failed;
    char error[160];
} th;

static int read_txt(const char *name)
{
    char buf[16];
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    int ok;
    if (!rw) {
        return 0;
    }
    ok = SDL_RWread(rw, buf, 1, 5) == 5 && memcmp(buf, "probe", 5) == 0;
    SDL_RWclose(rw);
    return ok;
}

static int image_ok(const char *name)
{
    SDL_Surface *s = IMG_Load(name);
    int ok = s && s->w == 8 && s->h == 4;
    if (s) {
        SDL_FreeSurface(s);
    }
    return ok;
}

static int wav_ok(const char *name)
{
    Mix_Chunk *c = Mix_LoadWAV(name);
    int ok = c != NULL;
    if (c) {
        Mix_FreeChunk(c);
    }
    return ok;
}

static int mus_ok(const char *name)
{
    Mix_Music *m = Mix_LoadMUS(name);
    int ok = m != NULL;
    if (m) {
        Mix_FreeMusic(m);
    }
    return ok;
}

static int SDLCALL thread_fn(void *data)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    int i;
    SDL_RWops *rw;
    (void)data;
    th.home = me->pr_HomeDir != 0;
    th.cwd = me->pr_CurrentDir != 0;
    th.quiet = me->pr_WindowPtr == (APTR)-1L;
    for (i = 0; i < N_NAMES; i++) {
        th.read_ok[i] = read_txt(name_txt[i]);
        th.image_ok[i] = image_ok(name_png[i]);
        th.wav_ok[i] = wav_ok(name_wav[i]);
        th.mus_ok[i] = mus_ok(name_wav[i]);
    }
    /* Not there: an error, quickly, with no requester (the thread has none). */
    rw = SDL_RWFromFile("NoSuchVolume:no/such/file.txt", "rb");
    th.missing_failed = rw == NULL;
    if (rw) {
        SDL_RWclose(rw);
    }
    SDL_snprintf(th.error, sizeof(th.error), "%s", SDL_GetError());
    return 0;
}

/* A PROGDIR:-named 8x4 picture and a short WAV, as files. */
static int make_files(void)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 8, 4, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RWops *rw;
    Uint8 wav[44 + 800];
    Uint32 *px;
    int i, ok = 1;
    static const Uint8 head[44] = {
        'R', 'I', 'F', 'F', 0x38, 0x03, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x40, 0x1f, 0, 0, 0x80, 0x3e, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0x20, 0x03, 0, 0};

    if (!s) {
        return 0;
    }
    px = (Uint32 *)s->pixels;
    for (i = 0; i < 8 * 4; i++) {
        px[i] = 0xFF204060u + (Uint32)i;
    }
    ok &= IMG_SavePNG(s, "PROGDIR:progdir-probe.png") == 0;
    SDL_FreeSurface(s);
    memcpy(wav, head, 44);
    for (i = 0; i < 400; i++) {
        Sint16 v = (Sint16)((i & 8) ? 3000 : -3000);
        wav[44 + 2 * i] = (Uint8)(v & 255);
        wav[45 + 2 * i] = (Uint8)((v >> 8) & 255);
    }
    rw = SDL_RWFromFile("PROGDIR:progdir-probe.wav", "wb");
    ok &= rw && SDL_RWwrite(rw, wav, 1, sizeof(wav)) == sizeof(wav);
    if (rw) {
        SDL_RWclose(rw);
    }
    rw = SDL_RWFromFile("PROGDIR:progdir-probe.txt", "wb");
    ok &= rw && SDL_RWwrite(rw, "probe", 1, 5) == 5;
    if (rw) {
        SDL_RWclose(rw);
    }
    return ok;
}

int main(int argc, char **argv)
{
    char full[300], line[400];
    BPTR lock;
    int i, all;
    SDL_Thread *t;

    (void)argc;
    (void)argv;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024) != 0) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        SDL_Quit();
        return 20;
    }

    report("files", make_files(), "PROGDIR:progdir-probe.txt, .png and .wav written with SDL_RWFromFile and IMG_SavePNG");

    /* The program's folder's full path, and a relative name (the current directory is that folder). */
    full[0] = 0;
    lock = Lock("PROGDIR:progdir-probe.txt", SHARED_LOCK);
    if (lock) {
        NameFromLock(lock, full, sizeof(full));
        UnLock(lock);
    }
    report("progdir", full[0] != 0, full[0] ? full : "PROGDIR:progdir-probe.txt can't be locked");
    SDL_snprintf(name_txt[0], sizeof(name_txt[0]), "PROGDIR:progdir-probe.txt");
    SDL_snprintf(name_png[0], sizeof(name_png[0]), "PROGDIR:progdir-probe.png");
    SDL_snprintf(name_wav[0], sizeof(name_wav[0]), "PROGDIR:progdir-probe.wav");
    SDL_snprintf(name_txt[1], sizeof(name_txt[1]), "progdir-probe.txt");
    SDL_snprintf(name_png[1], sizeof(name_png[1]), "progdir-probe.png");
    SDL_snprintf(name_wav[1], sizeof(name_wav[1]), "progdir-probe.wav");
    if (full[0]) {
        size_t n = strlen(full);
        /* full ends in "progdir-probe.txt" (17 characters): without it, the
           folder with its "/" or ":" */
        if (n > 17) {
            full[n - 17] = 0;
        }
        SDL_snprintf(name_txt[2], sizeof(name_txt[2]), "%sprogdir-probe.txt", full);
        SDL_snprintf(name_png[2], sizeof(name_png[2]), "%sprogdir-probe.png", full);
        SDL_snprintf(name_wav[2], sizeof(name_wav[2]), "%sprogdir-probe.wav", full);
    }

    /* In the program's own process. */
    for (i = 0; i < N_NAMES; i++) {
        int a = read_txt(name_txt[i]), b = image_ok(name_png[i]), c = wav_ok(name_wav[i]), d = mus_ok(name_wav[i]);
        SDL_snprintf(line, sizeof(line), "%s: SDL_RWFromFile %s, IMG_Load %s, Mix_LoadWAV %s, Mix_LoadMUS %s",
                     kind[i], a ? "yes" : "NO", b ? "yes" : "NO", c ? "yes" : "NO", d ? "yes" : "NO");
        report("process", a && b && c && d, line);
    }

    /* In an SDL thread: no home folder or directory of its own but its parent's. */
    memset(&th, 0, sizeof(th));
    t = SDL_CreateThread(thread_fn, "progdir", NULL);
    if (!t) {
        report("thread", 0, SDL_GetError());
    } else {
        SDL_WaitThread(t, NULL);
        SDL_snprintf(line, sizeof(line), "home folder %s, current directory %s, requesters %s",
                     th.home ? "kept" : "NONE", th.cwd ? "kept" : "NONE", th.quiet ? "off" : "ON");
        report("thread", th.home && th.cwd && th.quiet, line);
        for (i = 0; i < N_NAMES; i++) {
            all = th.read_ok[i] && th.image_ok[i] && th.wav_ok[i] && th.mus_ok[i];
            SDL_snprintf(line, sizeof(line), "%s: SDL_RWFromFile %s, IMG_Load %s, Mix_LoadWAV %s, Mix_LoadMUS %s",
                         kind[i], th.read_ok[i] ? "yes" : "NO", th.image_ok[i] ? "yes" : "NO",
                         th.wav_ok[i] ? "yes" : "NO", th.mus_ok[i] ? "yes" : "NO");
            report("in thread", all, line);
        }
        SDL_snprintf(line, sizeof(line), "a name that is not there fails (%s)", th.error);
        report("missing", th.missing_failed, line);
    }

    DeleteFile("PROGDIR:progdir-probe.txt");
    DeleteFile("PROGDIR:progdir-probe.png");
    DeleteFile("PROGDIR:progdir-probe.wav");
    Mix_CloseAudio();
    Mix_Quit();
    IMG_Quit();
    SDL_Quit();
    printf("%s\n", failures ? "progdir: FAILED" : "progdir: all passed");
    return failures ? 10 : 0;
}
