/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ALInfo: what OpenAL says on this Amiga, through OpenAL.module
 * (alinfo.m) or OpenAL inside the program (alinfo). With no device it
 * still asks the module what needs none (the ALC version, the device list,
 * the extensions, the enum values, the entry points), so it checks the
 * module's loading and its calls on any 68k, sound or not. With a device it
 * prints the AL strings and the attributes too. Return code 0 when every
 * answer is as OpenAL.module gives it, 5 when not, 10 with no OpenAL at all. */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <stdio.h>
#include <string.h>

static int bad;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("ALInfo: wrong: %s\n", what);
        bad = 1;
    }
}

int main(void)
{
    static const char *const alc_exts[] = { "ALC_ENUMERATION_EXT", "ALC_EXT_disconnect", "ALC_EXT_CAPTURE",
                                            "ALC_EXT_EFX", "ALC_ENUMERATE_ALL_EXT" };
    const ALCchar *def, *list;
    ALCint major = 0, minor = 0;
    ALCdevice *dev;
    size_t i;

    def = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    if (!def) {
        printf("ALInfo: no OpenAL\n");
        return 10;
    }
    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(NULL, ALC_MINOR_VERSION, 1, &minor);
    printf("ALInfo: ALC %ld.%ld, default device \"%s\"\n", (long)major, (long)minor, def);
    expect(major == 1 && minor == 1, "ALC 1.1");
    list = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    for (; list && *list; list += strlen(list) + 1) {
        printf("ALInfo: device \"%s\"\n", list);
    }
    for (i = 0; i < sizeof alc_exts / sizeof alc_exts[0]; i++) {
        printf("ALInfo: %s %s\n", alc_exts[i], alcIsExtensionPresent(NULL, alc_exts[i]) ? "yes" : "no");
    }
    expect(alcIsExtensionPresent(NULL, "ALC_EXT_disconnect") && !alcIsExtensionPresent(NULL, "ALC_EXT_EFX"),
           "the ALC extensions");
    expect(alcGetEnumValue(NULL, "AL_FORMAT_51CHN16") == AL_FORMAT_51CHN16, "alcGetEnumValue");
    expect(alcGetEnumValue(NULL, "ALC_CONNECTED") == ALC_CONNECTED, "alcGetEnumValue (ALC)");
    /* The entry points are the program's own (the stub's), which set A4. */
    expect(alcGetProcAddress(NULL, "alcOpenDevice") == (void *)alcOpenDevice, "alcGetProcAddress");
    expect(alGetProcAddress("alGenSources") == (void *)alGenSources, "alGetProcAddress");
    expect(alGetProcAddress("alGenEffects") == NULL, "no EFX");
    expect(alGetError() == AL_INVALID_OPERATION, "alGetError with no context");
    expect(alcGetString(NULL, ALC_EXTENSIONS) == NULL && alcGetError(NULL) == ALC_INVALID_DEVICE,
           "ALC_EXTENSIONS with no device");

    dev = alcOpenDevice(NULL);
    if (!dev) {
        printf("ALInfo: no device opens (ahi.device?)\n");
    } else {
        ALCcontext *ctx = alcCreateContext(dev, NULL);
        ALCint attrs[16], n = 0, connected = 0;
        if (ctx && alcMakeContextCurrent(ctx)) {
            printf("ALInfo: AL_VERSION \"%s\"\n", alGetString(AL_VERSION));
            printf("ALInfo: AL_RENDERER \"%s\"\n", alGetString(AL_RENDERER));
            printf("ALInfo: AL_VENDOR \"%s\"\n", alGetString(AL_VENDOR));
            printf("ALInfo: AL_EXTENSIONS \"%s\"\n", alGetString(AL_EXTENSIONS));
            printf("ALInfo: ALC_EXTENSIONS \"%s\"\n", alcGetString(dev, ALC_EXTENSIONS));
            alcGetIntegerv(dev, ALC_ATTRIBUTES_SIZE, 1, &n);
            if (n > 0 && n <= 16) {
                alcGetIntegerv(dev, ALC_ALL_ATTRIBUTES, n, attrs);
                for (i = 0; i + 1 < (size_t)n && attrs[i]; i += 2) {
                    printf("ALInfo: attribute 0x%lx = %ld\n", (long)attrs[i], (long)attrs[i + 1]);
                }
            }
            alcGetIntegerv(dev, ALC_CONNECTED, 1, &connected);
            printf("ALInfo: %s\n", connected ? "connected" : "disconnected: no sound");
            expect(alGetFloat(AL_SPEED_OF_SOUND) == 343.3f, "a float back from the module");
            alcMakeContextCurrent(NULL);
        } else {
            printf("ALInfo: no context (0x%lx)\n", (long)alcGetError(dev));
            bad = 1;
        }
        if (ctx) {
            alcDestroyContext(ctx);
        }
        expect(alcCloseDevice(dev) == ALC_TRUE, "alcCloseDevice");
    }
    printf("ALInfo: %s\n", bad ? "something was wrong" : "all as expected");
    return bad ? 5 : 0;
}
