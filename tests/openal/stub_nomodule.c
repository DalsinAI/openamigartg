/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libopenal.a (stubs/openal/openal_stub.c) on a PC: the stub's own file,
 * built against OpenAL.module's headers, with exec, dos and opengpu.library
 * answered here (tests/sdl3/host's stand-ins), as tests/sdl3/stub_nomodule.c
 * does for libSDL3.a. Run by tests/openal/run_host.sh.
 *
 * With no OpenAL.module, by either way of loading (opengpu.library's
 * OGPU_ModuleOpen, or the stub's own LoadSeg with no opengpu.library 0.5):
 * the first call says once that the module isn't installed; the program
 * goes on; every call returns 0 (alcOpenDevice NULL: no device);
 * no second try at loading; opengpu.library closed as often as opened.
 * With a module older than the program (its table shorter): the same, and
 * the module closed again. With a module: every call reaches it with its
 * arguments, and the two GetProcAddress give the stub's own entries.
 * Exit 0 when every check passes. */
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

/* The stub never calls exit(): a program without its sound goes on (an
 * exit() here would end the test before its checks). */
#include "openal_stub.c"

/* exec, dos and opengpu.library, as the test sets them. */
struct ExecBase *SysBase;
struct Library *DOSBase;
static struct Library opengpu = { 0, 17 };
static int have_opengpu, opens, closes, module_opens, module_closes, loadsegs;
static struct OpenALModuleTable *module_to_give;
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
    module_opens++;
    if (strcmp(name, "OpenAL") || version != OPENAL_MODULE_OPEN_VERSION || !module_to_give) {
        *table = NULL;
        return NULL;
    }
    *table = module_to_give;
    return (APTR)&module_to_give;
}

void test_ModuleClose(struct Library *base, APTR handle)
{
    (void)base;
    (void)handle;
    module_closes++;
}

/* A module, as the test has it: a few calls of its own. */
static int module_ended, fill_answer;
static ALCdevice *fake_open(const ALCchar *name)
{
    return (ALCdevice *)(name && !strcmp(name, "AHI Mix") ? (void *)&opengpu : NULL);
}
static ALfloat fake_get_float(ALenum param)
{
    return param == AL_SPEED_OF_SOUND ? 343.3f : 0.0f;
}
static ALenum last_param;
static ALfloat last_value;
static void fake_listenerf(ALenum param, ALfloat value)
{
    last_param = param;
    last_value = value;
}
static void *fake_get_proc(const ALchar *name)
{
    return name && !strcmp(name, "alGetFloat") ? (void *)fake_get_float : NULL;
}
static void *fake_alc_get_proc(ALCdevice *device, const ALCchar *name)
{
    (void)device;
    return name && !strcmp(name, "alcOpenDevice") ? (void *)fake_open : NULL;
}
static LONG fake_fill(ULONG version, void *procs, ULONG size)
{
    struct OpenALProcs p;
    if (fill_answer < 0 || version != OPENAL_PROCS_VERSION || size != sizeof p)
        return -1;
    memset(&p, 0, sizeof p);
    p.alcOpenDevice = fake_open;
    p.alGetFloat = fake_get_float;
    p.alListenerf = fake_listenerf;
    p.alGetProcAddress = fake_get_proc;
    p.alcGetProcAddress = fake_alc_get_proc;
    memcpy(procs, &p, size);
    return 0;
}
static void fake_close(void)
{
    module_ended++;
}
static struct OpenALModuleTable fake_module = { { OPENAL_MODULE_VERSION, NULL, NULL }, fake_fill, fake_close };

static int failed;
#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL %s: %s\n", how, what);             \
            failed = 1;                                     \
        }                                                   \
    } while (0)

static const char missing[] =
    "OpenAL: this program's sound needs LIBS:OpenGPU/OpenAL.module (OpenGPU), which isn't installed.\n";
static const char older[] =
    "OpenAL: LIBS:OpenGPU/OpenAL.module is older than this program. Install a newer OpenGPU.\n";

static void reset(int with_opengpu, unsigned revision, struct OpenALModuleTable *module, int fill)
{
    have_opengpu = with_opengpu;
    opengpu.lib_Revision = (UWORD)revision;
    opens = closes = module_opens = module_closes = loadsegs = module_ended = 0;
    module_to_give = module;
    fill_answer = fill;
    out[0] = 0;
    OpenALStub_ready = OpenALStub_failed = 0;
}

static void run_missing(const char *how, int with_opengpu, const char *message)
{
    ALCint v = 99;
    reset(with_opengpu, 17, NULL, 0);
    CHECK(alcOpenDevice(NULL) == NULL, "alcOpenDevice answers NULL: no device");
    CHECK(!strcmp(out, message), "it says the module isn't installed");
    CHECK(alGetError() == AL_NO_ERROR, "alGetError answers 0");
    CHECK(alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER) == NULL, "alcGetString answers NULL");
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 0.0f, "alGetFloat answers 0");
    CHECK(alGetProcAddress("alGetFloat") == NULL, "alGetProcAddress answers NULL");
    CHECK(alcGetProcAddress(NULL, "alcOpenDevice") == NULL, "alcGetProcAddress answers NULL");
    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, &v);
    CHECK(v == 99, "alcGetIntegerv leaves its destination");
    alListenerf(AL_GAIN, 1.0f);
    alcCloseDevice(NULL);
    /* As the program ends: the stub's destructor, then calls after it. */
    OpenALStub_Close();
    CHECK(alcOpenDevice(NULL) == NULL, "a call after the end answers NULL");
    CHECK(!strcmp(out, message), "the message once");
    CHECK(module_opens + loadsegs == 1, "one try at loading the module");
    CHECK(opens == closes, "opengpu.library closed as often as opened");
    if (!failed)
        printf("ok %s\n", how);
}

static void run_older(void)
{
    const char *how = "a module older than the program";
    reset(1, 17, &fake_module, -1);
    CHECK(alcOpenDevice("AHI Mix") == NULL, "alcOpenDevice answers NULL");
    CHECK(!strcmp(out, older), "it says the module is older");
    CHECK(module_ended == 1 && module_closes == 1, "the module closed again");
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 0.0f, "calls answer 0");
    OpenALStub_Close();
    CHECK(module_opens == 1 && opens == closes, "one try, opengpu.library closed");
    if (!failed)
        printf("ok %s\n", how);
}

static void run_present(void)
{
    const char *how = "with the module";
    void *p;
    reset(1, 17, &fake_module, 0);
    CHECK(alcOpenDevice("AHI Mix") == (ALCdevice *)(void *)&opengpu, "alcOpenDevice reaches the module");
    CHECK(alcOpenDevice("other") == NULL, "with its argument");
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 343.3f, "a float comes back");
    alListenerf(AL_GAIN, 0.25f);
    CHECK(last_param == AL_GAIN && last_value == 0.25f, "a float goes in");
    p = alGetProcAddress("alGetFloat");
    CHECK(p == (void *)alGetFloat, "alGetProcAddress gives the stub's entry, not the module's");
    CHECK(((ALfloat(*)(ALenum))p)(AL_SPEED_OF_SOUND) == 343.3f, "and it works");
    CHECK(alGetProcAddress("alGenEffects") == NULL, "a call the module hasn't got: NULL");
    p = alcGetProcAddress(NULL, "alcOpenDevice");
    CHECK(p == (void *)alcOpenDevice, "alcGetProcAddress gives the stub's entry");
    CHECK(out[0] == 0, "nothing said");
    OpenALStub_Close();
    CHECK(module_ended == 1 && module_closes == 1, "the module's close, then OGPU_ModuleClose");
    CHECK(module_opens == 1 && loadsegs == 0 && opens == closes, "one open, opengpu.library closed");
    if (!failed)
        printf("ok %s\n", how);
}

int main(void)
{
    run_missing("opengpu.library 0.17 (OGPU_ModuleOpen), no module", 1, missing);
    run_missing("no opengpu.library (LoadSeg), no module", 0, missing);
    run_older();
    run_present();
    return failed;
}
