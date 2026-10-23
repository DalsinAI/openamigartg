/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libSDL3.a (stubs/sdl3/SDL3_stub.c) with no SDL3.module, on the host: the
 * stub's own file, built against SDL 3's headers, with exec, dos and
 * opengpu.library answered here (tests/sdl3/host).
 *
 * The first SDL call says the module isn't installed, once, and ends the
 * program with exit(20). The SDL calls that come while the program ends
 * (SDL_Quit from atexit or a destructor, as libnix's exit list runs them)
 * must then do nothing: no second load of the module, no second message and
 * no second exit(), which on an Amiga ran libnix's exit list again from
 * inside itself, for ever when the call came from a destructor. Both ways
 * of loading are checked: opengpu.library's OGPU_ModuleOpen, and the stub's
 * own LoadSeg when there is no opengpu.library 0.5.
 *   tests/run.sh with SDL3_SRC=<SDL 3.4.14's tree> (its include/ and src/)
 * Exit 0 when every check passes. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>

/* No A4 on the host (include/opengpu/module.h has these for the 68k). */
#define OGPU_A4(a4) (void)(a4)
static APTR ogpu_a4_get(void)
{
    return NULL;
}

/* The stub's exit(): back to the test, counted. */
static jmp_buf exit_jump;
static int exits, exit_code;
static void test_exit(int code) __attribute__((noreturn));
static void test_exit(int code)
{
    exits++;
    exit_code = code;
    longjmp(exit_jump, 1);
}
#define exit test_exit
#include "SDL3_stub.c"
#undef exit

/* exec, dos and opengpu.library, as the test sets them. */
struct ExecBase *SysBase;
struct Library *DOSBase;
static struct Library opengpu = { 0, 17 };
static int have_opengpu, opens, closes, module_opens, loadsegs;
static char out[1024];

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    (void)version;
    if (strcmp(name, "opengpu.library") || !have_opengpu)
        return NULL;
    opens++;
    return &opengpu;
}

void CloseLibrary(struct Library *lib)
{
    if (lib == &opengpu)
        closes++;
}

BPTR Output(void)
{
    return 1;
}

LONG FPuts(BPTR fh, CONST_STRPTR str)
{
    (void)fh;
    if (strlen(out) + strlen(str) < sizeof(out))
        strcat(out, str);
    return 0;
}

LONG IoErr(void)
{
    return ERROR_OBJECT_NOT_FOUND;
}

BPTR LoadSeg(CONST_STRPTR name)
{
    (void)name;
    loadsegs++;
    return 0;
}

void UnLoadSeg(BPTR seg)
{
    (void)seg;
}

APTR test_ModuleOpen(struct Library *base, CONST_STRPTR name, ULONG version, APTR *table)
{
    (void)base;
    (void)name;
    (void)version;
    module_opens++;
    *table = NULL;
    return NULL;
}

void test_ModuleClose(struct Library *base, APTR handle)
{
    (void)base;
    (void)handle;
}

static int failed;
#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL %s: %s\n", how, what);             \
            failed = 1;                                     \
        }                                                   \
    } while (0)

static const char message[] =
    "SDL 3: this program needs LIBS:OpenGPU/SDL3.module (OpenGPU), which isn't installed.\n";

static void run(const char *how, int with_opengpu)
{
    volatile int phase = 0;

    have_opengpu = with_opengpu;
    opens = closes = module_opens = loadsegs = exits = exit_code = 0;
    out[0] = 0;
    SDL3Stub_ready = SDL3Stub_failed = 0;

    if (setjmp(exit_jump) == 0) {
        SDL_Init(SDL_INIT_VIDEO);
        CHECK(0, "SDL_Init came back with no module");
    }
    CHECK(exits == 1 && exit_code == 20, "the first call ends the program with exit(20)");
    CHECK(!strcmp(out, message), "it says the module isn't installed");

    /* libnix's exit list: atexit(SDL_Quit), then a program's destructor,
       then the stub's own, then a destructor after it. */
    if (setjmp(exit_jump) == 0) {
        phase = 1;
        SDL_Quit();
        phase = 2;
        CHECK(!SDL_Init(SDL_INIT_VIDEO), "SDL_Init answers false");
        SDL_Log("%s", "nothing");
        SDL_SetError("%d", 1);
        CHECK(SDL_GetError() == NULL, "SDL_GetError answers NULL");
        SDL_Quit();
        phase = 3;
        SDL3Stub_Close();
        SDL_Quit();
        phase = 4;
    }
    CHECK(phase == 4, "the SDL calls while the program ends come back");
    CHECK(exits == 1, "exit() once");
    CHECK(!strcmp(out, message), "the message once");
    CHECK(module_opens + loadsegs == 1, "one try at loading the module");
    CHECK(opens == closes, "opengpu.library closed as often as opened");
    if (!failed)
        printf("ok %s\n", how);
}

int main(void)
{
    run("opengpu.library 0.17 (OGPU_ModuleOpen)", 1);
    run("no opengpu.library (LoadSeg)", 0);
    return failed;
}
