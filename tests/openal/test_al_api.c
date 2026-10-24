/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's AL and ALC calls on a PC (tests/openal/run_host.sh): every
 * call's error cases and state queries, from the OpenAL 1.1 specification's
 * sections and tables (named at each group), with the module's choices
 * where the specification leaves one (library/modules/openal/README.md).
 * Exit 0 when every check passes. */
#include "check.h"
#include "host_out.h"

#include <float.h>

static ALCdevice *dev;
static ALCcontext *ctx;

/* ---- ALC with no device (6.3) ----------------------------------------------------- */

static void test_alc_no_device(void)
{
    const ALCchar *s;
    ALCint v;
    ALCdevice *d;

    /* 6.3.7: the default device, and the list (ALC_ENUMERATION_EXT, 9.5).
     * No board here (host_voices_fitted is 0): Mix is the default. */
    s = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Mix"));
    s = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Voices") && !strcmp(s + 11, "AHI Mix") && s[19] == 0);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    /* "Specifying NULL for deviceHandle when asking for ALC_EXTENSIONS will
     * generate an ALC_INVALID_DEVICE error." */
    CHECK(alcGetString(NULL, ALC_EXTENSIONS) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    CHECK(alcGetString(NULL, 0x7777) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_ENUM);
    s = alcGetString(NULL, ALC_INVALID_CONTEXT);
    CHECK(s && !strcmp(s, "ALC_INVALID_CONTEXT"));
    s = alcGetString(NULL, ALC_NO_ERROR);
    CHECK(s && !strcmp(s, "ALC_NO_ERROR"));

    /* 6.3.3: case does not matter; NULL is ALC_INVALID_VALUE. */
    CHECK(alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == ALC_TRUE);
    CHECK(alcIsExtensionPresent(NULL, "alc_enumeration_ext") == ALC_TRUE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_EXT_disconnect") == ALC_TRUE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_EXT_CAPTURE") == ALC_TRUE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_EXT_EFX") == ALC_FALSE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == ALC_FALSE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_SOFT_HRTF") == ALC_FALSE);
    CHECK(alcIsExtensionPresent(NULL, "ALC_ENUMERATION") == ALC_FALSE);
    CHECK(alcIsExtensionPresent(NULL, "") == ALC_FALSE);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    CHECK(alcIsExtensionPresent(NULL, NULL) == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);

    /* 6.3.8: the versions with a NULL device; the others need a device. */
    v = 0;
    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, &v);
    CHECK(v == 1);
    v = 0;
    alcGetIntegerv(NULL, ALC_MINOR_VERSION, 1, &v);
    CHECK(v == 1);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    v = 1234;
    alcGetIntegerv(NULL, ALC_ATTRIBUTES_SIZE, 1, &v);
    CHECK(v == 1234);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    alcGetIntegerv(NULL, ALC_CAPTURE_SAMPLES, 1, &v);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    /* "A NULL destination or a zero size parameter will cause ALC to ignore
     * the query." */
    v = 1234;
    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 0, &v);
    CHECK(v == 1234);
    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, NULL);
    CHECK_ALC(NULL, ALC_NO_ERROR);

    /* 6.3.4, 6.3.5. */
    CHECK(alcGetProcAddress(NULL, "alcOpenDevice") == (void *)alcOpenDevice);
    CHECK(alcGetProcAddress(NULL, "alBufferData") == (void *)alBufferData);
    CHECK(alcGetProcAddress(NULL, "alGenEffects") == NULL);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    CHECK(alcGetProcAddress(NULL, NULL) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    CHECK(alcGetEnumValue(NULL, "ALC_FREQUENCY") == 0x1007);
    CHECK(alcGetEnumValue(NULL, "ALC_CONNECTED") == 0x313);
    CHECK(alcGetEnumValue(NULL, "AL_FORMAT_51CHN16") == 0x120B);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    CHECK(alcGetEnumValue(NULL, NULL) == 0);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    CHECK(alcGetEnumValue(NULL, "ALC_NOT_A_TOKEN") == 0);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);

    /* 2.7 and 6.3.6: the first error stays until it is read. */
    alcGetString(NULL, 0x7777);
    alcIsExtensionPresent(NULL, NULL);
    CHECK_ALC(NULL, ALC_INVALID_ENUM);
    CHECK_ALC(NULL, ALC_NO_ERROR);

    /* 6.1.1: an unknown device, or no output: NULL. */
    CHECK(alcOpenDevice("DirectSound3D") == NULL);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    host_out_fail_open = 1;
    CHECK(alcOpenDevice(NULL) == NULL);
    host_out_fail_open = 0;
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    d = alcOpenDevice("AHI Mix");
    CHECK(d != NULL);
    CHECK(alcCloseDevice(d) == ALC_TRUE);
    /* A closed device is no longer valid (6.1.2). */
    CHECK_ALC(d, ALC_INVALID_DEVICE);
    CHECK(alcCloseDevice(d) == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    CHECK(alcCloseDevice(NULL) == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);

    /* 6.4.2: there is no capture device. */
    CHECK(alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER) == NULL);
    s = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
    CHECK(s && s[0] == 0 && s[1] == 0);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    CHECK(alcCaptureOpenDevice(NULL, 22050, AL_FORMAT_MONO16, 1024) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_VALUE);
    CHECK(alcCaptureCloseDevice(NULL) == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);

    /* Contexts: none to make current, process or destroy. */
    CHECK(alcGetCurrentContext() == NULL);
    CHECK(alcMakeContextCurrent(NULL) == ALC_TRUE);
    CHECK(alcMakeContextCurrent((ALCcontext *)&v) == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);
    alcProcessContext(NULL);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);
    alcSuspendContext((ALCcontext *)&v);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);
    alcDestroyContext(NULL);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);
    CHECK(alcGetContextsDevice((ALCcontext *)&v) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);
    CHECK(alcCreateContext(NULL, NULL) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    CHECK(alcCreateContext((ALCdevice *)&v, NULL) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    CHECK(alcGetError((ALCdevice *)&v) == ALC_INVALID_DEVICE);

    /* AL with no context: alGetError says so, and every call does nothing. */
    CHECK(alGetError() == AL_INVALID_OPERATION);
    {
        ALuint s1 = 77;
        alGenSources(1, &s1);
        CHECK(s1 == 77);
        CHECK(alIsSource(1) == AL_FALSE);
        CHECK(alIsBuffer(0) == AL_FALSE);
    }
}

/* ---- Devices and contexts (6.1, 6.2) --------------------------------------------------- */

static void test_alc_device(void)
{
    ALCint attrs[16], v, all[16];
    ALCcontext *c2, *c3;
    const ALCchar *s;

    dev = alcOpenDevice(NULL);
    CHECK(dev != NULL);
    s = alcGetString(dev, ALC_DEVICE_SPECIFIER);
    CHECK(s && !strcmp(s, "AHI Mix"));
    s = alcGetString(dev, ALC_EXTENSIONS);
    CHECK(s && strstr(s, "ALC_ENUMERATION_EXT") && strstr(s, "ALC_EXT_disconnect"));
    CHECK(alcIsExtensionPresent(dev, "ALC_EXT_DISCONNECT") == ALC_TRUE);
    CHECK(alcIsExtensionPresent((ALCdevice *)&v, "ALC_EXT_disconnect") == ALC_FALSE);
    CHECK_ALC(NULL, ALC_INVALID_DEVICE);
    /* A playback device is no capture device. */
    CHECK(alcGetString(dev, ALC_CAPTURE_DEVICE_SPECIFIER) == NULL);
    CHECK_ALC(dev, ALC_INVALID_DEVICE);
    alcCaptureStart(dev);
    CHECK_ALC(dev, ALC_INVALID_DEVICE);
    alcGetIntegerv(dev, ALC_CAPTURE_SAMPLES, 1, &v);
    CHECK_ALC(dev, ALC_INVALID_DEVICE);
    alcGetIntegerv(dev, 0x7777, 1, &v);
    CHECK_ALC(dev, ALC_INVALID_ENUM);

    /* 6.2.2: what can't be provided fails, with ALC_INVALID_VALUE. Tokens
     * the module doesn't know (here EFX's ALC_MAX_AUXILIARY_SENDS) are
     * passed over. */
    attrs[0] = ALC_FREQUENCY; attrs[1] = 100; attrs[2] = 0;
    CHECK(alcCreateContext(dev, attrs) == NULL);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    attrs[0] = ALC_REFRESH; attrs[1] = 0; attrs[2] = 0;
    CHECK(alcCreateContext(dev, attrs) == NULL);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    attrs[0] = ALC_SYNC; attrs[1] = ALC_TRUE; attrs[2] = 0;
    CHECK(alcCreateContext(dev, attrs) == NULL);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    attrs[0] = ALC_MONO_SOURCES; attrs[1] = -1; attrs[2] = 0;
    CHECK(alcCreateContext(dev, attrs) == NULL);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    host_out_fail_start = 1;
    CHECK(alcCreateContext(dev, NULL) == NULL);
    host_out_fail_start = 0;
    CHECK_ALC(dev, ALC_OUT_OF_MEMORY);

    attrs[0] = ALC_FREQUENCY; attrs[1] = 22050;
    attrs[2] = 0x20003; attrs[3] = 4;           /* ALC_MAX_AUXILIARY_SENDS */
    attrs[4] = ALC_REFRESH; attrs[5] = 50;
    attrs[6] = ALC_SYNC; attrs[7] = ALC_FALSE;
    attrs[8] = 0;
    ctx = alcCreateContext(dev, attrs);
    CHECK(ctx != NULL);
    CHECK_ALC(dev, ALC_NO_ERROR);
    CHECK(alcGetContextsDevice(ctx) == dev);
    CHECK(dev->frequency == 22050 && dev->update_frames == 441);

    /* A second context at another rate can't be given; at the same one it can. */
    attrs[0] = ALC_FREQUENCY; attrs[1] = 44100; attrs[2] = 0;
    CHECK(alcCreateContext(dev, attrs) == NULL);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    attrs[0] = ALC_FREQUENCY; attrs[1] = 22050; attrs[2] = 0;
    c2 = alcCreateContext(dev, attrs);
    CHECK(c2 != NULL);
    c3 = alcCreateContext(dev, NULL);
    CHECK(c3 != NULL);

    CHECK(alcMakeContextCurrent(ctx) == ALC_TRUE);
    CHECK(alcGetCurrentContext() == ctx);

    /* Table 6-4. */
    v = 0;
    alcGetIntegerv(dev, ALC_ATTRIBUTES_SIZE, 1, &v);
    CHECK(v == 11);
    memset(all, 0x55, sizeof all);
    alcGetIntegerv(dev, ALC_ALL_ATTRIBUTES, 5, all);
    CHECK(all[0] == 0x55555555);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    alcGetIntegerv(dev, ALC_ALL_ATTRIBUTES, 16, all);
    CHECK(all[0] == ALC_FREQUENCY && all[1] == 22050);
    CHECK(all[2] == ALC_REFRESH && all[3] == 50);
    CHECK(all[4] == ALC_SYNC && all[5] == ALC_FALSE);
    CHECK(all[6] == ALC_MONO_SOURCES && all[7] == 256);
    CHECK(all[8] == ALC_STEREO_SOURCES && all[9] == 16);
    CHECK(all[10] == 0 && all[11] == 0x55555555);
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &v);
    CHECK(v == 22050);
    alcGetIntegerv(dev, ALC_MONO_SOURCES, 1, &v);
    CHECK(v == 256);
    alcGetIntegerv(dev, ALC_CONNECTED, 1, &v);
    CHECK(v == ALC_TRUE);
    CHECK_ALC(dev, ALC_NO_ERROR);

    /* 6.2.6: a current context can't be destroyed (ALC_INVALID_VALUE: ALC
     * has no ALC_INVALID_OPERATION). */
    alcDestroyContext(ctx);
    CHECK_ALC(dev, ALC_INVALID_VALUE);
    CHECK(alcGetContextsDevice(ctx) == dev);
    alcDestroyContext(c2);
    alcDestroyContext(c3);
    CHECK_ALC(NULL, ALC_NO_ERROR);
    CHECK(alcGetContextsDevice(c2) == NULL);
    CHECK_ALC(NULL, ALC_INVALID_CONTEXT);

    /* 6.1.2: no closing while a context is there. */
    CHECK(alcCloseDevice(dev) == ALC_FALSE);
    CHECK(alcGetContextsDevice(ctx) == dev);

    /* Suspend and process: repeated calls are legal (6.2.4, 6.2.5). */
    alcSuspendContext(ctx);
    alcSuspendContext(ctx);
    CHECK(ctx->suspended);
    alcProcessContext(ctx);
    alcProcessContext(ctx);
    CHECK(!ctx->suspended);
    CHECK_ALC(dev, ALC_NO_ERROR);
}

/* ---- AL's own state (2.7, 2.8, 3, 7) --------------------------------------------------- */

static void test_al_state(void)
{
    const ALchar *s;
    ALint i;
    ALfloat f;
    ALdouble d;
    ALboolean b;

    CHECK_AL(AL_NO_ERROR);
    s = alGetString(AL_VERSION);
    CHECK(s && !strncmp(s, "1.1 ", 4));
    CHECK(alGetString(AL_VENDOR) != NULL);
    s = alGetString(AL_RENDERER);
    CHECK(s && !strcmp(s, "AHI Mix"));
    s = alGetString(AL_EXTENSIONS);
    CHECK(s && strstr(s, "AL_EXT_FLOAT32") && strstr(s, "AL_EXT_MCFORMATS") && strstr(s, "AL_EXT_OFFSET"));
    s = alGetString(AL_INVALID_OPERATION);
    CHECK(s && !strcmp(s, "AL_INVALID_OPERATION"));
    s = alGetString(AL_NO_ERROR);
    CHECK(s && !strcmp(s, "AL_NO_ERROR"));
    CHECK_AL(AL_NO_ERROR);
    CHECK(alGetString(0x7777) == NULL);
    CHECK_AL(AL_INVALID_ENUM);

    /* 7.1: case doesn't matter; EFX and the rest are not there. */
    CHECK(alIsExtensionPresent("AL_EXT_FLOAT32") == AL_TRUE);
    CHECK(alIsExtensionPresent("al_ext_float32") == AL_TRUE);
    CHECK(alIsExtensionPresent("AL_EXT_MCFORMATS") == AL_TRUE);
    CHECK(alIsExtensionPresent("AL_EXT_LINEAR_DISTANCE") == AL_TRUE);
    CHECK(alIsExtensionPresent("AL_EXT_EXPONENT_DISTANCE") == AL_TRUE);
    CHECK(alIsExtensionPresent("AL_EXT_OFFSET") == AL_TRUE);
    CHECK(alIsExtensionPresent("ALC_EXT_EFX") == AL_FALSE);
    CHECK(alIsExtensionPresent("AL_SOFT_source_spatialize") == AL_FALSE);
    CHECK(alIsExtensionPresent("AL_EXT_IMA4") == AL_FALSE);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alIsExtensionPresent(NULL) == AL_FALSE);
    CHECK_AL(AL_INVALID_VALUE);

    CHECK(alGetEnumValue("AL_FORMAT_MONO_FLOAT32") == 0x10010);
    CHECK(alGetEnumValue("AL_INVERSE_DISTANCE_CLAMPED") == 0xD002);
    CHECK(alGetEnumValue("AL_SEC_OFFSET") == 0x1024);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alGetEnumValue("AL_EFFECT_TYPE") == 0);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(alGetEnumValue(NULL) == 0);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(alGetProcAddress("alBufferData") == (void *)alBufferData);
    CHECK(alGetProcAddress("alcCloseDevice") == (void *)alcCloseDevice);
    CHECK(alGetProcAddress("alGenEffects") == NULL);
    CHECK(alGetProcAddress("alBufferDataStatic") == NULL);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alGetProcAddress(NULL) == NULL);
    CHECK_AL(AL_INVALID_VALUE);

    /* 2.7: the first error stays. */
    alGetString(0x7777);
    alIsExtensionPresent(NULL);
    CHECK_AL(AL_INVALID_ENUM);
    CHECK_AL(AL_NO_ERROR);

    /* 2.8: OpenAL 1.1 has no capability. */
    alEnable(AL_SOURCE_RELATIVE);
    CHECK_AL(AL_INVALID_ENUM);
    alDisable(0);
    CHECK_AL(AL_INVALID_ENUM);
    CHECK(alIsEnabled(AL_LOOPING) == AL_FALSE);
    CHECK_AL(AL_INVALID_ENUM);

    /* 3.1.1: the defaults (3.4, 3.5.2). */
    CHECK(alGetFloat(AL_DOPPLER_FACTOR) == 1.0f);
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 343.3f);
    CHECK(alGetFloat(AL_DOPPLER_VELOCITY) == 1.0f);
    CHECK(alGetInteger(AL_DISTANCE_MODEL) == AL_INVERSE_DISTANCE_CLAMPED);
    CHECK(alGetBoolean(AL_DOPPLER_FACTOR) == AL_TRUE);
    CHECK_NEAR(alGetDouble(AL_SPEED_OF_SOUND), 343.3, 1e-6);
    i = 0;
    alGetIntegerv(AL_DISTANCE_MODEL, &i);
    CHECK(i == AL_INVERSE_DISTANCE_CLAMPED);
    f = 0;
    alGetFloatv(AL_DOPPLER_FACTOR, &f);
    CHECK(f == 1.0f);
    d = 0;
    alGetDoublev(AL_DOPPLER_FACTOR, &d);
    CHECK(d == 1.0);
    b = AL_FALSE;
    alGetBooleanv(AL_SPEED_OF_SOUND, &b);
    CHECK(b == AL_TRUE);
    CHECK_AL(AL_NO_ERROR);
    /* "NULL destinations are quietly ignored." */
    alGetIntegerv(AL_DISTANCE_MODEL, NULL);
    alGetFloatv(0x7777, NULL);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alGetInteger(AL_GAIN) == 0);
    CHECK_AL(AL_INVALID_ENUM);
    i = 99;
    alGetIntegerv(AL_POSITION, &i);
    CHECK(i == 99);
    CHECK_AL(AL_INVALID_ENUM);
    CHECK(alGetFloat(0x7777) == 0.0f);
    CHECK_AL(AL_INVALID_ENUM);

    /* 3.5.2: a negative Doppler factor, a speed of sound not above zero. */
    alDopplerFactor(-1.0f);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(alGetFloat(AL_DOPPLER_FACTOR) == 1.0f);
    alDopplerFactor(0.0f);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alGetFloat(AL_DOPPLER_FACTOR) == 0.0f);
    alDopplerFactor(1.0f);
    alSpeedOfSound(0.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSpeedOfSound(-5.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSpeedOfSound(NAN);
    CHECK_AL(AL_INVALID_VALUE);
    alSpeedOfSound(INFINITY);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 343.3f);
    alSpeedOfSound(100.0f);
    CHECK(alGetFloat(AL_SPEED_OF_SOUND) == 100.0f);
    alSpeedOfSound(343.3f);
    alDopplerVelocity(0.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alDopplerVelocity(2.0f);
    CHECK(alGetFloat(AL_DOPPLER_VELOCITY) == 2.0f);
    alDopplerVelocity(1.0f);
    CHECK_AL(AL_NO_ERROR);

    /* 3.4: the seven legal models; anything else, AL_INVALID_ENUM (2.7). */
    {
        static const ALenum models[] = { AL_NONE, AL_INVERSE_DISTANCE, AL_INVERSE_DISTANCE_CLAMPED,
                                         AL_LINEAR_DISTANCE, AL_LINEAR_DISTANCE_CLAMPED,
                                         AL_EXPONENT_DISTANCE, AL_EXPONENT_DISTANCE_CLAMPED };
        size_t k;
        for (k = 0; k < sizeof models / sizeof models[0]; k++) {
            alDistanceModel(models[k]);
            CHECK(alGetInteger(AL_DISTANCE_MODEL) == models[k]);
        }
        CHECK_AL(AL_NO_ERROR);
        alDistanceModel(AL_LINEAR_DISTANCE + 0x100);
        CHECK_AL(AL_INVALID_ENUM);
        CHECK(alGetInteger(AL_DISTANCE_MODEL) == AL_EXPONENT_DISTANCE_CLAMPED);
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    }
}

/* ---- The listener (4.2) ------------------------------------------------------------- */

static void test_listener(void)
{
    ALfloat f[6], x, y, z;
    ALint iv[6], a, b2, c2;
    ALfloat orient[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    ALint iorient[6] = { 0, 0, -2, 0, 3, 0 };

    /* Defaults (4.1, 4.2.1). */
    alGetListenerfv(AL_ORIENTATION, f);
    CHECK(f[0] == 0 && f[1] == 0 && f[2] == -1 && f[3] == 0 && f[4] == 1 && f[5] == 0);
    alGetListener3f(AL_POSITION, &x, &y, &z);
    CHECK(x == 0 && y == 0 && z == 0);
    alGetListener3f(AL_VELOCITY, &x, &y, &z);
    CHECK(x == 0 && y == 0 && z == 0);
    alGetListenerf(AL_GAIN, &x);
    CHECK(x == 1.0f);
    CHECK_AL(AL_NO_ERROR);

    /* Each form its signature has. */
    alListenerf(AL_GAIN, 0.5f);
    alGetListenerf(AL_GAIN, &x);
    CHECK(x == 0.5f);
    f[0] = 2.0f;
    alListenerfv(AL_GAIN, f);
    alGetListenerfv(AL_GAIN, &x);
    CHECK(x == 2.0f);
    alListenerf(AL_GAIN, 1.0f);
    alListener3f(AL_POSITION, 1.0f, 2.0f, 3.0f);
    alGetListener3f(AL_POSITION, &x, &y, &z);
    CHECK(x == 1.0f && y == 2.0f && z == 3.0f);
    alListener3i(AL_VELOCITY, 4, 5, 6);
    alGetListener3i(AL_VELOCITY, &a, &b2, &c2);
    CHECK(a == 4 && b2 == 5 && c2 == 6);
    iv[0] = 7; iv[1] = 8; iv[2] = 9;
    alListeneriv(AL_POSITION, iv);
    alGetListeneriv(AL_POSITION, iv);
    CHECK(iv[0] == 7 && iv[1] == 8 && iv[2] == 9);
    alListenerfv(AL_ORIENTATION, orient);
    alGetListenerfv(AL_ORIENTATION, f);
    CHECK(!memcmp(f, orient, sizeof orient));
    alListeneriv(AL_ORIENTATION, iorient);
    alGetListeneriv(AL_ORIENTATION, iv);
    CHECK(!memcmp(iv, iorient, sizeof iorient));
    CHECK_AL(AL_NO_ERROR);

    /* Forms the signature lacks: AL_INVALID_ENUM. */
    alListeneri(AL_GAIN, 1);
    CHECK_AL(AL_INVALID_ENUM);
    alListenerf(AL_POSITION, 1.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alListener3f(AL_ORIENTATION, 1.0f, 0.0f, 0.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alGetListeneri(AL_GAIN, &a);
    CHECK_AL(AL_INVALID_ENUM);
    alGetListener3f(AL_ORIENTATION, &x, &y, &z);
    CHECK_AL(AL_INVALID_ENUM);
    /* A source's token to the listener (2.15), and no token at all (2.7). */
    alListenerf(AL_PITCH, 1.0f);
    CHECK_AL(AL_INVALID_OPERATION);
    alListeneri(AL_LOOPING, 1);
    CHECK_AL(AL_INVALID_OPERATION);
    alListenerf(0x7777, 1.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alGetListenerf(AL_PITCH, &x);
    CHECK_AL(AL_INVALID_ENUM);
    /* Values (2.15, 4.1): NULL, a negative gain, not finite. */
    alListenerfv(AL_POSITION, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alListeneriv(AL_ORIENTATION, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alListenerf(AL_GAIN, -0.5f);
    CHECK_AL(AL_INVALID_VALUE);
    alListener3f(AL_POSITION, NAN, 0.0f, 0.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alListener3f(AL_VELOCITY, 0.0f, INFINITY, 0.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alGetListener3f(AL_POSITION, &x, &y, &z);
    CHECK(x == 7.0f && y == 8.0f && z == 9.0f);
    alGetListenerf(AL_GAIN, &x);
    CHECK(x == 1.0f);
    /* 2.16: a NULL destination is quietly ignored. */
    alGetListenerf(AL_GAIN, NULL);
    alGetListener3f(AL_POSITION, &x, NULL, &z);
    alGetListeneriv(0x7777, NULL);
    CHECK_AL(AL_NO_ERROR);

    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    orient[0] = 0.0f; orient[2] = -1.0f; orient[4] = 1.0f; orient[5] = 0.0f;
    alListenerfv(AL_ORIENTATION, orient);
    CHECK_AL(AL_NO_ERROR);
}

/* ---- Buffers (5) --------------------------------------------------------------------- */

static ALint bufi(ALuint b, ALenum p)
{
    ALint v = -12345;
    alGetBufferi(b, p, &v);
    return v;
}

static void test_buffers(void)
{
    ALuint b[3], none = 0, bad = 4242;
    ALshort pcm[600];
    ALubyte u8[100];
    float fl[100];
    ALint v;
    ALfloat f;
    memset(pcm, 0, sizeof pcm);
    memset(u8, 128, sizeof u8);
    memset(fl, 0, sizeof fl);

    /* 2.12. */
    alGenBuffers(-1, b);
    CHECK_AL(AL_INVALID_VALUE);
    alGenBuffers(1, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alGenBuffers(0, NULL);
    CHECK_AL(AL_NO_ERROR);
    alGenBuffers(3, b);
    CHECK_AL(AL_NO_ERROR);
    CHECK(b[0] && b[1] && b[2] && b[0] != b[1] && b[1] != b[2]);
    CHECK(alIsBuffer(b[0]) == AL_TRUE && alIsBuffer(0) == AL_TRUE && alIsBuffer(bad) == AL_FALSE);

    /* Tables 5-1 to 5-4: the defaults. */
    CHECK(bufi(b[0], AL_FREQUENCY) == 0);
    CHECK(bufi(b[0], AL_BITS) == 16);
    CHECK(bufi(b[0], AL_CHANNELS) == 1);
    CHECK(bufi(b[0], AL_SIZE) == 0);
    CHECK_AL(AL_NO_ERROR);

    /* 5.3.4. */
    alBufferData(bad, AL_FORMAT_MONO16, pcm, 100, 22050);
    CHECK_AL(AL_INVALID_NAME);
    alBufferData(0, AL_FORMAT_MONO16, pcm, 100, 22050);
    CHECK_AL(AL_INVALID_NAME);
    alBufferData(b[0], 0x7777, pcm, 100, 22050);
    CHECK_AL(AL_INVALID_ENUM);
    alBufferData(b[0], 0x1300, pcm, 100, 22050);    /* AL_EXT_IMA4's, which the module hasn't got */
    CHECK_AL(AL_INVALID_ENUM);
    alBufferData(b[0], AL_FORMAT_MONO16, pcm, 101, 22050);   /* "an odd value for 16-bit data" */
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_STEREO16, pcm, 102, 22050);
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_MONO_FLOAT32, fl, 6, 22050);
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_51CHN16, pcm, 18, 22050);
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_MONO16, pcm, 100, 0);       /* AL_FREQUENCY (0, any] */
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_MONO16, pcm, -2, 22050);
    CHECK_AL(AL_INVALID_VALUE);
    alBufferData(b[0], AL_FORMAT_MONO16, NULL, 100, 22050);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(bufi(b[0], AL_SIZE) == 0 && bufi(b[0], AL_FREQUENCY) == 0);
    /* Table 5-2: a size of 0 is a legal NOP. */
    alBufferData(b[0], AL_FORMAT_STEREO8, NULL, 0, 11025);
    CHECK_AL(AL_NO_ERROR);
    CHECK(bufi(b[0], AL_FREQUENCY) == 0 && bufi(b[0], AL_BITS) == 16);

    /* The stored form (5.3.3). */
    alBufferData(b[0], AL_FORMAT_MONO16, pcm, 1000, 22050);
    CHECK_AL(AL_NO_ERROR);
    CHECK(bufi(b[0], AL_FREQUENCY) == 22050 && bufi(b[0], AL_BITS) == 16);
    CHECK(bufi(b[0], AL_CHANNELS) == 1 && bufi(b[0], AL_SIZE) == 1000);
    alBufferData(b[1], AL_FORMAT_STEREO8, u8, 100, 8000);
    CHECK(bufi(b[1], AL_BITS) == 8 && bufi(b[1], AL_CHANNELS) == 2 && bufi(b[1], AL_SIZE) == 100);
    alBufferData(b[1], AL_FORMAT_MONO_FLOAT32, fl, 400, 48000);
    CHECK(bufi(b[1], AL_BITS) == 16 && bufi(b[1], AL_CHANNELS) == 1 && bufi(b[1], AL_SIZE) == 200);
    alBufferData(b[1], AL_FORMAT_STEREO_FLOAT32, fl, 400, 48000);
    CHECK(bufi(b[1], AL_BITS) == 16 && bufi(b[1], AL_CHANNELS) == 2 && bufi(b[1], AL_SIZE) == 200);
    alBufferData(b[2], AL_FORMAT_51CHN16, pcm, 144, 44100);
    CHECK(bufi(b[2], AL_BITS) == 16 && bufi(b[2], AL_CHANNELS) == 2 && bufi(b[2], AL_SIZE) == 48);
    alBufferData(b[2], AL_FORMAT_QUAD8, u8, 100, 44100);
    CHECK(bufi(b[2], AL_BITS) == 16 && bufi(b[2], AL_CHANNELS) == 2 && bufi(b[2], AL_SIZE) == 100);
    alBufferData(b[2], AL_FORMAT_71CHN32, fl, 384, 44100);
    CHECK(bufi(b[2], AL_SIZE) == 48 && bufi(b[2], AL_FREQUENCY) == 44100);
    alBufferData(b[2], AL_FORMAT_REAR16, pcm, 400, 44100);
    CHECK(bufi(b[2], AL_SIZE) == 400 && bufi(b[2], AL_CHANNELS) == 2);
    alBufferData(b[2], AL_FORMAT_REAR8, u8, 100, 44100);           /* two channels, but folded: 16 bit */
    CHECK(bufi(b[2], AL_SIZE) == 200 && bufi(b[2], AL_BITS) == 16 && bufi(b[2], AL_CHANNELS) == 2);
    alBufferData(b[2], AL_FORMAT_61CHN8, u8, 98, 44100);
    CHECK(bufi(b[2], AL_SIZE) == 56);
    CHECK_AL(AL_NO_ERROR);

    /* 5.3.1: its own errors for queries. */
    v = 77;
    alGetBufferi(bad, AL_SIZE, &v);
    CHECK(v == 77);
    CHECK_AL(AL_INVALID_OPERATION);
    alGetBufferi(b[0], AL_PITCH, &v);
    CHECK_AL(AL_INVALID_VALUE);
    alGetBufferi(b[0], 0x7777, &v);
    CHECK_AL(AL_INVALID_VALUE);
    alGetBufferf(b[0], AL_FREQUENCY, &f);
    CHECK_AL(AL_INVALID_VALUE);
    alGetBufferiv(b[0], AL_CHANNELS, &v);
    CHECK(v == 1);
    alGetBufferi(b[0], AL_SIZE, NULL);
    alGetBufferfv(bad, AL_SIZE, NULL);
    CHECK_AL(AL_NO_ERROR);
    /* 5.3.2: the core has no attribute to set. */
    alBufferi(b[0], AL_FREQUENCY, 8000);
    CHECK_AL(AL_INVALID_ENUM);
    alBufferi(b[0], AL_PITCH, 1);
    CHECK_AL(AL_INVALID_OPERATION);
    alBufferf(b[0], 0x7777, 1.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alBuffer3i(bad, AL_SIZE, 1, 2, 3);
    CHECK_AL(AL_INVALID_NAME);
    CHECK(bufi(b[0], AL_FREQUENCY) == 22050);

    /* 2.13, 5.2.2. */
    alDeleteBuffers(-1, b);
    CHECK_AL(AL_INVALID_VALUE);
    alDeleteBuffers(1, &none);
    CHECK_AL(AL_NO_ERROR);
    {
        ALuint mixed[2];
        mixed[0] = b[2];
        mixed[1] = bad;
        alDeleteBuffers(2, mixed);
        CHECK_AL(AL_INVALID_NAME);
        CHECK(alIsBuffer(b[2]) == AL_TRUE);
        mixed[1] = b[2];            /* the same name twice */
        alDeleteBuffers(2, mixed);
        CHECK_AL(AL_NO_ERROR);
        CHECK(alIsBuffer(b[2]) == AL_FALSE);
        alDeleteBuffers(1, &b[2]);
        CHECK_AL(AL_INVALID_NAME);
    }
    alDeleteBuffers(2, b);
    CHECK_AL(AL_NO_ERROR);
    CHECK(alIsBuffer(b[0]) == AL_FALSE && alIsBuffer(b[1]) == AL_FALSE);
}

/* ---- Sources (4.3) -------------------------------------------------------------------- */

static ALint srci(ALuint s, ALenum p)
{
    ALint v = -12345;
    alGetSourcei(s, p, &v);
    return v;
}

static ALfloat srcf(ALuint s, ALenum p)
{
    ALfloat v = -12345.0f;
    alGetSourcef(s, p, &v);
    return v;
}

static void test_source_attributes(void)
{
    ALuint s[2], bad = 4343;
    ALfloat x, y, z, fv[3];
    ALint a, b2, c2, iv[3];

    /* 2.12. */
    alGenSources(-1, s);
    CHECK_AL(AL_INVALID_VALUE);
    alGenSources(1, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alGenSources(0, NULL);
    CHECK_AL(AL_NO_ERROR);
    alGenSources(2, s);
    CHECK_AL(AL_NO_ERROR);
    CHECK(s[0] && s[1] && s[0] != s[1]);
    CHECK(alIsSource(s[0]) && alIsSource(s[1]) && !alIsSource(0) && !alIsSource(bad));

    /* The defaults of 4.1 and 4.3.2's tables. */
    CHECK(srcf(s[0], AL_PITCH) == 1.0f);
    CHECK(srcf(s[0], AL_GAIN) == 1.0f);
    CHECK(srcf(s[0], AL_MIN_GAIN) == 0.0f);
    CHECK(srcf(s[0], AL_MAX_GAIN) == 1.0f);
    CHECK(srcf(s[0], AL_REFERENCE_DISTANCE) == 1.0f);
    CHECK(srcf(s[0], AL_ROLLOFF_FACTOR) == 1.0f);
    CHECK(srcf(s[0], AL_MAX_DISTANCE) == FLT_MAX);
    CHECK(srcf(s[0], AL_CONE_INNER_ANGLE) == 360.0f);
    CHECK(srcf(s[0], AL_CONE_OUTER_ANGLE) == 360.0f);
    CHECK(srcf(s[0], AL_CONE_OUTER_GAIN) == 0.0f);
    alGetSource3f(s[0], AL_POSITION, &x, &y, &z);
    CHECK(x == 0 && y == 0 && z == 0);
    alGetSource3f(s[0], AL_VELOCITY, &x, &y, &z);
    CHECK(x == 0 && y == 0 && z == 0);
    alGetSource3f(s[0], AL_DIRECTION, &x, &y, &z);
    CHECK(x == 0 && y == 0 && z == 0);
    CHECK(srci(s[0], AL_SOURCE_RELATIVE) == AL_FALSE);
    CHECK(srci(s[0], AL_LOOPING) == AL_FALSE);
    CHECK(srci(s[0], AL_BUFFER) == 0);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_UNDETERMINED);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 0);
    CHECK(srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    CHECK(srcf(s[0], AL_SEC_OFFSET) == 0.0f);
    CHECK(srci(s[0], AL_SAMPLE_OFFSET) == 0);
    CHECK(srci(s[0], AL_BYTE_OFFSET) == 0);
    CHECK(srci(s[0], AL_REFERENCE_DISTANCE) == 1);
    CHECK(srci(s[0], AL_CONE_INNER_ANGLE) == 360);
    CHECK_AL(AL_NO_ERROR);

    /* Each form, round trip. */
    alSourcef(s[0], AL_PITCH, 2.0f);
    alSourcef(s[0], AL_GAIN, 3.0f);
    alSourcef(s[0], AL_MIN_GAIN, 0.25f);
    alSourcef(s[0], AL_MAX_GAIN, 0.75f);
    alSourcei(s[0], AL_REFERENCE_DISTANCE, 5);
    alSourcef(s[0], AL_ROLLOFF_FACTOR, 0.5f);
    fv[0] = 100.0f;
    alSourcefv(s[0], AL_MAX_DISTANCE, fv);
    iv[0] = 90;
    alSourceiv(s[0], AL_CONE_INNER_ANGLE, iv);
    alSourcef(s[0], AL_CONE_OUTER_ANGLE, 180.0f);
    alSourcef(s[0], AL_CONE_OUTER_GAIN, 0.5f);
    alSource3f(s[0], AL_POSITION, 1.0f, 2.0f, 3.0f);
    alSource3i(s[0], AL_VELOCITY, 4, 5, 6);
    iv[0] = 0; iv[1] = 0; iv[2] = -1;
    alSourceiv(s[0], AL_DIRECTION, iv);
    alSourcei(s[0], AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(s[0], AL_LOOPING, AL_TRUE);
    CHECK_AL(AL_NO_ERROR);
    CHECK(srcf(s[0], AL_PITCH) == 2.0f && srcf(s[0], AL_GAIN) == 3.0f);
    CHECK(srcf(s[0], AL_MIN_GAIN) == 0.25f && srcf(s[0], AL_MAX_GAIN) == 0.75f);
    CHECK(srcf(s[0], AL_REFERENCE_DISTANCE) == 5.0f && srcf(s[0], AL_ROLLOFF_FACTOR) == 0.5f);
    CHECK(srci(s[0], AL_MAX_DISTANCE) == 100 && srcf(s[0], AL_CONE_INNER_ANGLE) == 90.0f);
    CHECK(srcf(s[0], AL_CONE_OUTER_ANGLE) == 180.0f && srcf(s[0], AL_CONE_OUTER_GAIN) == 0.5f);
    alGetSource3i(s[0], AL_POSITION, &a, &b2, &c2);
    CHECK(a == 1 && b2 == 2 && c2 == 3);
    alGetSourcefv(s[0], AL_VELOCITY, fv);
    CHECK(fv[0] == 4.0f && fv[1] == 5.0f && fv[2] == 6.0f);
    alGetSourceiv(s[0], AL_DIRECTION, iv);
    CHECK(iv[0] == 0 && iv[1] == 0 && iv[2] == -1);
    CHECK(srci(s[0], AL_SOURCE_RELATIVE) == AL_TRUE && srci(s[0], AL_LOOPING) == AL_TRUE);
    CHECK_AL(AL_NO_ERROR);

    /* 2.15: a bad name, a value out of range (the old value stays). */
    alSourcef(bad, AL_GAIN, 1.0f);
    CHECK_AL(AL_INVALID_NAME);
    alSourcef(0, AL_GAIN, 1.0f);
    CHECK_AL(AL_INVALID_NAME);
    alGetSourcef(bad, AL_GAIN, &x);
    CHECK_AL(AL_INVALID_NAME);
    alSourcef(s[0], AL_PITCH, 0.0f);        /* (0.0f, any] */
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_PITCH, -1.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_GAIN, -0.1f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_MIN_GAIN, 1.5f);     /* [0.0f, 1.0f] */
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_MAX_GAIN, -0.1f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_CONE_OUTER_GAIN, 2.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_REFERENCE_DISTANCE, -1.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcei(s[0], AL_ROLLOFF_FACTOR, -1);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_MAX_DISTANCE, -1.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcei(s[0], AL_LOOPING, 2);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcei(s[0], AL_SOURCE_RELATIVE, -1);
    CHECK_AL(AL_INVALID_VALUE);
    alSource3f(s[0], AL_POSITION, 0.0f, NAN, 0.0f);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_GAIN, NAN);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcefv(s[0], AL_POSITION, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceiv(s[0], AL_DIRECTION, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alSourcef(s[0], AL_SEC_OFFSET, -1.0f);  /* [0.0f, any] */
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(srcf(s[0], AL_PITCH) == 2.0f && srcf(s[0], AL_GAIN) == 3.0f && srcf(s[0], AL_MIN_GAIN) == 0.25f);
    alGetSource3f(s[0], AL_POSITION, &x, &y, &z);
    CHECK(x == 1.0f && y == 2.0f && z == 3.0f);
    CHECK(srci(s[0], AL_LOOPING) == AL_TRUE);

    /* 2.7, 2.15, 2.16: tokens. */
    alSourcef(s[0], AL_LOOPING, 1.0f);      /* i, iv only */
    CHECK_AL(AL_INVALID_ENUM);
    alSourcei(s[0], AL_GAIN, 1);            /* f, fv only */
    CHECK_AL(AL_INVALID_ENUM);
    alSource3f(s[0], AL_GAIN, 1.0f, 1.0f, 1.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alSourcef(s[0], AL_POSITION, 1.0f);
    CHECK_AL(AL_INVALID_ENUM);
    alSourcei(s[0], AL_SOURCE_STATE, AL_PLAYING);   /* query only */
    CHECK_AL(AL_INVALID_ENUM);
    alSourcei(s[0], AL_BUFFERS_QUEUED, 1);
    CHECK_AL(AL_INVALID_ENUM);
    alSourcei(s[0], AL_SOURCE_TYPE, AL_STATIC);
    CHECK_AL(AL_INVALID_ENUM);
    alSourcefv(s[0], AL_ORIENTATION, fv);   /* the listener's */
    CHECK_AL(AL_INVALID_OPERATION);
    alSourcei(s[0], AL_FREQUENCY, 1);       /* a buffer's */
    CHECK_AL(AL_INVALID_OPERATION);
    alSourcei(s[0], 0x7777, 1);
    CHECK_AL(AL_INVALID_ENUM);
    alGetSourcef(s[0], AL_POSITION, &x);
    CHECK_AL(AL_INVALID_ENUM);
    alGetSourcei(s[0], AL_PITCH, &a);
    CHECK_AL(AL_INVALID_ENUM);
    alGetSourcef(s[0], AL_SOURCE_STATE, &x);
    CHECK_AL(AL_INVALID_ENUM);
    alGetSource3f(s[0], AL_GAIN, &x, &y, &z);
    CHECK_AL(AL_INVALID_ENUM);
    alGetSourcei(s[0], AL_ORIENTATION, &a);
    CHECK_AL(AL_INVALID_ENUM);
    /* A NULL destination is ignored. */
    alGetSourcef(s[0], AL_GAIN, NULL);
    alGetSource3f(s[0], AL_POSITION, NULL, &y, &z);
    alGetSourceiv(bad, 0x7777, NULL);
    CHECK_AL(AL_NO_ERROR);

    /* 4.3.1: deleting. */
    alDeleteSources(-1, s);
    CHECK_AL(AL_INVALID_VALUE);
    alDeleteSources(1, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    {
        ALuint mixed[2];
        mixed[0] = s[1];
        mixed[1] = bad;
        alDeleteSources(2, mixed);
        CHECK_AL(AL_INVALID_NAME);
        CHECK(alIsSource(s[1]) == AL_TRUE);
    }
    alDeleteSources(2, s);
    CHECK_AL(AL_NO_ERROR);
    CHECK(!alIsSource(s[0]) && !alIsSource(s[1]));
    alDeleteSources(1, s);
    CHECK_AL(AL_INVALID_NAME);
}

/* 2.12: a context's sources are limited by ALC_MONO_SOURCES and
 * ALC_STEREO_SOURCES. */
static void test_source_limit(void)
{
    ALCint attrs[] = { ALC_MONO_SOURCES, 3, ALC_STEREO_SOURCES, 1, 0 };
    ALCcontext *small = alcCreateContext(dev, attrs);
    ALuint s[6];
    ALCint v;
    CHECK(small != NULL);
    alcMakeContextCurrent(small);
    alcGetIntegerv(dev, ALC_MONO_SOURCES, 1, &v);
    CHECK(v == 3);
    alcGetIntegerv(dev, ALC_STEREO_SOURCES, 1, &v);
    CHECK(v == 1);
    alGenSources(5, s);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(small->nsources == 0);
    alGenSources(4, s);
    CHECK_AL(AL_NO_ERROR);
    alGenSources(1, s + 4);
    CHECK_AL(AL_INVALID_VALUE);
    alDeleteSources(1, s);
    alGenSources(1, s + 4);
    CHECK_AL(AL_NO_ERROR);
    /* Sources are the context's own (2.9.1). */
    alcMakeContextCurrent(ctx);
    CHECK(alIsSource(s[1]) == AL_FALSE);
    alSourcePlay(s[1]);
    CHECK_AL(AL_INVALID_NAME);
    alcMakeContextCurrent(small);
    CHECK(alIsSource(s[1]) == AL_TRUE);
    alcMakeContextCurrent(NULL);
    /* 6.2.6: its sources go with it. */
    alcDestroyContext(small);
    CHECK_ALC(dev, ALC_NO_ERROR);
    alcMakeContextCurrent(ctx);
}

/* ---- The queue and the states (4.3.2, 4.3.5, 4.3.6) --------------------------------------- */

static void test_queue_and_states(void)
{
    ALuint s[2], b[4], none = 0, bad = 4444, out[4];
    ALshort pcm[200];
    ALshort mix[2 * 64];
    int k;
    for (k = 0; k < 200; k++) {
        pcm[k] = (ALshort)(k * 10);
    }
    alGenSources(2, s);
    alGenBuffers(4, b);
    alBufferData(b[0], AL_FORMAT_MONO16, pcm, 200, 22050);
    alBufferData(b[1], AL_FORMAT_MONO16, pcm, 200, 22050);
    alBufferData(b[2], AL_FORMAT_STEREO16, pcm, 200, 22050);   /* another format */
    alBufferData(b[3], AL_FORMAT_MONO16, pcm, 200, 11025);     /* another frequency */
    CHECK_AL(AL_NO_ERROR);

    /* Source Type (4.3.2). */
    alSourcei(s[0], AL_BUFFER, (ALint)b[0]);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_STATIC);
    CHECK(srci(s[0], AL_BUFFER) == (ALint)b[0]);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 1 && srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    alSourceQueueBuffers(s[0], 1, &b[1]);       /* queue on a static source */
    CHECK_AL(AL_INVALID_OPERATION);
    alSourcei(s[0], AL_BUFFER, (ALint)bad);
    CHECK_AL(AL_INVALID_VALUE);
    CHECK(srci(s[0], AL_BUFFER) == (ALint)b[0]);
    alSourcei(s[0], AL_BUFFER, 0);
    CHECK_AL(AL_NO_ERROR);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_UNDETERMINED && srci(s[0], AL_BUFFERS_QUEUED) == 0);
    alSourceQueueBuffers(s[0], 2, b);
    CHECK_AL(AL_NO_ERROR);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_STREAMING && srci(s[0], AL_BUFFERS_QUEUED) == 2);
    CHECK(srci(s[0], AL_BUFFER) == (ALint)b[0]);

    /* 4.3.5: every buffer alike, or the queue stays as it was. */
    alSourceQueueBuffers(s[0], 1, &b[2]);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceQueueBuffers(s[0], 1, &b[3]);
    CHECK_AL(AL_INVALID_VALUE);
    {
        ALuint two[2];
        two[0] = b[1];
        two[1] = b[2];
        alSourceQueueBuffers(s[0], 2, two);
        CHECK_AL(AL_INVALID_VALUE);
    }
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 2);
    alSourceQueueBuffers(s[0], 1, &bad);
    CHECK_AL(AL_INVALID_NAME);
    alSourceQueueBuffers(bad, 1, b);
    CHECK_AL(AL_INVALID_NAME);
    alSourceQueueBuffers(s[0], -1, b);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceQueueBuffers(s[0], 1, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceQueueBuffers(s[0], 0, NULL);
    CHECK_AL(AL_NO_ERROR);
    alSourceQueueBuffers(s[0], 1, &none);       /* the NULL buffer can always be queued */
    CHECK_AL(AL_NO_ERROR);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 3);

    /* 5.1, 2.13: a buffer in a queue can't be deleted or changed. */
    alDeleteBuffers(1, &b[0]);
    CHECK_AL(AL_INVALID_OPERATION);
    {
        ALuint two[2];
        two[0] = b[3];              /* free */
        two[1] = b[1];              /* queued */
        alDeleteBuffers(2, two);
        CHECK_AL(AL_INVALID_OPERATION);
        CHECK(alIsBuffer(b[3]) == AL_TRUE);
    }
    alBufferData(b[1], AL_FORMAT_MONO16, pcm, 100, 22050);
    CHECK_AL(AL_INVALID_OPERATION);
    CHECK(bufi(b[1], AL_SIZE) == 200);

    /* 4.3.5: only processed entries come off. */
    alSourceUnqueueBuffers(s[0], 1, out);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceUnqueueBuffers(s[0], -1, out);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceUnqueueBuffers(s[0], 1, NULL);
    CHECK_AL(AL_INVALID_VALUE);
    alSourceUnqueueBuffers(bad, 1, out);
    CHECK_AL(AL_INVALID_NAME);
    alSourceUnqueueBuffers(s[0], 0, NULL);
    CHECK_AL(AL_NO_ERROR);

    /* 4.3.6's transitions. */
    alSourcePause(s[0]);                        /* INITIAL: legal NOPs */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourceStop(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourceRewind(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourcePlay(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PLAYING);
    /* AL_BUFFER on an active source. */
    alSourcei(s[0], AL_BUFFER, (ALint)b[1]);
    CHECK_AL(AL_INVALID_OPERATION);
    alSourcei(s[0], AL_BUFFER, 0);
    CHECK_AL(AL_INVALID_OPERATION);
    al_mix_render(dev, mix, 64);
    CHECK(srci(s[0], AL_SAMPLE_OFFSET) == 64);
    alSourcePlay(s[0]);                         /* PLAYING: from the start */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PLAYING && srci(s[0], AL_SAMPLE_OFFSET) == 0);
    al_mix_render(dev, mix, 10);
    alSourcePause(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PAUSED);
    al_mix_render(dev, mix, 64);                /* paused: no offset increments */
    CHECK(srci(s[0], AL_SAMPLE_OFFSET) == 10);
    alSourcePause(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PAUSED);
    alSourceUnqueueBuffers(s[0], 1, out);       /* paused, nothing processed */
    CHECK_AL(AL_INVALID_VALUE);
    alSourcePlay(s[0]);                         /* PAUSED: resumes */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PLAYING && srci(s[0], AL_SAMPLE_OFFSET) == 10);
    alSourceStop(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_STOPPED);
    /* Stopped: every entry processed, and they come off. */
    CHECK(srci(s[0], AL_BUFFERS_PROCESSED) == 3 && srci(s[0], AL_BUFFERS_QUEUED) == 3);
    CHECK(srci(s[0], AL_SAMPLE_OFFSET) == 0);
    alSourceStop(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_STOPPED);
    alSourceRewind(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL && srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    alSourcePlay(s[0]);
    alSourcePause(s[0]);
    alSourceRewind(s[0]);                       /* PAUSED -> INITIAL */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourcePlay(s[0]);
    alSourcePause(s[0]);
    alSourceStop(s[0]);                         /* PAUSED -> STOPPED */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_STOPPED);
    alSourcePlay(s[0]);                         /* STOPPED -> PLAYING */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PLAYING && srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    alSourceRewind(s[0]);                       /* PLAYING -> INITIAL */
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourceStop(s[0]);
    CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL);
    alSourcePlay(s[0]);
    alSourceStop(s[0]);
    alSourceUnqueueBuffers(s[0], 2, out);
    CHECK_AL(AL_NO_ERROR);
    CHECK(out[0] == b[0] && out[1] == b[1]);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 0 && srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    alSourceUnqueueBuffers(s[0], 1, out);
    CHECK_AL(AL_NO_ERROR);
    CHECK(out[0] == 0);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 0 && srci(s[0], AL_SOURCE_TYPE) == AL_UNDETERMINED);
    alSourceUnqueueBuffers(s[0], 1, out);
    CHECK_AL(AL_INVALID_VALUE);

    /* A queue of the NULL buffer alone counts 0 (4.3.2). */
    alSourceQueueBuffers(s[0], 1, &none);
    CHECK(srci(s[0], AL_BUFFERS_QUEUED) == 0 && srci(s[0], AL_BUFFERS_PROCESSED) == 0);
    alSourcei(s[0], AL_BUFFER, 0);

    /* Attaching any buffer to a streaming source makes it static (4.3.2). */
    alSourceQueueBuffers(s[0], 1, &b[0]);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_STREAMING);
    alSourcei(s[0], AL_BUFFER, (ALint)b[1]);
    CHECK(srci(s[0], AL_SOURCE_TYPE) == AL_STATIC && srci(s[0], AL_BUFFERS_QUEUED) == 1);
    CHECK_AL(AL_NO_ERROR);
    alDeleteBuffers(1, &b[0]);                  /* b[0] is free again */
    CHECK_AL(AL_NO_ERROR);

    /* No buffer: playing ends at once. */
    alSourcePlay(s[1]);
    CHECK(srci(s[1], AL_SOURCE_STATE) == AL_STOPPED);

    /* The vector calls: a bad name, and none changes. */
    alSourcei(s[1], AL_BUFFER, (ALint)b[1]);
    {
        ALuint two[2];
        two[0] = s[1];
        two[1] = bad;
        alSourcePlayv(2, two);
        CHECK_AL(AL_INVALID_NAME);
        CHECK(srci(s[1], AL_SOURCE_STATE) == AL_STOPPED);
        two[1] = s[0];
        alSourcePlayv(2, two);
        CHECK_AL(AL_NO_ERROR);
        CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PLAYING && srci(s[1], AL_SOURCE_STATE) == AL_PLAYING);
        alSourcePausev(2, two);
        CHECK(srci(s[0], AL_SOURCE_STATE) == AL_PAUSED && srci(s[1], AL_SOURCE_STATE) == AL_PAUSED);
        alSourceStopv(2, two);
        CHECK(srci(s[0], AL_SOURCE_STATE) == AL_STOPPED && srci(s[1], AL_SOURCE_STATE) == AL_STOPPED);
        alSourceRewindv(2, two);
        CHECK(srci(s[0], AL_SOURCE_STATE) == AL_INITIAL && srci(s[1], AL_SOURCE_STATE) == AL_INITIAL);
        alSourcePlayv(-1, two);
        CHECK_AL(AL_INVALID_VALUE);
        alSourceStopv(1, NULL);
        CHECK_AL(AL_INVALID_VALUE);
        alSourcePlay(bad);
        CHECK_AL(AL_INVALID_NAME);
    }

    /* A playing source can be deleted (4.3.1); its buffer is then free. */
    alSourcePlay(s[1]);
    alDeleteSources(1, &s[1]);
    CHECK_AL(AL_NO_ERROR);
    alSourcei(s[0], AL_BUFFER, 0);
    alDeleteBuffers(3, &b[1]);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, s);
    CHECK_AL(AL_NO_ERROR);
}

/* ---- ALC_EXT_disconnect ----------------------------------------------------------------- */

static void test_disconnect(void)
{
    ALuint s, b;
    ALshort pcm[100];
    ALCint v;
    memset(pcm, 0, sizeof pcm);
    alGenSources(1, &s);
    alGenBuffers(1, &b);
    alBufferData(b, AL_FORMAT_MONO16, pcm, sizeof pcm, 22050);
    alSourcei(s, AL_BUFFER, (ALint)b);
    alSourcei(s, AL_LOOPING, AL_TRUE);
    alSourcePlay(s);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_PLAYING);
    al_device_disconnect(dev);
    alcGetIntegerv(dev, ALC_CONNECTED, 1, &v);
    CHECK(v == ALC_FALSE);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED && srci(s, AL_BUFFERS_PROCESSED) == 1);
    alSourcePlay(s);
    CHECK(srci(s, AL_SOURCE_STATE) == AL_STOPPED);
    CHECK_AL(AL_NO_ERROR);
    alDeleteSources(1, &s);
    alDeleteBuffers(1, &b);
}

/* ---- Ending (6.1.2, 6.2.6) -------------------------------------------------------------- */

static void test_close(void)
{
    ALuint b;
    alGenBuffers(1, &b);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK_ALC(dev, ALC_NO_ERROR);
    CHECK(alcGetCurrentContext() == NULL);
    /* A buffer is left: no closing. */
    CHECK(alcCloseDevice(dev) == ALC_FALSE);
    ctx = alcCreateContext(dev, NULL);
    alcMakeContextCurrent(ctx);
    CHECK(alIsBuffer(b) == AL_TRUE);            /* buffers outlive their context (2.11) */
    alDeleteBuffers(1, &b);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    CHECK(alcCloseDevice(dev) == ALC_TRUE);
    CHECK_ALC(NULL, ALC_NO_ERROR);
}

int main(void)
{
    al_core_init();
    test_alc_no_device();
    test_alc_device();
    test_al_state();
    test_listener();
    test_buffers();
    test_source_attributes();
    test_source_limit();
    test_queue_and_states();
    test_disconnect();
    test_close();
    al_core_shutdown();
    return check_done("test_al_api");
}
