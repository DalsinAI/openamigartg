/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's ALC (section 6 of the OpenAL 1.1 specification): devices,
 * contexts, the ALC queries and errors, capture (refused: there is no
 * capture device), and what the core shares: the locks, the name tables,
 * the errors, and the look-ups by name for the GetProcAddress and
 * GetEnumValue calls. */
#include "al_internal.h"

#include <stdlib.h>
#include <string.h>

#ifndef AL_HOST
#include <proto/exec.h>
#endif

/* ---- Locks --------------------------------------------------------------------- */

#ifdef AL_HOST
void al_mutex_init(al_mutex *m)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
}

void al_mutex_lock(al_mutex *m)
{
    pthread_mutex_lock(m);
}

void al_mutex_unlock(al_mutex *m)
{
    pthread_mutex_unlock(m);
}
#else
void al_mutex_init(al_mutex *m)
{
    memset(m, 0, sizeof *m);
    InitSemaphore(m);
}

void al_mutex_lock(al_mutex *m)
{
    ObtainSemaphore(m);
}

void al_mutex_unlock(al_mutex *m)
{
    ReleaseSemaphore(m);
}
#endif

al_mutex al_device_lock;
al_mutex al_state_lock;

/* ---- What a program has open ------------------------------------------------------ */

ALCcontext *al_current;                 /* 6.2.3: one for the whole program */
struct al_names al_buffer_names, al_source_names;
static ALCdevice *al_devices;
static ALCenum alc_error;               /* errors with no valid device to keep them */
static int al_ready;

/* The back ends, in the order alcOpenDevice(NULL) tries them: Voices when
 * the board has a Host mix mode and voices free, Mix otherwise. */
static const struct al_backend *const al_backends[] = { &al_backend_voices, &al_backend_mix };
#define AL_NBACKENDS (sizeof(al_backends) / sizeof(al_backends[0]))
/* ALC_DEVICE_SPECIFIER with no device (6.3.7, ALC_ENUMERATION_EXT): each
 * name, then a second NUL. */
static const char al_device_list[] = "AHI Voices\0AHI Mix\0";

#define AL_DEFAULT_REFRESH 25           /* updates a second: 40 ms each */
#define AL_DEFAULT_FREQUENCY 22050      /* when the output doesn't say */
#define AL_MIN_FREQUENCY 8000
#define AL_MAX_FREQUENCY 96000
#define AL_DEFAULT_MONO 256
#define AL_DEFAULT_STEREO 16
#define AL_MAX_SOURCES 1024             /* in one context */
#define AL_ATTRIBUTES 11                /* ALC_ALL_ATTRIBUTES: five pairs and the 0 */

void al_core_init(void)
{
    if (al_ready) {
        return;
    }
    al_mutex_init(&al_device_lock);
    al_mutex_init(&al_state_lock);
    al_current = NULL;
    al_devices = NULL;
    alc_error = ALC_NO_ERROR;
    memset(&al_buffer_names, 0, sizeof al_buffer_names);
    memset(&al_source_names, 0, sizeof al_source_names);
    al_ready = 1;
}

/* ---- Names (2.11) --------------------------------------------------------------- */

ALuint al_names_add(struct al_names *t, void *item)
{
    ALuint i, room = t->room;
    void **items;
    /* The next free name after the last one given, so a name just deleted
     * isn't given again at once. */
    for (i = 0; i < room; i++) {
        ALuint k = (t->hint + i) % room;
        if (!t->items[k]) {
            t->items[k] = item;
            t->hint = k + 1;
            return k + 1;
        }
    }
    room = room ? room * 2 : 64;
    items = realloc(t->items, room * sizeof *items);
    if (!items) {
        return 0;
    }
    memset(items + t->room, 0, (room - t->room) * sizeof *items);
    items[t->room] = item;
    t->hint = t->room + 1;
    t->items = items;
    i = t->room + 1;
    t->room = room;
    return i;
}

void *al_names_get(const struct al_names *t, ALuint name)
{
    if (name == 0 || name > t->room) {
        return NULL;
    }
    return t->items[name - 1];
}

void al_names_remove(struct al_names *t, ALuint name)
{
    if (name != 0 && name <= t->room) {
        t->items[name - 1] = NULL;
    }
}

void al_names_free(struct al_names *t)
{
    free(t->items);
    memset(t, 0, sizeof *t);
}

/* ---- Errors, and the start and end of an AL call ------------------------------------ */

void al_set_error(ALCcontext *context, ALenum error)
{
    if (context && context->error == AL_NO_ERROR) {
        context->error = error;
    }
}

void alc_set_error(ALCdevice *device, ALCenum error)
{
    if (device) {
        if (device->error == ALC_NO_ERROR) {
            device->error = error;
        }
    } else if (alc_error == ALC_NO_ERROR) {
        alc_error = error;
    }
}

ALCcontext *al_enter(void)
{
    ALCcontext *c;
    AL_LOCK();
    c = al_current;
    if (!c) {
        AL_UNLOCK();
    } else if (c->device->backend->sync) {
        c->device->backend->sync(c);
    }
    return c;
}

/* The back end hears of every change, suspended or not: Mix needs no word
 * (it reads the state as it mixes); Voices sends plays, stops and queues to
 * the board at once and the rest when the context isn't suspended. */
void al_leave(ALCcontext *c)
{
    if (c->changed) {
        c->changed = AL_FALSE;
        if (c->device->backend->update) {
            c->device->backend->update(c);
        }
    }
    AL_UNLOCK();
}

ALCboolean alc_device_valid(const ALCdevice *device)
{
    const ALCdevice *d;
    for (d = al_devices; d; d = d->next) {
        if (d == device) {
            return ALC_TRUE;
        }
    }
    return ALC_FALSE;
}

static ALCboolean alc_context_valid(const ALCcontext *context)
{
    const ALCdevice *d;
    const ALCcontext *c;
    if (!context) {
        return ALC_FALSE;
    }
    for (d = al_devices; d; d = d->next) {
        for (c = d->contexts; c; c = c->next) {
            if (c == context) {
                return ALC_TRUE;
            }
        }
    }
    return ALC_FALSE;
}

/* A device argument: the device when it is valid, else NULL and *bad set
 * when it was given but isn't one. */
static ALCdevice *alc_device_arg(ALCdevice *device, int *bad)
{
    *bad = 0;
    if (!device) {
        return NULL;
    }
    if (!alc_device_valid(device)) {
        *bad = 1;
        return NULL;
    }
    return device;
}

/* ---- Look-ups by name ------------------------------------------------------------ */

ALboolean al_list_has(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    if (n == 0) {
        return AL_FALSE;
    }
    while (*p) {
        size_t len = 0, i;
        while (p[len] && p[len] != ' ') {
            len++;
        }
        if (len == n) {
            for (i = 0; i < n; i++) {
                char a = p[i], b = name[i];
                if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
                if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
                if (a != b) {
                    break;
                }
            }
            if (i == n) {
                return AL_TRUE;
            }
        }
        p += len;
        while (*p == ' ') {
            p++;
        }
    }
    return AL_FALSE;
}

/* AL_EXTENSIONS and ALC_EXTENSIONS. The 1.0-compatible names of OpenAL
 * 1.1's own features are there (appendix 9: a 1.1 implementation always
 * has them). ALC_EXT_CAPTURE's calls are there and say there is no capture
 * device (6.4.2). */
const char al_extensions[] = "AL_EXT_EXPONENT_DISTANCE AL_EXT_FLOAT32 AL_EXT_LINEAR_DISTANCE AL_EXT_MCFORMATS AL_EXT_OFFSET";
const char alc_extensions[] = "ALC_ENUMERATION_EXT ALC_EXT_CAPTURE ALC_EXT_disconnect";

struct al_proc_name {
    const char *name;
    void *address;
};

static const struct al_proc_name al_procs[] = {
#define OPENAL_PROC(rc, fn, params, args) { #fn, (void *)fn },
#define OPENAL_PROCV(fn, params, args) { #fn, (void *)fn },
#include "openal_procs.h"
#undef OPENAL_PROC
#undef OPENAL_PROCV
};

void *al_proc_address(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof al_procs / sizeof al_procs[0]; i++) {
        if (!strcmp(al_procs[i].name, name)) {
            return al_procs[i].address;
        }
    }
    return NULL;
}

struct al_enum_name {
    const char *name;
    ALenum value;
};

#define E(x) { #x, x },
static const struct al_enum_name al_enums[] = {
    E(AL_NONE) E(AL_FALSE) E(AL_TRUE)
    E(AL_SOURCE_RELATIVE) E(AL_CONE_INNER_ANGLE) E(AL_CONE_OUTER_ANGLE) E(AL_PITCH)
    E(AL_POSITION) E(AL_DIRECTION) E(AL_VELOCITY) E(AL_LOOPING) E(AL_BUFFER) E(AL_GAIN)
    E(AL_MIN_GAIN) E(AL_MAX_GAIN) E(AL_ORIENTATION) E(AL_SOURCE_STATE) E(AL_INITIAL)
    E(AL_PLAYING) E(AL_PAUSED) E(AL_STOPPED) E(AL_BUFFERS_QUEUED) E(AL_BUFFERS_PROCESSED)
    E(AL_REFERENCE_DISTANCE) E(AL_ROLLOFF_FACTOR) E(AL_CONE_OUTER_GAIN) E(AL_MAX_DISTANCE)
    E(AL_SEC_OFFSET) E(AL_SAMPLE_OFFSET) E(AL_BYTE_OFFSET) E(AL_SOURCE_TYPE) E(AL_STATIC)
    E(AL_STREAMING) E(AL_UNDETERMINED)
    E(AL_FORMAT_MONO8) E(AL_FORMAT_MONO16) E(AL_FORMAT_STEREO8) E(AL_FORMAT_STEREO16)
    E(AL_FREQUENCY) E(AL_BITS) E(AL_CHANNELS) E(AL_SIZE)
    E(AL_UNUSED) E(AL_PENDING) E(AL_PROCESSED)
    E(AL_NO_ERROR) E(AL_INVALID_NAME) E(AL_INVALID_ENUM) E(AL_INVALID_VALUE)
    E(AL_INVALID_OPERATION) E(AL_OUT_OF_MEMORY)
    E(AL_VENDOR) E(AL_VERSION) E(AL_RENDERER) E(AL_EXTENSIONS)
    E(AL_DOPPLER_FACTOR) E(AL_DOPPLER_VELOCITY) E(AL_SPEED_OF_SOUND) E(AL_DISTANCE_MODEL)
    E(AL_INVERSE_DISTANCE) E(AL_INVERSE_DISTANCE_CLAMPED) E(AL_LINEAR_DISTANCE)
    E(AL_LINEAR_DISTANCE_CLAMPED) E(AL_EXPONENT_DISTANCE) E(AL_EXPONENT_DISTANCE_CLAMPED)
    E(AL_FORMAT_MONO_FLOAT32) E(AL_FORMAT_STEREO_FLOAT32)
    E(AL_FORMAT_QUAD8) E(AL_FORMAT_QUAD16) E(AL_FORMAT_QUAD32)
    E(AL_FORMAT_REAR8) E(AL_FORMAT_REAR16) E(AL_FORMAT_REAR32)
    E(AL_FORMAT_51CHN8) E(AL_FORMAT_51CHN16) E(AL_FORMAT_51CHN32)
    E(AL_FORMAT_61CHN8) E(AL_FORMAT_61CHN16) E(AL_FORMAT_61CHN32)
    E(AL_FORMAT_71CHN8) E(AL_FORMAT_71CHN16) E(AL_FORMAT_71CHN32)
    E(ALC_FALSE) E(ALC_TRUE)
    E(ALC_FREQUENCY) E(ALC_REFRESH) E(ALC_SYNC) E(ALC_MONO_SOURCES) E(ALC_STEREO_SOURCES)
    E(ALC_NO_ERROR) E(ALC_INVALID_DEVICE) E(ALC_INVALID_CONTEXT) E(ALC_INVALID_ENUM)
    E(ALC_INVALID_VALUE) E(ALC_OUT_OF_MEMORY)
    E(ALC_MAJOR_VERSION) E(ALC_MINOR_VERSION) E(ALC_ATTRIBUTES_SIZE) E(ALC_ALL_ATTRIBUTES)
    E(ALC_DEFAULT_DEVICE_SPECIFIER) E(ALC_DEVICE_SPECIFIER) E(ALC_EXTENSIONS)
    E(ALC_CAPTURE_DEVICE_SPECIFIER) E(ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER) E(ALC_CAPTURE_SAMPLES)
    E(ALC_CONNECTED)
};
#undef E

ALboolean al_enum_value(const char *name, ALenum *value)
{
    size_t i;
    for (i = 0; i < sizeof al_enums / sizeof al_enums[0]; i++) {
        if (!strcmp(al_enums[i].name, name)) {
            *value = al_enums[i].value;
            return AL_TRUE;
        }
    }
    return AL_FALSE;
}

/* ---- Devices (6.1) -------------------------------------------------------------- */

static void alc_free_contexts(ALCdevice *device);

/* The default device: the first back end that has an output now. */
static const struct al_backend *alc_default_backend(void)
{
    size_t i;
    for (i = 0; i + 1 < AL_NBACKENDS; i++) {
        if (!al_backends[i]->available || al_backends[i]->available()) {
            return al_backends[i];
        }
    }
    return al_backends[AL_NBACKENDS - 1];
}

ALCdevice *ALC_APIENTRY alcOpenDevice(const ALCchar *devicename)
{
    const struct al_backend *b = NULL;
    ALCdevice *d;
    size_t i, first = 0, last = AL_NBACKENDS;

    al_mutex_lock(&al_device_lock);
    /* No name: each back end in turn, until one opens (Voices fails when
     * the board has no Host mix mode or no voices free). A name: that one. */
    if (devicename && devicename[0]) {
        for (i = 0; i < AL_NBACKENDS; i++) {
            if (!strcmp(devicename, al_backends[i]->name)) {
                b = al_backends[i];
                first = i;
                last = i + 1;
            }
        }
        if (!b) {
            AL_LOCK();
            alc_set_error(NULL, ALC_INVALID_VALUE);
            AL_UNLOCK();
            al_mutex_unlock(&al_device_lock);
            return NULL;
        }
    }
    d = calloc(1, sizeof *d);
    if (!d) {
        AL_LOCK();
        alc_set_error(NULL, ALC_OUT_OF_MEMORY);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return NULL;
    }
    for (i = first; i < last; i++) {
        b = al_backends[i];
        memset(d, 0, sizeof *d);
        d->backend = b;
        d->connected = ALC_TRUE;
        d->refresh = AL_DEFAULT_REFRESH;
        if (b->open(d)) {
            break;
        }
    }
    if (i == last) {
        free(d);
        AL_LOCK();
        alc_set_error(NULL, ALC_INVALID_VALUE);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return NULL;
    }
    if (d->frequency < AL_MIN_FREQUENCY || d->frequency > AL_MAX_FREQUENCY) {
        d->frequency = AL_DEFAULT_FREQUENCY;
    }
    AL_LOCK();
    d->next = al_devices;
    al_devices = d;
    AL_UNLOCK();
    al_mutex_unlock(&al_device_lock);
    return d;
}

/* Every context of the device and its sources: under both locks. */
static void alc_free_context(ALCcontext *c)
{
    ALCdevice *d = c->device;
    ALCcontext **p;
    while (c->sources) {
        al_source_free(c->sources);
    }
    for (p = &d->contexts; *p; p = &(*p)->next) {
        if (*p == c) {
            *p = c->next;
            break;
        }
    }
    if (al_current == c) {
        al_current = NULL;
    }
    free(c);
}

static void alc_free_contexts(ALCdevice *device)
{
    while (device->contexts) {
        alc_free_context(device->contexts);
    }
}

/* The device's buffers: under both locks, its sources gone. */
static void alc_free_buffers(ALCdevice *device)
{
    ALuint i;
    for (i = 1; i <= al_buffer_names.room; i++) {
        ALbuffer *b = al_names_get(&al_buffer_names, i);
        if (b && b->device == device) {
            al_names_remove(&al_buffer_names, i);
            al_buffer_free(b);
        }
    }
    device->buffers = 0;
}

/* Under al_device_lock: the device stops, everything on it goes, and it is
 * closed and freed. */
static void alc_end_device(ALCdevice *d)
{
    ALCdevice **p;
    if (d->running) {
        d->backend->stop(d);
        d->running = ALC_FALSE;
    }
    AL_LOCK();
    alc_free_contexts(d);
    alc_free_buffers(d);
    for (p = &al_devices; *p; p = &(*p)->next) {
        if (*p == d) {
            *p = d->next;
            break;
        }
    }
    AL_UNLOCK();
    d->backend->close(d);
    free(d->mix);
    free(d);
}

ALCboolean ALC_APIENTRY alcCloseDevice(ALCdevice *device)
{
    al_mutex_lock(&al_device_lock);
    AL_LOCK();
    if (!alc_device_valid(device)) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return ALC_FALSE;
    }
    /* 6.1.2: "Failure will occur if all the device's contexts and buffers
     * have not been destroyed." */
    if (device->contexts || device->buffers) {
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return ALC_FALSE;
    }
    AL_UNLOCK();
    alc_end_device(device);
    al_mutex_unlock(&al_device_lock);
    return ALC_TRUE;
}

void al_core_shutdown(void)
{
    if (!al_ready) {
        return;
    }
    al_mutex_lock(&al_device_lock);
    while (al_devices) {
        alc_end_device(al_devices);
    }
    AL_LOCK();
    al_current = NULL;
    al_names_free(&al_buffer_names);
    al_names_free(&al_source_names);
    AL_UNLOCK();
    al_mutex_unlock(&al_device_lock);
}

void al_device_disconnect(ALCdevice *device)
{
    ALCcontext *c;
    ALsource *s;
    AL_LOCK();
    if (device->connected) {
        device->connected = ALC_FALSE;
        for (c = device->contexts; c; c = c->next) {
            for (s = c->sources; s; s = s->next) {
                if (s->state == AL_PLAYING || s->state == AL_PAUSED) {
                    al_source_stop_all(s);
                }
            }
        }
    }
    AL_UNLOCK();
}

/* ---- Contexts (6.2) ------------------------------------------------------------- */

ALCcontext *ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrlist)
{
    ALCcontext *c;
    ALCint freq = 0, refresh = 0, sync = 0, mono = AL_DEFAULT_MONO, stereo = AL_DEFAULT_STEREO;
    int have_freq = 0;
    ALCenum bad = ALC_NO_ERROR;

    al_mutex_lock(&al_device_lock);
    AL_LOCK();
    if (!alc_device_valid(device)) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return NULL;
    }
    /* 6.2.2: a zero-ended list of pairs. Tokens the module doesn't know
     * (another implementation's, EFX's) are passed over. */
    if (attrlist) {
        const ALCint *a;
        for (a = attrlist; a[0]; a += 2) {
            switch (a[0]) {
            case ALC_FREQUENCY:
                freq = a[1];
                have_freq = 1;
                if (freq < AL_MIN_FREQUENCY || freq > AL_MAX_FREQUENCY) {
                    bad = ALC_INVALID_VALUE;
                }
                break;
            case ALC_REFRESH:
                refresh = a[1];
                if (refresh <= 0) {
                    bad = ALC_INVALID_VALUE;
                }
                break;
            case ALC_SYNC:
                sync = a[1];
                break;
            case ALC_MONO_SOURCES:
                mono = a[1];
                if (mono < 0) {
                    bad = ALC_INVALID_VALUE;
                }
                break;
            case ALC_STEREO_SOURCES:
                stereo = a[1];
                if (stereo < 0) {
                    bad = ALC_INVALID_VALUE;
                }
                break;
            default:
                break;
            }
        }
    }
    /* 6.2.2 (a), (b): what can't be given. A synchronous context can't (the
     * output runs by itself), nor another rate on a device that plays. */
    if (sync) {
        bad = ALC_INVALID_VALUE;
    }
    /* An output with a rate of its own (Voices: the board's 44.1 kHz)
     * takes ALC_FREQUENCY and ALC_REFRESH as hints, and keeps its own. */
    if (device->backend->flags & AL_BACKEND_FIXED_RATE) {
        have_freq = 0;
        refresh = 0;
    }
    if (device->running && have_freq && (ALuint)freq != device->frequency) {
        bad = ALC_INVALID_VALUE;
    }
    if (bad != ALC_NO_ERROR) {
        alc_set_error(device, bad);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return NULL;
    }
    c = calloc(1, sizeof *c);
    if (!c) {
        alc_set_error(device, ALC_OUT_OF_MEMORY);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return NULL;
    }
    c->device = device;
    c->listener_at[2] = -1.0f;
    c->listener_up[1] = 1.0f;
    c->listener_gain = 1.0f;
    c->distance_model = AL_INVERSE_DISTANCE_CLAMPED;
    c->doppler_factor = 1.0f;
    c->doppler_velocity = 1.0f;
    c->speed_of_sound = 343.3f;
    if (stereo > AL_MAX_SOURCES) {
        stereo = AL_MAX_SOURCES;
    }
    if (mono > AL_MAX_SOURCES - stereo) {
        mono = AL_MAX_SOURCES - stereo;
    }
    c->mono_sources = mono;
    c->stereo_sources = stereo;
    c->max_sources = (ALuint)(mono + stereo);
    AL_UNLOCK();

    /* The first context starts the output, at the rate it asks for. */
    if (!device->running) {
        ALint *mix;
        ALuint frames;
        AL_LOCK();
        if (have_freq) {
            device->frequency = (ALuint)freq;
        }
        if (refresh > 0) {
            device->refresh = (ALuint)refresh;
        }
        frames = device->frequency / device->refresh;
        if (frames < 64) {
            frames = 64;
        }
        if (frames > 16384) {
            frames = 16384;
        }
        mix = realloc(device->mix, (size_t)frames * 2 * sizeof *mix);
        if (mix) {
            device->mix = mix;
            device->update_frames = frames;
        }
        AL_UNLOCK();
        if (!mix || !device->backend->start(device)) {
            free(c);
            AL_LOCK();
            alc_set_error(device, ALC_OUT_OF_MEMORY);
            AL_UNLOCK();
            al_mutex_unlock(&al_device_lock);
            return NULL;
        }
        device->running = ALC_TRUE;
    }
    AL_LOCK();
    c->next = device->contexts;
    device->contexts = c;
    AL_UNLOCK();
    al_mutex_unlock(&al_device_lock);
    return c;
}

ALCboolean ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context)
{
    AL_LOCK();
    if (context && !alc_context_valid(context)) {
        alc_set_error(NULL, ALC_INVALID_CONTEXT);
        AL_UNLOCK();
        return ALC_FALSE;
    }
    al_current = context;
    AL_UNLOCK();
    return ALC_TRUE;
}

void ALC_APIENTRY alcProcessContext(ALCcontext *context)
{
    AL_LOCK();
    if (!alc_context_valid(context)) {
        alc_set_error(NULL, ALC_INVALID_CONTEXT);
    } else if (context->suspended) {
        context->suspended = AL_FALSE;
        context->changed = AL_FALSE;
        if (context->device->backend->update) {
            context->device->backend->update(context);
        }
    }
    AL_UNLOCK();
}

void ALC_APIENTRY alcSuspendContext(ALCcontext *context)
{
    AL_LOCK();
    if (!alc_context_valid(context)) {
        alc_set_error(NULL, ALC_INVALID_CONTEXT);
    } else {
        context->suspended = AL_TRUE;
    }
    AL_UNLOCK();
}

void ALC_APIENTRY alcDestroyContext(ALCcontext *context)
{
    ALCdevice *d;
    al_mutex_lock(&al_device_lock);
    AL_LOCK();
    if (!alc_context_valid(context)) {
        alc_set_error(NULL, ALC_INVALID_CONTEXT);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return;
    }
    d = context->device;
    /* 6.2.6: a current context can't be destroyed. ALC has no
     * ALC_INVALID_OPERATION token, which the text names; ALC_INVALID_VALUE
     * is the error that says the call doesn't apply. */
    if (context == al_current) {
        alc_set_error(d, ALC_INVALID_VALUE);
        AL_UNLOCK();
        al_mutex_unlock(&al_device_lock);
        return;
    }
    alc_free_context(context);
    AL_UNLOCK();
    if (!d->contexts && d->running) {
        d->backend->stop(d);
        d->running = ALC_FALSE;
    }
    al_mutex_unlock(&al_device_lock);
}

ALCcontext *ALC_APIENTRY alcGetCurrentContext(void)
{
    ALCcontext *c;
    AL_LOCK();
    c = al_current;
    AL_UNLOCK();
    return c;
}

ALCdevice *ALC_APIENTRY alcGetContextsDevice(ALCcontext *context)
{
    ALCdevice *d = NULL;
    AL_LOCK();
    if (!alc_context_valid(context)) {
        alc_set_error(NULL, ALC_INVALID_CONTEXT);
    } else {
        d = context->device;
    }
    AL_UNLOCK();
    return d;
}

/* ---- Queries (6.3) -------------------------------------------------------------- */

ALCenum ALC_APIENTRY alcGetError(ALCdevice *device)
{
    ALCenum e;
    AL_LOCK();
    if (!device) {
        e = alc_error;
        alc_error = ALC_NO_ERROR;
    } else if (!alc_device_valid(device)) {
        e = ALC_INVALID_DEVICE;
    } else {
        e = device->error;
        device->error = ALC_NO_ERROR;
    }
    AL_UNLOCK();
    return e;
}

ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname)
{
    int bad;
    ALCboolean r = ALC_FALSE;
    ALCdevice *d;
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    if (bad) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
    } else if (!extname) {
        alc_set_error(d, ALC_INVALID_VALUE);
    } else {
        r = al_list_has(alc_extensions, extname) ? ALC_TRUE : ALC_FALSE;
    }
    AL_UNLOCK();
    return r;
}

void *ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcname)
{
    int bad;
    void *r = NULL;
    ALCdevice *d;
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    if (bad) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
    } else if (!funcname) {
        alc_set_error(d, ALC_INVALID_VALUE);
    } else {
        r = al_proc_address(funcname);
    }
    AL_UNLOCK();
    return r;
}

ALCenum ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumname)
{
    int bad;
    ALenum v = 0;
    ALCdevice *d;
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    if (bad) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
    } else if (!enumname || !al_enum_value(enumname, &v)) {
        alc_set_error(d, ALC_INVALID_VALUE);
        v = 0;
    }
    AL_UNLOCK();
    return v;
}

const ALCchar *ALC_APIENTRY alcGetString(ALCdevice *device, ALCenum param)
{
    int bad;
    const ALCchar *r = NULL;
    ALCdevice *d;
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    switch (param) {
    /* 6.3.7: the error tokens' strings, as their names (2.7). */
    case ALC_NO_ERROR:
        r = "ALC_NO_ERROR";
        break;
    case ALC_INVALID_DEVICE:
        r = "ALC_INVALID_DEVICE";
        break;
    case ALC_INVALID_CONTEXT:
        r = "ALC_INVALID_CONTEXT";
        break;
    case ALC_INVALID_ENUM:
        r = "ALC_INVALID_ENUM";
        break;
    case ALC_INVALID_VALUE:
        r = "ALC_INVALID_VALUE";
        break;
    case ALC_OUT_OF_MEMORY:
        r = "ALC_OUT_OF_MEMORY";
        break;
    case ALC_DEFAULT_DEVICE_SPECIFIER:
        r = alc_default_backend()->name;    /* the device is ignored */
        break;
    case ALC_DEVICE_SPECIFIER:
        if (bad) {
            alc_set_error(NULL, ALC_INVALID_DEVICE);
        } else {
            r = d ? d->backend->name : al_device_list;
        }
        break;
    case ALC_EXTENSIONS:
        if (!d) {
            alc_set_error(NULL, ALC_INVALID_DEVICE);
        } else {
            r = alc_extensions;
        }
        break;
    /* No capture device: none is the default, the list is empty, and no
     * device is a capture device. */
    case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
        break;
    case ALC_CAPTURE_DEVICE_SPECIFIER:
        if (device) {
            alc_set_error(d, ALC_INVALID_DEVICE);
        } else {
            r = "\0";
        }
        break;
    default:
        alc_set_error(d, bad ? ALC_INVALID_DEVICE : ALC_INVALID_ENUM);
        break;
    }
    AL_UNLOCK();
    return r;
}

/* The device's attributes as alcGetIntegerv gives them (table 6-4): the
 * current context's, when it is on this device; else its first one's. */
static void alc_attributes(ALCdevice *d, ALCint *a)
{
    ALCcontext *c = (al_current && al_current->device == d) ? al_current : d->contexts;
    a[0] = ALC_FREQUENCY;
    a[1] = (ALCint)d->frequency;
    a[2] = ALC_REFRESH;
    a[3] = (ALCint)d->refresh;
    a[4] = ALC_SYNC;
    a[5] = ALC_FALSE;
    a[6] = ALC_MONO_SOURCES;
    a[7] = c ? c->mono_sources : AL_DEFAULT_MONO;
    a[8] = ALC_STEREO_SOURCES;
    a[9] = c ? c->stereo_sources : AL_DEFAULT_STEREO;
    a[10] = 0;
}

void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
    int bad;
    ALCdevice *d;
    ALCint a[AL_ATTRIBUTES];
    /* 6.3.8: "A NULL destination or a zero size parameter will cause ALC
     * to ignore the query." */
    if (!values || size <= 0) {
        return;
    }
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    if (bad) {
        alc_set_error(NULL, ALC_INVALID_DEVICE);
        AL_UNLOCK();
        return;
    }
    switch (param) {
    case ALC_MAJOR_VERSION:
        values[0] = 1;
        break;
    case ALC_MINOR_VERSION:
        values[0] = 1;
        break;
    case ALC_ATTRIBUTES_SIZE:
    case ALC_ALL_ATTRIBUTES:
    case ALC_FREQUENCY:
    case ALC_REFRESH:
    case ALC_SYNC:
    case ALC_MONO_SOURCES:
    case ALC_STEREO_SOURCES:
    case ALC_CONNECTED:
    case ALC_CAPTURE_SAMPLES:
        if (!d) {
            alc_set_error(NULL, ALC_INVALID_DEVICE);
            break;
        }
        alc_attributes(d, a);
        switch (param) {
        case ALC_ATTRIBUTES_SIZE:
            values[0] = AL_ATTRIBUTES;
            break;
        case ALC_ALL_ATTRIBUTES:
            if (size < AL_ATTRIBUTES) {
                alc_set_error(d, ALC_INVALID_VALUE);
            } else {
                memcpy(values, a, sizeof a);
            }
            break;
        case ALC_FREQUENCY:
            values[0] = a[1];
            break;
        case ALC_REFRESH:
            values[0] = a[3];
            break;
        case ALC_SYNC:
            values[0] = a[5];
            break;
        case ALC_MONO_SOURCES:
            values[0] = a[7];
            break;
        case ALC_STEREO_SOURCES:
            values[0] = a[9];
            break;
        case ALC_CONNECTED:
            values[0] = d->connected;
            break;
        default:                /* ALC_CAPTURE_SAMPLES: not a capture device */
            alc_set_error(d, ALC_INVALID_DEVICE);
            break;
        }
        break;
    default:
        alc_set_error(d, ALC_INVALID_ENUM);
        break;
    }
    AL_UNLOCK();
}

/* ---- Capture (6.4.2): there is no capture device ----------------------------------- */

ALCdevice *ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize)
{
    (void)devicename;
    (void)frequency;
    (void)format;
    (void)buffersize;
    AL_LOCK();
    alc_set_error(NULL, ALC_INVALID_VALUE);
    AL_UNLOCK();
    return NULL;
}

static void alc_not_capture(ALCdevice *device)
{
    int bad;
    ALCdevice *d;
    AL_LOCK();
    d = alc_device_arg(device, &bad);
    alc_set_error(d, ALC_INVALID_DEVICE);
    AL_UNLOCK();
}

ALCboolean ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device)
{
    alc_not_capture(device);
    return ALC_FALSE;
}

void ALC_APIENTRY alcCaptureStart(ALCdevice *device)
{
    alc_not_capture(device);
}

void ALC_APIENTRY alcCaptureStop(ALCdevice *device)
{
    alc_not_capture(device);
}

void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
{
    (void)buffer;
    (void)samples;
    alc_not_capture(device);
}

#ifdef AL_STATIC_BUILD
/* libopenal_static.a: OpenAL inside the program, set up and ended with it. */
static void __attribute__((constructor)) al_static_start(void)
{
    al_core_init();
}

static void __attribute__((destructor)) al_static_end(void)
{
    al_core_shutdown();
}
#endif
