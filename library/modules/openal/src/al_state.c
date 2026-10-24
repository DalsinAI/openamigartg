/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module: the AL calls on the context's own state (OpenAL 1.1
 * sections 2.7, 2.8, 3, 4.2 and 7): errors, capabilities, the global
 * queries, the strings, extensions, the distance model, Doppler and the
 * listener.
 *
 * The errors for an attribute token follow the specification's sections:
 *   - a set call given a token of another kind of object (AL_ORIENTATION to
 *     a source, AL_PITCH to the listener): AL_INVALID_OPERATION (2.15);
 *   - any other token the call doesn't take, a query-only attribute given
 *     to a set call, or a form the attribute's signature lacks (alListeneri
 *     with AL_GAIN): AL_INVALID_ENUM (2.7, 2.16);
 *   - a value out of range, a float that is not finite, or a NULL pointer
 *     to a set call: AL_INVALID_VALUE (2.7, 2.15);
 *   - a NULL destination to a get call: the call is quietly ignored (2.16). */
#include "al_internal.h"
#include "al_attr.h"

#include <string.h>

ALboolean al_is_attribute(ALenum param)
{
    switch (param) {
    case AL_SOURCE_RELATIVE: case AL_CONE_INNER_ANGLE: case AL_CONE_OUTER_ANGLE:
    case AL_PITCH: case AL_POSITION: case AL_DIRECTION: case AL_VELOCITY: case AL_LOOPING:
    case AL_BUFFER: case AL_GAIN: case AL_MIN_GAIN: case AL_MAX_GAIN: case AL_ORIENTATION:
    case AL_SOURCE_STATE: case AL_BUFFERS_QUEUED: case AL_BUFFERS_PROCESSED:
    case AL_REFERENCE_DISTANCE: case AL_ROLLOFF_FACTOR: case AL_CONE_OUTER_GAIN:
    case AL_MAX_DISTANCE: case AL_SEC_OFFSET: case AL_SAMPLE_OFFSET: case AL_BYTE_OFFSET:
    case AL_SOURCE_TYPE: case AL_FREQUENCY: case AL_BITS: case AL_CHANNELS: case AL_SIZE:
        return AL_TRUE;
    default:
        return AL_FALSE;
    }
}

ALenum al_set_check(int forms, int form, ALenum param)
{
    if (!(forms & AL_FORMS)) {
        return al_is_attribute(param) ? AL_INVALID_OPERATION : AL_INVALID_ENUM;
    }
    if ((forms & AL_READONLY) || !(forms & form)) {
        return AL_INVALID_ENUM;
    }
    return AL_NO_ERROR;
}

ALenum al_get_check(int forms, int form)
{
    return (forms & form) ? AL_NO_ERROR : AL_INVALID_ENUM;
}

/* ---- Errors and capabilities (2.7, 2.8) ------------------------------------------ */

ALenum AL_APIENTRY alGetError(void)
{
    ALenum e;
    ALCcontext *c = al_enter();
    if (!c) {
        return AL_INVALID_OPERATION;
    }
    e = c->error;
    c->error = AL_NO_ERROR;
    al_leave(c);
    return e;
}

/* OpenAL 1.1 has no capabilities to enable. */
void AL_APIENTRY alEnable(ALenum capability)
{
    ALCcontext *c = al_enter();
    (void)capability;
    if (c) {
        al_set_error(c, AL_INVALID_ENUM);
        al_leave(c);
    }
}

void AL_APIENTRY alDisable(ALenum capability)
{
    ALCcontext *c = al_enter();
    (void)capability;
    if (c) {
        al_set_error(c, AL_INVALID_ENUM);
        al_leave(c);
    }
}

ALboolean AL_APIENTRY alIsEnabled(ALenum capability)
{
    ALCcontext *c = al_enter();
    (void)capability;
    if (c) {
        al_set_error(c, AL_INVALID_ENUM);
        al_leave(c);
    }
    return AL_FALSE;
}

/* ---- Global queries (3.1.1) -------------------------------------------------------- */

static ALboolean al_global(ALCcontext *c, ALenum param, ALdouble *v)
{
    switch (param) {
    case AL_DOPPLER_FACTOR:
        *v = c->doppler_factor;
        return AL_TRUE;
    case AL_DOPPLER_VELOCITY:
        *v = c->doppler_velocity;
        return AL_TRUE;
    case AL_SPEED_OF_SOUND:
        *v = c->speed_of_sound;
        return AL_TRUE;
    case AL_DISTANCE_MODEL:
        *v = c->distance_model;
        return AL_TRUE;
    default:
        al_set_error(c, AL_INVALID_ENUM);
        return AL_FALSE;
    }
}

void AL_APIENTRY alGetBooleanv(ALenum param, ALboolean *values)
{
    ALdouble v;
    ALCcontext *c;
    if (!values) {
        return;
    }
    if ((c = al_enter()) != NULL) {
        if (al_global(c, param, &v)) {
            *values = v != 0.0 ? AL_TRUE : AL_FALSE;
        }
        al_leave(c);
    }
}

void AL_APIENTRY alGetIntegerv(ALenum param, ALint *values)
{
    ALdouble v;
    ALCcontext *c;
    if (!values) {
        return;
    }
    if ((c = al_enter()) != NULL) {
        if (al_global(c, param, &v)) {
            *values = (ALint)v;
        }
        al_leave(c);
    }
}

void AL_APIENTRY alGetFloatv(ALenum param, ALfloat *values)
{
    ALdouble v;
    ALCcontext *c;
    if (!values) {
        return;
    }
    if ((c = al_enter()) != NULL) {
        if (al_global(c, param, &v)) {
            *values = (ALfloat)v;
        }
        al_leave(c);
    }
}

void AL_APIENTRY alGetDoublev(ALenum param, ALdouble *values)
{
    ALdouble v;
    ALCcontext *c;
    if (!values) {
        return;
    }
    if ((c = al_enter()) != NULL) {
        if (al_global(c, param, &v)) {
            *values = v;
        }
        al_leave(c);
    }
}

ALboolean AL_APIENTRY alGetBoolean(ALenum param)
{
    ALdouble v = 0.0;
    ALCcontext *c = al_enter();
    if (c) {
        al_global(c, param, &v);
        al_leave(c);
    }
    return v != 0.0 ? AL_TRUE : AL_FALSE;
}

ALint AL_APIENTRY alGetInteger(ALenum param)
{
    ALdouble v = 0.0;
    ALCcontext *c = al_enter();
    if (c) {
        al_global(c, param, &v);
        al_leave(c);
    }
    return (ALint)v;
}

ALfloat AL_APIENTRY alGetFloat(ALenum param)
{
    ALdouble v = 0.0;
    ALCcontext *c = al_enter();
    if (c) {
        al_global(c, param, &v);
        al_leave(c);
    }
    return (ALfloat)v;
}

ALdouble AL_APIENTRY alGetDouble(ALenum param)
{
    ALdouble v = 0.0;
    ALCcontext *c = al_enter();
    if (c) {
        al_global(c, param, &v);
        al_leave(c);
    }
    return v;
}

/* ---- Strings (3.1.2) -------------------------------------------------------------- */

const ALchar *AL_APIENTRY alGetString(ALenum param)
{
    const ALchar *r = NULL;
    ALCcontext *c;
    AL_LOCK();
    c = al_current;
    switch (param) {
    case AL_VENDOR:
        r = "AmigaChrome Team";
        break;
    case AL_VERSION:
        r = "1.1 OpenAL.module 2";
        break;
    case AL_RENDERER:
        r = c ? c->device->backend->name : al_backend_mix.name;
        break;
    case AL_EXTENSIONS:
        r = al_extensions;
        break;
    /* 2.7: each error's string is its name. */
    case AL_NO_ERROR:
        r = "AL_NO_ERROR";
        break;
    case AL_INVALID_NAME:
        r = "AL_INVALID_NAME";
        break;
    case AL_INVALID_ENUM:
        r = "AL_INVALID_ENUM";
        break;
    case AL_INVALID_VALUE:
        r = "AL_INVALID_VALUE";
        break;
    case AL_INVALID_OPERATION:
        r = "AL_INVALID_OPERATION";
        break;
    case AL_OUT_OF_MEMORY:
        r = "AL_OUT_OF_MEMORY";
        break;
    default:
        al_set_error(c, AL_INVALID_ENUM);
        break;
    }
    AL_UNLOCK();
    return r;
}

/* ---- Extensions (7) --------------------------------------------------------------- */

ALboolean AL_APIENTRY alIsExtensionPresent(const ALchar *extname)
{
    ALboolean r = AL_FALSE;
    AL_LOCK();
    if (!extname) {
        al_set_error(al_current, AL_INVALID_VALUE);
    } else {
        r = al_list_has(al_extensions, extname);
    }
    AL_UNLOCK();
    return r;
}

void *AL_APIENTRY alGetProcAddress(const ALchar *fname)
{
    void *r = NULL;
    AL_LOCK();
    if (!fname) {
        al_set_error(al_current, AL_INVALID_VALUE);
    } else {
        r = al_proc_address(fname);
    }
    AL_UNLOCK();
    return r;
}

ALenum AL_APIENTRY alGetEnumValue(const ALchar *ename)
{
    ALenum v = 0;
    AL_LOCK();
    if (!ename || !al_enum_value(ename, &v)) {
        al_set_error(al_current, AL_INVALID_VALUE);
        v = 0;
    }
    AL_UNLOCK();
    return v;
}

/* ---- Distance model and Doppler (3.4, 3.5.2) --------------------------------------- */

static void al_context_changed(ALCcontext *c)
{
    c->dirty = AL_TRUE;
    c->changed = AL_TRUE;
}

void AL_APIENTRY alDopplerFactor(ALfloat value)
{
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (!AL_FINITE(value) || value < 0.0f) {
        al_set_error(c, AL_INVALID_VALUE);
    } else {
        c->doppler_factor = value;
        al_context_changed(c);
    }
    al_leave(c);
}

void AL_APIENTRY alDopplerVelocity(ALfloat value)
{
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (!AL_FINITE(value) || value <= 0.0f) {
        al_set_error(c, AL_INVALID_VALUE);
    } else {
        c->doppler_velocity = value;
        al_context_changed(c);
    }
    al_leave(c);
}

void AL_APIENTRY alSpeedOfSound(ALfloat value)
{
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    if (!AL_FINITE(value) || value <= 0.0f) {
        al_set_error(c, AL_INVALID_VALUE);
    } else {
        c->speed_of_sound = value;
        al_context_changed(c);
    }
    al_leave(c);
}

void AL_APIENTRY alDistanceModel(ALenum distanceModel)
{
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    switch (distanceModel) {
    case AL_NONE:
    case AL_INVERSE_DISTANCE:
    case AL_INVERSE_DISTANCE_CLAMPED:
    case AL_LINEAR_DISTANCE:
    case AL_LINEAR_DISTANCE_CLAMPED:
    case AL_EXPONENT_DISTANCE:
    case AL_EXPONENT_DISTANCE_CLAMPED:
        c->distance_model = distanceModel;
        al_context_changed(c);
        break;
    default:
        al_set_error(c, AL_INVALID_ENUM);   /* 2.7: not one of the allowed values */
        break;
    }
    al_leave(c);
}

/* ---- The listener (4.2) ------------------------------------------------------------ */

static int al_listener_forms(ALenum param, int *count)
{
    switch (param) {
    case AL_GAIN:
        *count = 1;
        return AL_F | AL_FV;
    case AL_POSITION:
    case AL_VELOCITY:
        *count = 3;
        return AL_3F | AL_FV | AL_3I | AL_IV;
    case AL_ORIENTATION:
        *count = 6;
        return AL_FV | AL_IV;
    default:
        *count = 0;
        return 0;
    }
}

/* A set call: v holds the values (ints already made floats), or is NULL. */
static void al_listener_set(ALenum param, int form, const ALfloat *v)
{
    int count, i;
    ALenum e;
    ALCcontext *c = al_enter();
    if (!c) {
        return;
    }
    e = al_set_check(al_listener_forms(param, &count), form, param);
    if (e == AL_NO_ERROR && !v) {
        e = AL_INVALID_VALUE;
    }
    for (i = 0; e == AL_NO_ERROR && i < count; i++) {
        if (!AL_FINITE(v[i])) {
            e = AL_INVALID_VALUE;
        }
    }
    if (e == AL_NO_ERROR && param == AL_GAIN && v[0] < 0.0f) {
        e = AL_INVALID_VALUE;
    }
    if (e != AL_NO_ERROR) {
        al_set_error(c, e);
        al_leave(c);
        return;
    }
    switch (param) {
    case AL_GAIN:
        c->listener_gain = v[0];
        break;
    case AL_POSITION:
        memcpy(c->listener_position, v, 3 * sizeof *v);
        break;
    case AL_VELOCITY:
        memcpy(c->listener_velocity, v, 3 * sizeof *v);
        break;
    default:                    /* AL_ORIENTATION */
        memcpy(c->listener_at, v, 3 * sizeof *v);
        memcpy(c->listener_up, v + 3, 3 * sizeof *v);
        break;
    }
    al_context_changed(c);
    al_leave(c);
}

/* A get call: the values into v (6 room); 0 when it took the param. */
static int al_listener_get(ALCcontext *c, ALenum param, int form, ALfloat *v)
{
    int count;
    ALenum e = al_get_check(al_listener_forms(param, &count), form);
    if (e != AL_NO_ERROR) {
        al_set_error(c, e);
        return -1;
    }
    switch (param) {
    case AL_GAIN:
        v[0] = c->listener_gain;
        break;
    case AL_POSITION:
        memcpy(v, c->listener_position, 3 * sizeof *v);
        break;
    case AL_VELOCITY:
        memcpy(v, c->listener_velocity, 3 * sizeof *v);
        break;
    default:
        memcpy(v, c->listener_at, 3 * sizeof *v);
        memcpy(v + 3, c->listener_up, 3 * sizeof *v);
        break;
    }
    return count;
}

void AL_APIENTRY alListenerf(ALenum param, ALfloat value)
{
    al_listener_set(param, AL_F, &value);
}

void AL_APIENTRY alListener3f(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ALfloat v[3];
    v[0] = value1;
    v[1] = value2;
    v[2] = value3;
    al_listener_set(param, AL_3F, v);
}

void AL_APIENTRY alListenerfv(ALenum param, const ALfloat *values)
{
    al_listener_set(param, AL_FV, values);
}

void AL_APIENTRY alListeneri(ALenum param, ALint value)
{
    ALfloat v = (ALfloat)value;
    al_listener_set(param, AL_I, &v);
}

void AL_APIENTRY alListener3i(ALenum param, ALint value1, ALint value2, ALint value3)
{
    ALfloat v[3];
    v[0] = (ALfloat)value1;
    v[1] = (ALfloat)value2;
    v[2] = (ALfloat)value3;
    al_listener_set(param, AL_3I, v);
}

void AL_APIENTRY alListeneriv(ALenum param, const ALint *values)
{
    ALfloat v[6];
    int count, i;
    if (!values) {
        al_listener_set(param, AL_IV, NULL);
        return;
    }
    al_listener_forms(param, &count);
    for (i = 0; i < count; i++) {
        v[i] = (ALfloat)values[i];
    }
    al_listener_set(param, AL_IV, v);
}

void AL_APIENTRY alGetListenerf(ALenum param, ALfloat *value)
{
    ALfloat v[6];
    ALCcontext *c;
    if (!value || !(c = al_enter())) {
        return;
    }
    if (al_listener_get(c, param, AL_F, v) > 0) {
        *value = v[0];
    }
    al_leave(c);
}

void AL_APIENTRY alGetListener3f(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALfloat v[6];
    ALCcontext *c;
    if (!value1 || !value2 || !value3 || !(c = al_enter())) {
        return;
    }
    if (al_listener_get(c, param, AL_3F, v) > 0) {
        *value1 = v[0];
        *value2 = v[1];
        *value3 = v[2];
    }
    al_leave(c);
}

void AL_APIENTRY alGetListenerfv(ALenum param, ALfloat *values)
{
    ALfloat v[6];
    int n, i;
    ALCcontext *c;
    if (!values || !(c = al_enter())) {
        return;
    }
    n = al_listener_get(c, param, AL_FV, v);
    for (i = 0; i < n; i++) {
        values[i] = v[i];
    }
    al_leave(c);
}

void AL_APIENTRY alGetListeneri(ALenum param, ALint *value)
{
    ALfloat v[6];
    ALCcontext *c;
    if (!value || !(c = al_enter())) {
        return;
    }
    if (al_listener_get(c, param, AL_I, v) > 0) {
        *value = (ALint)v[0];
    }
    al_leave(c);
}

void AL_APIENTRY alGetListener3i(ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALfloat v[6];
    ALCcontext *c;
    if (!value1 || !value2 || !value3 || !(c = al_enter())) {
        return;
    }
    if (al_listener_get(c, param, AL_3I, v) > 0) {
        *value1 = (ALint)v[0];
        *value2 = (ALint)v[1];
        *value3 = (ALint)v[2];
    }
    al_leave(c);
}

void AL_APIENTRY alGetListeneriv(ALenum param, ALint *values)
{
    ALfloat v[6];
    int n, i;
    ALCcontext *c;
    if (!values || !(c = al_enter())) {
        return;
    }
    n = al_listener_get(c, param, AL_IV, v);
    for (i = 0; i < n; i++) {
        values[i] = (ALint)v[i];
    }
    al_leave(c);
}
